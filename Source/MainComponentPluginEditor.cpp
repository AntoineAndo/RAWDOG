#include "MainComponent.h"
#include "PluginHost.h"
#include "SampleFormat.h"

void MainComponent::loadAndOpenPlugin(int row)
{
    auto* desc = listModel.getType(row);
    if (desc == nullptr)
        return;

    if (currentPlugin != nullptr)
    {
        endLivePreviewSession(false); // discards any unapplied live preview and detaches the watcher
        currentPlugin->releaseResources();
        currentPlugin = nullptr;
    }

    juce::String errorMessage;
    currentPlugin = PluginHost::createInstance(scanner.getFormatManager(), *desc, sampleRate, blockSize, errorMessage);

    if (currentPlugin == nullptr)
    {
        setStatus("Failed to load plugin: " + errorMessage);
        return;
    }

    pluginParamWatcher.attachTo(*currentPlugin);

    setStatus("Loaded plugin: " + desc->name);
    openEditorClicked();
}

void MainComponent::openEditorClicked()
{
    if (currentPlugin == nullptr)
    {
        setStatus("Load a plugin first.");
        return;
    }

    if (pluginEditorPanel != nullptr)
        return;

    if (headerEditorPanel != nullptr)
    {
        setStatus("Finish editing the header first (Apply or Cancel).");
        return;
    }

    auto* editor = currentPlugin->hasEditor() ? currentPlugin->createEditorIfNeeded()
                                               : new juce::GenericAudioProcessorEditor(*currentPlugin);

    if (editor == nullptr)
    {
        setStatus("This plugin has no editor UI.");
        return;
    }

    pluginEditorPanel = std::make_unique<PluginEditorPanel>(std::unique_ptr<juce::AudioProcessorEditor>(editor), [this]
    {
        applyClicked();
    },
    [this]
    {
        cancelEditorClicked();
    });

    leftColumn.setEditorPanel(pluginEditorPanel.get());

    // Re-seed the left/right column split so the left column starts out sized
    // to fit this plugin's editor, capped at half the window width — same
    // "reseed only on a newly-opened panel" pattern as leftColumn's internal
    // list/panel split, so ordinary window resizes afterward don't reset it.
    outerLayout.setItemLayout(0, 200, -0.5, pluginEditorPanel->getPreferredWidth());
    resized();

    updatePluginListEnablement();
    menuModel.menuItemsChanged();

    // Reflects the plugin's default parameter state immediately, before any tweak,
    // so livePreviewBytes is populated even if Apply is clicked with zero changes.
    refreshLivePreview();
}

juce::MemoryBlock MainComponent::computeProcessedPixelBytes(juce::AudioPluginInstance& plugin,
                                                              const juce::Range<int>& selection,
                                                              std::optional<RawImage::Channel> channel)
{
    plugin.reset(); // clean DSP state before every independent reprocessing pass, so repeated
                     // live-preview passes against the same source bytes stay deterministic for
                     // stateful plugins (filters, reverbs, etc).

    // Channel-scoped: process that channel's deinterleaved plane instead of
    // the interleaved buffer. Same selection-scoping logic either way — the
    // caller (refreshLivePreview/applyClicked) decides whether the result is
    // an edited plane (to commit via applyChannelBytes()) or the edited whole
    // visual-order buffer (to commit via applyVisualOrderedBytes()).
    const juce::MemoryBlock& source = channel.has_value() ? workingImage->getChannelPlane(*channel)
                                                           : workingImage->getVisualOrderedPixelBytes();

    // Bytes outside the selection are provably untouched (see PROJECT.md's "Apply
    // scoping"), so start from a plain byte copy of the source and only pay the
    // float round-trip for the selected sub-range — on a large image with a small
    // selection, converting the whole buffer on every live-preview tick was pure
    // waste.
    if (! selection.isEmpty())
    {
        const int start = selection.getStart();
        const int length = selection.getLength();

        const auto mode = workingImage->getSampleMode();
        juce::MemoryBlock selectionBytes(static_cast<const char*>(source.getData()) + start, (size_t) length);
        auto selectedBuffer = SampleFormat::bytesToBuffer(selectionBytes, mode);
        PluginHost::processWholeBuffer(plugin, selectedBuffer, blockSize);
        SampleFormat::bufferToBytes(selectedBuffer, selectionBytes, mode);

        juce::MemoryBlock result(source);
        result.copyFrom(selectionBytes.getData(), start, (size_t) length);
        return result;
    }

    const auto mode = workingImage->getSampleMode();
    auto buffer = SampleFormat::bytesToBuffer(source, mode);
    PluginHost::processWholeBuffer(plugin, buffer, blockSize);

    juce::MemoryBlock result;
    result.setSize(source.getSize());
    SampleFormat::bufferToBytes(buffer, result, mode);
    return result;
}

void MainComponent::sampleModeChanged()
{
    if (workingImage == nullptr)
        return;

    const auto mode = sampleModeCombo.getSelectedId() == 2 ? SampleFormat::Mode::unipolar
                                                            : SampleFormat::Mode::bipolar;
    workingImage->setSampleMode(mode);
    waveformView.setSampleMode(mode);
    for (auto& view : channelWaveformViews)
        view.setSampleMode(mode);

    refreshLivePreview(); // re-renders both the image preview and the waveform(s)
}

