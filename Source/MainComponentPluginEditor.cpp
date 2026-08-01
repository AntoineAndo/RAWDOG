#include "MainComponent.h"
#include "PluginHost.h"
#include "SampleFormat.h"

void MainComponent::addPluginToChain(int row)
{
    // The plugin list itself stays browsable/searchable without an image
    // loaded (see updatePluginListEnablement()'s listBrowsable) -- but there's
    // nothing a chain could process yet, so double-clicking a row here is a
    // deliberate no-op rather than starting a session with no image behind it.
    if (workingImage == nullptr)
        return;

    auto target = listModel.getLoadTarget(row);
    if (! target.has_value() || target->description == nullptr)
        return;

    const auto* desc = target->description;

    // Validate/instantiate before touching pluginChain at all -- a failed new
    // load must never affect any slot already in the chain.
    juce::String errorMessage;
    auto plugin = PluginHost::createInstance(scanner.getFormatManager(), *desc, sampleRate, blockSize, errorMessage);

    if (plugin == nullptr)
    {
        setStatus("Failed to load plugin: " + errorMessage);
        return;
    }

    // A preset row's saved state, applied before the editor opens so the very
    // first refreshLivePreview() call below already reflects it, rather than
    // the plugin's freshly-instantiated defaults.
    if (target->presetState.has_value())
        plugin->setStateInformation(target->presetState->getData(), (int) target->presetState->getSize());

    ChainSlot slot;
    slot.plugin = std::move(plugin);
    pluginChain.push_back(std::move(slot));

    setStatus("Added to chain: " + desc->name);

    selectChainSlot((int) pluginChain.size() - 1);

    // Unconditional, even though selectChainSlot() normally refreshes the
    // rack itself on success: if it bailed out early (no editor UI, or the
    // header editor is open), the new slot would otherwise sit in
    // pluginChain -- silently affecting every future refreshLivePreview()/
    // Apply -- without ever appearing in the rack UI. rebuild() is cheap and
    // idempotent, so a harmless redundant call on the common (success) path.
    refreshEffectChainPanel();

    // The chain's shape just changed (a new slot appended) -- always needs a
    // fresh pass, even for the very first slot. selectChainSlot() itself
    // deliberately never calls this (see its own doc comment): a *pure*
    // reselection is a provable no-op recompute, but adding a slot never is.
    refreshLivePreview();

    // Land back on the rack so the user immediately sees the effect they
    // just picked land in the chain, rather than staying on the plugin
    // browser they came from.
    leftColumn.showEffectChainTab();
}

void MainComponent::selectChainSlot(int index)
{
    if (! juce::isPositiveAndBelow(index, pluginChain.size()) || index == selectedChainSlot)
        return;

    if (headerEditorPanel != nullptr)
    {
        setStatus("Finish editing the header first (Apply or Cancel).");
        return;
    }

    auto& slot = pluginChain[(size_t) index];
    auto& plugin = *slot.plugin;

    auto* editor = plugin.hasEditor() ? plugin.createEditorIfNeeded()
                                       : new juce::GenericAudioProcessorEditor(plugin);

    if (editor == nullptr)
    {
        setStatus("This plugin has no editor UI.");
        return;
    }

    // The outgoing slot's live ramps live in its ParameterAutomationPanel
    // while its editor is mounted -- write them back into ChainSlot::ramps
    // before tearing that panel down, so they aren't lost just because a
    // different slot was selected.
    if (selectedChainSlot >= 0 && pluginEditorPanel != nullptr)
        pluginChain[(size_t) selectedChainSlot].ramps = pluginEditorPanel->getParameterRamps();

    // Defensive, matching the MeldaProduction dead-man's-pedal precedent
    // already in PROJECT.md: some third-party plugins do unsafe things
    // internally when their editor/UI is torn down concurrently with their
    // own audio thread. Cheap here -- at most one render's worth of latency,
    // the same cost endLivePreviewSession() already pays on every
    // Cancel/plugin swap.
    livePreviewWorker.waitUntilIdle();

    leftColumn.setEditorPanel(nullptr);
    pluginEditorPanel.reset();

    selectedChainSlot = index;

    pluginEditorPanel = std::make_unique<PluginEditorPanel>(std::unique_ptr<juce::AudioProcessorEditor>(editor), plugin,
    [this]
    {
        deselectChainSlot();
    },
    [this]
    {
        refreshLivePreview();
    },
    [this]
    {
        savePresetClicked();
    },
    slot.ramps);

    leftColumn.setEditorPanel(pluginEditorPanel.get());

    // Re-seed the left/right column split so the left column starts out sized
    // to fit this slot's editor, capped at half the window width, so ordinary
    // window resizes afterward don't reset it. Fires on every reselection,
    // not just when the mounted plugin actually changes -- see PROJECT.md's
    // note on this being a foreseeable, accepted side effect of the chain.
    outerLayout.setItemLayout(0, 200, -0.5, pluginEditorPanel->getPreferredWidth());
    resized();

    pluginParamWatcher.attachTo(plugin);
    updatePluginListEnablement();
    menuModel.menuItemsChanged();
    refreshEffectChainPanel();

    // Rapid setImage() refreshes are about to start arriving (one per
    // delivered live-preview result) -- trade preview resample quality for
    // per-delivery message-thread time until the session ends.
    imagePreview.setFastResampling(true);

    // Deliberately no refreshLivePreview() call here: the chain's cumulative
    // processed output doesn't depend on which slot's editor happens to be
    // mounted, so a pure reselection is a provable no-op recompute. Every
    // caller that actually changes what should be previewed
    // (addPluginToChain/removeChainSlot/moveChainSlot/toggleChainSlotBypass)
    // calls refreshLivePreview() itself, explicitly, after this returns.
}

