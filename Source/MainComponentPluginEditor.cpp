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
                                                              const juce::Range<int>& selection)
{
    plugin.reset(); // clean DSP state before every independent reprocessing pass, so repeated
                     // live-preview passes against the same source bytes stay deterministic for
                     // stateful plugins (filters, reverbs, etc).

    // Bytes outside the selection are provably untouched (see PROJECT.md's "Apply
    // scoping"), so start from a plain byte copy of the source and only pay the
    // float round-trip for the selected sub-range — on a large image with a small
    // selection, converting the whole buffer on every live-preview tick was pure
    // waste.
    if (! selection.isEmpty())
    {
        const int start = selection.getStart();
        const int length = selection.getLength();

        juce::MemoryBlock selectionBytes(static_cast<const char*>(workingImage->pixelBytes.getData()) + start,
                                          (size_t) length);
        auto selectedBuffer = SampleFormat::bytesToBuffer(selectionBytes);
        PluginHost::processWholeBuffer(plugin, selectedBuffer, blockSize);
        SampleFormat::bufferToBytes(selectedBuffer, selectionBytes);

        juce::MemoryBlock result(workingImage->pixelBytes);
        result.copyFrom(selectionBytes.getData(), start, (size_t) length);
        return result;
    }

    auto buffer = SampleFormat::bytesToBuffer(workingImage->pixelBytes);
    PluginHost::processWholeBuffer(plugin, buffer, blockSize);

    juce::MemoryBlock result;
    result.setSize(workingImage->pixelBytes.getSize());
    SampleFormat::bufferToBytes(buffer, result);
    return result;
}

void MainComponent::refreshLivePreview()
{
    if (workingImage == nullptr || currentPlugin == nullptr)
        return;

    const auto selection = waveformView.getSelectionSampleRange();
    livePreviewBytes = computeProcessedPixelBytes(*currentPlugin, selection);

    imagePreview.setImage(workingImage->toJuceImageFromBytes(livePreviewBytes, selection), false);

    if (! selection.isEmpty())
    {
        // Mirror the scoped conversion above: only the selected sub-range of
        // livePreviewBytes actually changed, so only that sub-range of the
        // waveform's buffer needs updating, instead of reconverting the whole
        // (possibly huge) buffer to float again just to refresh a small part of it.
        juce::MemoryBlock selectionBytes(static_cast<const char*>(livePreviewBytes.getData()) + selection.getStart(),
                                          (size_t) selection.getLength());
        waveformView.updateSampleRange(selection.getStart(), SampleFormat::bytesToBuffer(selectionBytes));
    }
    else
    {
        waveformView.setBuffer(SampleFormat::bytesToBuffer(livePreviewBytes), false);
    }
}

void MainComponent::endLivePreviewSession(bool commitToWorkingImage)
{
    if (commitToWorkingImage && ! livePreviewBytes.isEmpty())
        workingImage->pixelBytes = livePreviewBytes;

    livePreviewBytes.reset();
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

    const bool hadSelection = ! waveformView.getSelectionSampleRange().isEmpty();

    if (livePreviewBytes.isEmpty()) // safety net: panel open but no refresh happened yet
        livePreviewBytes = computeProcessedPixelBytes(*currentPlugin, waveformView.getSelectionSampleRange());

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