void MainComponent::refreshLivePreview()
{
    if (workingImage == nullptr || currentPlugin == nullptr)
        return;

    const auto scope = getCurrentSelectionScope();

    if (scope.channel.has_value())
    {
        livePreviewChannel = scope.channel;
        livePreviewChannelPlaneBytes = computeProcessedPixelBytes(*currentPlugin, scope.range, scope.channel);
        livePreviewBytes = workingImage->previewWithChannelBytes(*scope.channel, livePreviewChannelPlaneBytes);

        imagePreview.setImage(workingImage->toJuceImageFromBytes(livePreviewBytes, *scope.channel, scope.range), false);

        auto& laneView = channelWaveformViews[(size_t) *scope.channel];

        if (! scope.range.isEmpty())
        {
            juce::MemoryBlock selectionBytes(static_cast<const char*>(livePreviewChannelPlaneBytes.getData()) + scope.range.getStart(),
                                              (size_t) scope.range.getLength());
            laneView.updateSampleRange(scope.range.getStart(), SampleFormat::bytesToBuffer(selectionBytes, workingImage->getSampleMode()));
        }
        else
        {
            laneView.setBuffer(SampleFormat::bytesToBuffer(livePreviewChannelPlaneBytes, workingImage->getSampleMode()), false);
        }

        return;
    }

    livePreviewChannel.reset();
    livePreviewVisualOrderBytes = computeProcessedPixelBytes(*currentPlugin, scope.range, std::nullopt);
    livePreviewBytes = workingImage->previewWithVisualOrderedBytes(livePreviewVisualOrderBytes);

    imagePreview.setImage(workingImage->toJuceImageFromBytes(livePreviewBytes, scope.range), false);

    if (! scope.range.isEmpty())
    {
        // Mirror the scoped conversion above: only the selected sub-range of
        // livePreviewVisualOrderBytes actually changed, so only that sub-range
        // of the waveform's buffer needs updating, instead of reconverting the
        // whole (possibly huge) buffer to float again just to refresh a small
        // part of it.
        juce::MemoryBlock selectionBytes(static_cast<const char*>(livePreviewVisualOrderBytes.getData()) + scope.range.getStart(),
                                          (size_t) scope.range.getLength());
        waveformView.updateSampleRange(scope.range.getStart(), SampleFormat::bytesToBuffer(selectionBytes, workingImage->getSampleMode()));
    }
    else
    {
        waveformView.setBuffer(SampleFormat::bytesToBuffer(livePreviewVisualOrderBytes, workingImage->getSampleMode()), false);
    }
}

void MainComponent::endLivePreviewSession(bool commitToWorkingImage)
{
    if (commitToWorkingImage && ! livePreviewBytes.isEmpty())
    {
        if (livePreviewChannel.has_value())
            workingImage->applyChannelBytes(*livePreviewChannel, livePreviewChannelPlaneBytes); // preserves the other 2 channels' caches
        else
            workingImage->applyVisualOrderedBytes(livePreviewVisualOrderBytes);
    }

    livePreviewBytes.reset();
    livePreviewChannelPlaneBytes.reset();
    livePreviewVisualOrderBytes.reset();
    livePreviewChannel.reset();
    leftColumn.setEditorPanel(nullptr);
    pluginEditorPanel.reset();
    pluginParamWatcher.attachTo(nullptr);
    updatePluginListEnablement();
    menuModel.menuItemsChanged();
}

void MainComponent::applyClicked()
{
    // A selection drag fires onSelectionChanged on every mouse-move frame, which
    // MainComponent coalesces into at most one recompute per event-loop turn (see
    // handleAsyncUpdate()) — but that means livePreviewBytes can momentarily lag
    // behind the true current selection between the last drag frame and the next
    // turn. Flush any pending recompute synchronously first, so the "isEmpty()"
    // safety net below can't be fooled by a *stale* (rather than merely absent)
    // livePreviewBytes into committing the wrong scope.
    handleUpdateNowIfNeeded();

    if (workingImage == nullptr)
    {
        setStatus("Load an image first.");
        return;
    }

    if (currentPlugin == nullptr)
    {
        setStatus("Load a plugin first.");
        return;
    }

    pushUndoState();

    const auto scope = getCurrentSelectionScope();
    const bool hadSelection = ! scope.range.isEmpty();

    if (livePreviewBytes.isEmpty()) // safety net: panel open but no refresh happened yet
    {
        if (scope.channel.has_value())
        {
            livePreviewChannel = scope.channel;
            livePreviewChannelPlaneBytes = computeProcessedPixelBytes(*currentPlugin, scope.range, scope.channel);
            livePreviewBytes = workingImage->previewWithChannelBytes(*scope.channel, livePreviewChannelPlaneBytes);
        }
        else
        {
            livePreviewVisualOrderBytes = computeProcessedPixelBytes(*currentPlugin, scope.range, std::nullopt);
            livePreviewBytes = workingImage->previewWithVisualOrderedBytes(livePreviewVisualOrderBytes);
        }
    }

    setStatus(hadSelection ? "Applied " + currentPlugin->getName() + " to selection."
                            : "Applied " + currentPlugin->getName() + " to the whole buffer.");

    endLivePreviewSession(true);

    updatePreview();
    updateWaveform();
}

void MainComponent::cancelEditorClicked()
{
    endLivePreviewSession(false); // false = discard, do not commit to workingImage->pixelBytes
    updatePreview();
    updateWaveform();
    setStatus("Cancelled — no changes applied.");
}