void MainComponent::deselectChainSlot()
{
    if (selectedChainSlot < 0)
        return;

    // Same write-back selectChainSlot() does before switching to a different
    // slot -- deselecting doesn't discard anything, it just stops showing an
    // editor for this one; the slot stays in the chain exactly as it was.
    if (pluginEditorPanel != nullptr)
        pluginChain[(size_t) selectedChainSlot].ramps = pluginEditorPanel->getParameterRamps();

    // Same defensive wait selectChainSlot()/removeChainSlot() already use
    // before tearing down an editor -- see their comments for the
    // MeldaProduction precedent this guards against.
    livePreviewWorker.waitUntilIdle();

    leftColumn.setEditorPanel(nullptr);
    pluginEditorPanel.reset();
    pluginParamWatcher.attachTo(nullptr);

    selectedChainSlot = -1;

    imagePreview.setFastResampling(false);

    updatePluginListEnablement();
    menuModel.menuItemsChanged();
    refreshEffectChainPanel();

    // Deliberately no refreshLivePreview() call here, for the same reason
    // selectChainSlot() doesn't have one: the chain's cumulative processed
    // output is unaffected by which slot (if any) is selected, since the
    // outgoing slot's ramps were just frozen to exactly what was already
    // being live-previewed.
}

void MainComponent::removeChainSlot(int index)
{
    if (! juce::isPositiveAndBelow(index, pluginChain.size()))
        return;

    // An empty chain is defined as "no session open" -- delegate entirely to
    // the same teardown Cancel already uses rather than duplicating its
    // cache/spinner/menu bookkeeping here.
    if (pluginChain.size() == 1)
    {
        endLivePreviewSession(false);
        return;
    }

    const bool removingSelected = (index == selectedChainSlot);

    // Must happen before tearing down the editor (if this is the selected
    // slot) or releasing this slot's plugin below: an in-flight worker pass
    // may still be inside that plugin's processBlock(), and per the
    // MeldaProduction precedent already in PROJECT.md, some third-party
    // plugins do unsafe things internally when their editor/UI is torn down
    // concurrently with their own audio thread -- same ordering
    // selectChainSlot()/endLivePreviewSession() already use, kept consistent
    // here rather than releasing/destroying first and waiting second.
    // Reordering (moveChainSlot) doesn't need this -- nothing is destroyed
    // there -- but removal does.
    livePreviewWorker.waitUntilIdle();

    // The removed slot's live ramps (if it happened to be the selected one)
    // are about to be discarded along with the plugin itself -- unlike a
    // plain reselection, there's nothing to write back first.
    if (removingSelected)
    {
        leftColumn.setEditorPanel(nullptr);
        pluginEditorPanel.reset();
        pluginParamWatcher.attachTo(nullptr);
    }

    if (pluginChain[(size_t) index].plugin != nullptr)
        pluginChain[(size_t) index].plugin->releaseResources();

    pluginChain.erase(pluginChain.begin() + index);

    if (removingSelected)
    {
        // Reset first so selectChainSlot() below doesn't mistake this for a
        // no-op reselection (it early-returns on index == selectedChainSlot).
        selectedChainSlot = -1;
        selectChainSlot(juce::jmin(index, (int) pluginChain.size() - 1));
    }
    else if (index < selectedChainSlot)
    {
        --selectedChainSlot; // same logical slot, shifted down by the erase -- no editor swap needed
    }
    // index > selectedChainSlot: no change needed.

    refreshLivePreview();
    refreshEffectChainPanel();
}

