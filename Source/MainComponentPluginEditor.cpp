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
        livePreviewWorker.waitUntilIdle(); // must not race a background pass still using the plugin we're about to release
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

    pluginEditorPanel = std::make_unique<PluginEditorPanel>(std::unique_ptr<juce::AudioProcessorEditor>(editor), *currentPlugin,
    [this]
    {
        applyClicked();
    },
    [this]
    {
        cancelEditorClicked();
    },
    [this]
    {
        refreshLivePreview();
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

    // Rapid setImage() refreshes are about to start arriving (one per
    // delivered live-preview result) -- trade preview resample quality for
    // per-delivery message-thread time until the session ends.
    imagePreview.setFastResampling(true);

    // Reflects the plugin's default parameter state immediately, before any tweak,
    // so a live-preview result is populated even if Apply is clicked with zero changes.
    refreshLivePreview();
}

std::shared_ptr<const juce::MemoryBlock> MainComponent::getOrBuildLivePreviewSource(std::optional<RawImage::Channel> channel)
{
    if (channel.has_value())
    {
        auto& cached = cachedChannelSource[(size_t) *channel];
        if (cached == nullptr)
            cached = std::make_shared<const juce::MemoryBlock>(workingImage->getChannelPlane(*channel));
        return cached;
    }

    if (cachedWholeBufferSource == nullptr)
        cachedWholeBufferSource = std::make_shared<const juce::MemoryBlock>(workingImage->getVisualOrderedPixelBytes());

    return cachedWholeBufferSource;
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
    // pluginEditorPanel is required (getParameterRamps() below), not just the
    // plugin: a plugin that loaded but failed to open an editor leaves the
    // watcher attached with no panel, and a spontaneous audioProcessorChanged
    // (program/latency change) would land here.
    if (workingImage == nullptr || currentPlugin == nullptr || pluginEditorPanel == nullptr)
        return;

    const auto scope = getCurrentSelectionScope();

    // Immediate visual feedback for the selection lines themselves -- cheap,
    // stays on the message thread, independent of however long the heavy
    // recompute below takes to come back.
    updateHighlightOverlay(*workingImage, scope);

    LivePreviewWorker::Request request;
    request.plugin = currentPlugin.get();
    request.image = workingImage.get();
    request.source = getOrBuildLivePreviewSource(scope.channel);
    request.channel = scope.channel;
    request.selection = scope.range;
    request.sampleMode = workingImage->getSampleMode();
    request.ramps = pluginEditorPanel->getParameterRamps();
    request.sampleRate = sampleRate;
    request.blockSize = blockSize;
    request.epoch = livePreviewEpoch;

    livePreviewWorker.submit(std::move(request));
}

void MainComponent::applyLivePreviewResult(LivePreviewWorker::Result result)
{
    // Stale: the session this was computed for already ended (Apply/Cancel,
    // or a different plugin panel opened) before this background pass
    // finished -- see LivePreviewWorker::Request::epoch.
    if (pluginEditorPanel == nullptr || result.epoch != livePreviewEpoch)
        return;

    // Route on the result's own channel/selection, not a freshly-queried
    // getCurrentSelectionScope() -- the live selection may have moved again
    // since this particular request was submitted, and processedBytes only
    // ever corresponds to what was current at submit time (a newer request,
    // if any, is already in flight or queued and will supersede this).
    //
    // Everything below is now just a hand-off of already-finished data --
    // the actual render (image composition + waveform float conversion)
    // happened on the worker thread, in LivePreviewWorker::renderResult(),
    // right after compute. No RawImage method is called here anymore.
    if (result.channel.has_value())
    {
        livePreviewChannel = result.channel;
        livePreviewChannelPlaneBytes = std::move(result.processedBytes);

        imagePreview.setImage(std::move(result.renderedImage), false);

        auto& laneView = channelWaveformViews[(size_t) *result.channel];
        if (! result.selection.isEmpty())
            laneView.updateSampleRange(result.selection.getStart(), result.waveformSamples, std::move(result.waveformPeaks));
        else
            laneView.setBuffer(std::move(result.waveformSamples), false, std::move(result.waveformPeaks));
    }
    else
    {
        livePreviewChannel.reset();
        livePreviewVisualOrderBytes = std::move(result.processedBytes);

        imagePreview.setImage(std::move(result.renderedImage), false);

        if (! result.selection.isEmpty())
            waveformView.updateSampleRange(result.selection.getStart(), result.waveformSamples, std::move(result.waveformPeaks));
        else
            waveformView.setBuffer(std::move(result.waveformSamples), false, std::move(result.waveformPeaks));
    }
}

void MainComponent::endLivePreviewSession(bool commitToWorkingImage)
{
    // Both unconditional: a not-yet-started request is simply thrown away,
    // and bumping the epoch makes any already-in-flight background pass's
    // eventual result recognizably stale (see applyLivePreviewResult()) --
    // regardless of whether this session is ending via commit or discard.
    livePreviewWorker.discardPending();
    ++livePreviewEpoch;

    // Unconditional, both commit and discard: LivePreviewWorker::renderResult()
    // now reads workingImage directly (via Request::image) on the worker
    // thread, so nothing may mutate/reassign workingImage -- which becomes
    // possible again the instant this function returns (Load/Reset/Undo/Redo
    // re-enable via updatePluginListEnablement() below) -- while a render
    // could still be in flight. applyClicked() already calls waitUntilIdle()
    // before this, so this is a no-op there; Cancel/plugin-swap gain at most
    // one render's worth of latency on dismiss, not a per-frame cost.
    livePreviewWorker.waitUntilIdle();

    const bool haveResult = livePreviewChannel.has_value() ? ! livePreviewChannelPlaneBytes.isEmpty()
                                                            : ! livePreviewVisualOrderBytes.isEmpty();

    if (commitToWorkingImage && haveResult)
    {
        if (livePreviewChannel.has_value())
            workingImage->applyChannelBytes(*livePreviewChannel, livePreviewChannelPlaneBytes); // preserves the other 2 channels' caches
        else
            workingImage->applyVisualOrderedBytes(livePreviewVisualOrderBytes);
    }

    livePreviewChannelPlaneBytes.reset();
    livePreviewVisualOrderBytes.reset();
    livePreviewChannel.reset();

    // workingImage->pixelBytes may change once this session ends (a commit
    // above, or Undo/Reset/a header edit becoming possible again once the
    // panel closes) -- these cached snapshots must not outlive that.
    cachedWholeBufferSource.reset();
    for (auto& cached : cachedChannelSource)
        cached.reset();

    // Full-quality resampling comes back with the session's end -- the
    // Apply/Cancel paths' updatePreview() right after this re-renders the
    // preview at normal quality either way.
    imagePreview.setFastResampling(false);

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
    // handleAsyncUpdate()) — but that means a refreshLivePreview() submit can
    // momentarily lag behind the true current selection between the last drag
    // frame and the next turn. Flush that first.
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

    // Block until the background worker has delivered the true latest result
    // (openEditorClicked() guarantees at least one submit() already happened,
    // so a live-preview result is always populated by the time this returns) --
    // guarantees the committed bytes are byte-identical to what was just
    // previewed, the same guarantee the old synchronous path made.
    livePreviewWorker.waitUntilIdle();

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
    setStatus("Cancelled - no changes applied.");
}