void MainComponent::moveChainSlot(int from, int to)
{
    if (! juce::isPositiveAndBelow(from, pluginChain.size()) || ! juce::isPositiveAndBelow(to, pluginChain.size()))
        return;
    if (from == to)
        return;

    // Erase+insert (not a plain swap) -- needed now that the rack's drag handle
    // can request an arbitrary target, not just an adjacent swap: moving slot 0
    // to the end of a 4-slot chain must shift 1/2/3 down by one, which a swap
    // would get wrong. Safe without flushing the live-preview worker: this only
    // relocates the ChainSlot/unique_ptr *value*, never the heap-allocated
    // AudioPluginInstance it owns, and the vector never grows past its prior
    // size -- so any raw AudioPluginInstance* already captured in an in-flight
    // request stays valid regardless of where in pluginChain it now sits.
    auto moved = std::move(pluginChain[(size_t) from]);
    pluginChain.erase(pluginChain.begin() + from);
    pluginChain.insert(pluginChain.begin() + to, std::move(moved));

    // to is "index in the resulting array" -- same convention removeChainSlot's
    // own selectedChainSlot shift already uses. Every slot strictly between
    // from and to (exclusive/inclusive as appropriate) shifts by one to make
    // room for the moved slot; anything outside that range is untouched.
    if (selectedChainSlot == from)
        selectedChainSlot = to;
    else if (from < to && selectedChainSlot > from && selectedChainSlot <= to)
        --selectedChainSlot;
    else if (to < from && selectedChainSlot >= to && selectedChainSlot < from)
        ++selectedChainSlot;

    refreshLivePreview();
    refreshEffectChainPanel();
}

void MainComponent::toggleChainSlotBypass(int index)
{
    if (! juce::isPositiveAndBelow(index, pluginChain.size()))
        return;

    pluginChain[(size_t) index].bypassed = ! pluginChain[(size_t) index].bypassed;

    refreshLivePreview();
    refreshEffectChainPanel();
}

void MainComponent::refreshEffectChainPanel()
{
    effectChainPanel.rebuild(pluginChain, selectedChainSlot);
}

std::vector<MainComponent::ChainSlotSnapshot> MainComponent::captureChainSnapshot() const
{
    std::vector<ChainSlotSnapshot> result;

    for (int i = 0; i < (int) pluginChain.size(); ++i)
    {
        const auto& slot = pluginChain[(size_t) i];

        ChainSlotSnapshot snapshot;
        snapshot.description = slot.plugin->getPluginDescription();
        slot.plugin->getStateInformation(snapshot.pluginState); // same call savePresetClicked() below already makes
        snapshot.bypassed = slot.bypassed;

        // The currently-selected slot's live ramps live in
        // pluginEditorPanel's ParameterAutomationPanel until a different
        // slot is selected (or deselected) writes them back into
        // ChainSlot::ramps -- same live-vs-frozen precedence already used
        // when building a LivePreviewWorker::Request below, so the
        // snapshot matches whatever was actually last rendered.
        snapshot.ramps = (i == selectedChainSlot && pluginEditorPanel != nullptr)
                              ? pluginEditorPanel->getParameterRamps()
                              : slot.ramps;

        result.push_back(std::move(snapshot));
    }

    return result;
}

void MainComponent::restoreChainFromSnapshot(const std::vector<ChainSlotSnapshot>& snapshot)
{
    // Defensive -- always empty here in practice, since Undo/Redo are only
    // ever active while pluginChain.empty() already holds (see
    // getCommandInfo()'s undoCommand/redoCommand cases), but this mirrors
    // endLivePreviewSession()'s own teardown rather than assuming.
    for (auto& slot : pluginChain)
        if (slot.plugin != nullptr)
            slot.plugin->releaseResources();
    pluginChain.clear();

    juce::StringArray failedNames;

    for (const auto& entry : snapshot)
    {
        juce::String errorMessage;
        auto plugin = PluginHost::createInstance(scanner.getFormatManager(), entry.description,
                                                   sampleRate, blockSize, errorMessage);
        if (plugin == nullptr)
        {
            failedNames.add(entry.description.name);
            continue;
        }

        plugin->setStateInformation(entry.pluginState.getData(), (int) entry.pluginState.getSize());

        ChainSlot slot;
        slot.plugin = std::move(plugin);
        slot.ramps = entry.ramps;
        slot.bypassed = entry.bypassed;
        pluginChain.push_back(std::move(slot));
    }

    selectedChainSlot = -1; // matches deselectChainSlot()'s "chain open, nothing selected" state

    updatePluginListEnablement();
    menuModel.menuItemsChanged();
    refreshEffectChainPanel();

    // Without this, a just-restored chain has no live-preview result yet --
    // hitting Apply immediately after would see haveResult == false in
    // endLivePreviewSession() and silently skip the commit.
    refreshLivePreview();

    if (! failedNames.isEmpty())
        setStatus("Restored chain, but couldn't reload: " + failedNames.joinIntoString(", "));
}

void MainComponent::savePresetClicked()
{
    if (! juce::isPositiveAndBelow(selectedChainSlot, pluginChain.size()))
        return;

    auto& plugin = *pluginChain[(size_t) selectedChainSlot].plugin;

    juce::MemoryBlock state;
    plugin.getStateInformation(state);
    pluginPresetsStore.addPreset(plugin.getPluginDescription().createIdentifierString(), state);

    listModel.notifyPresetsChanged();
    pluginListBox.updateContent();
    pluginListBox.repaint();
    setStatus("Preset saved.");
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
    // Deliberately NOT also gated on pluginEditorPanel != nullptr: a chain
    // session stays live (and must keep reprocessing on every selection/
    // sample-mode change) even with nothing currently selected for editing
    // -- see deselectChainSlot()'s doc comment.
    if (workingImage == nullptr || pluginChain.empty())
        return;

    const auto scope = getCurrentSelectionScope();

    // Immediate visual feedback for the selection lines themselves -- cheap,
    // stays on the message thread, independent of however long the heavy
    // recompute below takes to come back.
    updateHighlightOverlay(*workingImage, scope);

    LivePreviewWorker::Request request;
    request.chain.reserve(pluginChain.size());

    for (int i = 0; i < (int) pluginChain.size(); ++i)
    {
        auto& slot = pluginChain[(size_t) i];

        LivePreviewWorker::ChainSlotRequest slotRequest;
        slotRequest.plugin = slot.plugin.get();
        slotRequest.bypassed = slot.bypassed;

        // The selected slot's ramps are still live in its open
        // ParameterAutomationPanel -- read them from there, not from
        // ChainSlot::ramps (only refreshed when a *different* slot is
        // selected, or when deselected entirely -- see selectChainSlot()/
        // deselectChainSlot()). Every other slot's ramps (and this one's, if
        // nothing is currently selected -- pluginEditorPanel is then null)
        // are exactly its own frozen ChainSlot::ramps. Getting this backwards
        // means edits on the currently-open Automation tab would never reach
        // the live preview until you clicked away from that slot.
        slotRequest.ramps = (i == selectedChainSlot && pluginEditorPanel != nullptr) ? pluginEditorPanel->getParameterRamps() : slot.ramps;

        request.chain.push_back(std::move(slotRequest));
    }

    request.image = workingImage.get();
    request.source = getOrBuildLivePreviewSource(scope.channel);
    request.channel = scope.channel;
    request.selection = scope.range;
    request.sampleMode = workingImage->getSampleMode();
    request.sampleRate = sampleRate;
    request.blockSize = blockSize;
    request.epoch = livePreviewEpoch;

    livePreviewWorker.submit(std::move(request));
}

void MainComponent::applyLivePreviewResult(LivePreviewWorker::Result result)
{
    // Stale: the session this was computed for already ended (Apply/Cancel)
    // before this background pass finished -- see LivePreviewWorker::
    // Request::epoch. Gated on pluginChain.empty(), not pluginEditorPanel:
    // a result can legitimately arrive with nothing currently selected (the
    // chain session is still open, just deselected) and must still be
    // applied.
    if (pluginChain.empty() || result.epoch != livePreviewEpoch)
        return;

    // Route on the result's own channel/selection, not a freshly-queried
    // getCurrentSelectionScope() -- the live selection may have moved again
    // since this particular request was submitted, and processedBytes only
    // ever corresponds to what was current at submit time (a newer request,
    // if any, is already in flight or queued and will supersede this).
    //
    // Everything below is just a hand-off of already-finished data --
    // the actual render (image composition + waveform float conversion)
    // happened on the worker thread, in LivePreviewWorker::renderResult(),
    // right after compute.
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
    // reads workingImage directly (via Request::image) on the worker
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

    // Apply and Cancel both fully tear down the *whole* chain, not just the
    // currently-selected slot -- an empty chain is what "no session open"
    // means throughout this class (see pluginChain's own doc comment).
    for (auto& slot : pluginChain)
        if (slot.plugin != nullptr)
            slot.plugin->releaseResources();
    pluginChain.clear();
    selectedChainSlot = -1;

    updatePluginListEnablement();
    menuModel.menuItemsChanged();
    refreshEffectChainPanel();

    // Whatever was last shown (a live-preview result, possibly just discarded)
    // is stale the instant this session ends -- re-render from workingImage's
    // actual current committed bytes so the display always matches reality,
    // whether this ended via commit (Apply) or discard (Cancel/removing the
    // last slot). Centralized here so removeChainSlot()'s delegation to this
    // function (when the chain empties out) can't accidentally skip it.
    updatePreview();
    updateWaveform();
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

    if (pluginChain.empty())
    {
        setStatus("Load a plugin first.");
        return;
    }

    // Block until the background worker has delivered the true latest result
    // (addPluginToChain() guarantees at least one submit() already happened,
    // so a live-preview result is always populated by the time this returns) --
    // guarantees the committed bytes are byte-identical to what was just
    // previewed. Must run BEFORE pushUndoState(): that also snapshots each
    // slot's plugin state via captureChainSnapshot(), and calling getStateInformation()/
    // getPluginDescription() from the message thread while the worker thread
    // might still be inside that same plugin's processBlock() is exactly the
    // hazard every other chain call site (selectChainSlot/removeChainSlot/
    // deselectChainSlot) already serializes against by waiting first.
    livePreviewWorker.waitUntilIdle();

    pushUndoState();

    const auto scope = getCurrentSelectionScope();
    const bool hadSelection = ! scope.range.isEmpty();

    juce::StringArray activeNames;
    for (auto& slot : pluginChain)
        if (! slot.bypassed && slot.plugin != nullptr)
            activeNames.add(slot.plugin->getName());

    const juce::String chainDescription = activeNames.isEmpty() ? juce::String("(every effect bypassed)")
                                                                  : activeNames.joinIntoString(" -> ");

    setStatus(hadSelection ? "Applied " + chainDescription + " to selection."
                            : "Applied " + chainDescription + " to the whole buffer.");

    // endLivePreviewSession() re-renders the preview itself now (see its own
    // tail) -- no need to call updatePreview()/updateWaveform() here too.
    endLivePreviewSession(true);
}

// Wired to PluginEditorPanel's Cancel button: removes the currently selected
// slot from the chain (as if it had never been added), leaving every other
// slot untouched and still live. removeChainSlot() itself already handles
// "this was the last slot" by delegating to endLivePreviewSession(false), so
// that case still ends the whole session.
void MainComponent::cancelEditorClicked()
{
    if (selectedChainSlot < 0)
        return;

    const auto removedName = pluginChain[(size_t) selectedChainSlot].plugin->getName();
    removeChainSlot(selectedChainSlot);
    setStatus("Removed " + removedName + " from the chain.");
}
