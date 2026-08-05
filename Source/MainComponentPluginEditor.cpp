#include "MainComponent.h"
#include "PluginHost.h"
#include "SampleFormat.h"

ChainSlot* MainComponent::resolveChainSlot(const ChainPath& path)
{
    if (! juce::isPositiveAndBelow(path.topIndex, pluginChain.size()))
        return nullptr;

    auto& entry = pluginChain[(size_t) path.topIndex];

    if (! path.branch.has_value())
        return std::get_if<ChainSlot>(&entry); // nullptr if this entry is actually a ConditionalChainSlot

    auto* conditional = std::get_if<ConditionalChainSlot>(&entry);
    if (conditional == nullptr)
        return nullptr;

    auto& branchSlots = (*path.branch == Branch::a) ? conditional->branchA : conditional->branchB;
    return juce::isPositiveAndBelow(path.branchIndex, branchSlots.size()) ? &branchSlots[(size_t) path.branchIndex] : nullptr;
}

std::vector<ChainSlot>* MainComponent::resolveBranchContainer(int topIndex, Branch branch)
{
    if (! juce::isPositiveAndBelow(topIndex, pluginChain.size()))
        return nullptr;

    auto* conditional = std::get_if<ConditionalChainSlot>(&pluginChain[(size_t) topIndex]);
    if (conditional == nullptr)
        return nullptr;

    return branch == Branch::a ? &conditional->branchA : &conditional->branchB;
}

void MainComponent::addPluginToChain(int row)
{
    // The plugin list itself stays browsable/searchable without an image
    // loaded (see updatePluginListEnablement()'s listBrowsable) -- but there's
    // nothing a chain could process yet, so double-clicking a row here is a
    // deliberate no-op rather than starting a session with no image behind it.
    if (workingImage == nullptr)
        return;

    auto target = listModel.getLoadTarget(row);
    if (! target.has_value())
        return;

    const auto& desc = target->description;

    // Validate/instantiate before touching the chain at all -- a failed new
    // load must never affect any slot already in the chain.
    juce::String errorMessage;
    auto plugin = PluginHost::createInstance(scanner.getFormatManager(), desc, sampleRate, blockSize, errorMessage);

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

    // Consumed and cleared here regardless of outcome, so a later plain
    // top-level "+ Add Effect" click never accidentally reuses a stale target.
    const auto insertionTarget = pendingInsertionTarget;
    pendingInsertionTarget.reset();

    ChainPath newPath;

    if (insertionTarget.has_value() && insertionTarget->branch.has_value())
    {
        auto* container = resolveBranchContainer(insertionTarget->topIndex, *insertionTarget->branch);

        if (container == nullptr)
        {
            // The conditional slot this was aimed at no longer exists (removed
            // while the plugin browser was open) -- fall back to the top level
            // rather than silently dropping the plugin the user just picked.
            pluginChain.push_back(std::move(slot));
            newPath = { (int) pluginChain.size() - 1, std::nullopt, -1 };
        }
        else
        {
            container->push_back(std::move(slot));
            newPath = { insertionTarget->topIndex, insertionTarget->branch, (int) container->size() - 1 };
        }
    }
    else
    {
        pluginChain.push_back(std::move(slot));
        newPath = { (int) pluginChain.size() - 1, std::nullopt, -1 };
    }

    setStatus("Added to chain: " + desc.name);

    selectChainSlot(newPath);

    // Unconditional, even though selectChainSlot() normally refreshes the
    // rack itself on success: if it bailed out early (no editor UI, or the
    // header editor is open), the new slot would otherwise sit in the chain --
    // silently affecting every future refreshLivePreview()/Apply -- without
    // ever appearing in the rack UI. rebuild() is cheap and idempotent, so a
    // harmless redundant call on the common (success) path.
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

void MainComponent::addConditionalSlotToChain()
{
    // Same "nothing to process without an image" guard as addPluginToChain().
    if (workingImage == nullptr)
        return;

    pluginChain.push_back(ConditionalChainSlot {});

    setStatus("Added a condition to the chain.");

    refreshEffectChainPanel();
    refreshLivePreview();

    leftColumn.showEffectChainTab();
}

void MainComponent::selectChainSlot(ChainPath path)
{
    if (selectedChainSlot.has_value() && *selectedChainSlot == path)
        return;

    auto* slotPtr = resolveChainSlot(path);
    if (slotPtr == nullptr)
        return;

    if (headerEditorPanel != nullptr)
    {
        setStatus("Finish editing the header first (Apply or Cancel).");
        return;
    }

    auto& slot = *slotPtr;
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
    if (selectedChainSlot.has_value() && pluginEditorPanel != nullptr)
        if (auto* outgoing = resolveChainSlot(*selectedChainSlot))
            outgoing->ramps = pluginEditorPanel->getParameterRamps();

    // Defensive, matching the MeldaProduction dead-man's-pedal precedent
    // already in PROJECT.md: some third-party plugins do unsafe things
    // internally when their editor/UI is torn down concurrently with their
    // own audio thread. Cheap here -- at most one render's worth of latency,
    // the same cost endLivePreviewSession() already pays on every
    // Cancel/plugin swap.
    livePreviewWorker.waitUntilIdle();

    leftColumn.setEditorPanel(nullptr);
    pluginEditorWindow.reset();
    pluginEditorPanel.reset();

    selectedChainSlot = path;

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

    if (generalSettingsStore.isPluginWindowModeEnabled())
    {
        // No embedded panel -- LeftColumnPanel::resized() already fills the
        // whole left column with the effect chain rack whenever currentPanel
        // is null, so the outer left/right split below is left untouched.
        pluginEditorWindow = std::make_unique<PluginEditorWindow>(plugin.getName(), *pluginEditorPanel);
        pluginEditorWindow->onCloseButtonPressed = [this] { deselectChainSlot(); };
        pluginEditorWindow->setVisible(true);
        pluginEditorWindow->toFront(true);
    }
    else
    {
        leftColumn.setEditorPanel(pluginEditorPanel.get());

        // Re-seed the left/right column split so the left column starts out
        // sized to fit this slot's editor, capped at half the window width,
        // so ordinary window resizes afterward don't reset it. Fires on
        // every reselection, not just when the mounted plugin actually
        // changes -- see PROJECT.md's note on this being a foreseeable,
        // accepted side effect of the chain.
        outerLayout.setItemLayout(0, 200, -0.5, pluginEditorPanel->getPreferredWidth());
        resized();
    }

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
    if (! selectedChainSlot.has_value())
        return;

    // Same write-back selectChainSlot() does before switching to a different
    // slot -- deselecting doesn't discard anything, it just stops showing an
    // editor for this one; the slot stays in the chain exactly as it was.
    if (pluginEditorPanel != nullptr)
        if (auto* outgoing = resolveChainSlot(*selectedChainSlot))
            outgoing->ramps = pluginEditorPanel->getParameterRamps();

    // Same defensive wait selectChainSlot()/removeChainSlot() already use
    // before tearing down an editor -- see their comments for the
    // MeldaProduction precedent this guards against.
    livePreviewWorker.waitUntilIdle();

    leftColumn.setEditorPanel(nullptr);
    pluginEditorWindow.reset();
    pluginEditorPanel.reset();
    pluginParamWatcher.attachTo(nullptr);

    selectedChainSlot.reset();

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

void MainComponent::removeChainSlot(ChainPath path)
{
    const bool isTopLevel = ! path.branch.has_value();
    std::vector<ChainSlot>* container = isTopLevel ? nullptr : resolveBranchContainer(path.topIndex, *path.branch);

    if (isTopLevel)
    {
        if (! juce::isPositiveAndBelow(path.topIndex, pluginChain.size()))
            return;

        // An empty top-level chain is defined as "no session open" --
        // delegate entirely to the same teardown Cancel already uses rather
        // than duplicating its cache/spinner/menu bookkeeping here. Removing
        // the last slot in a *branch* doesn't hit this: an empty branch is
        // just a documented pass-through state, not a lost session.
        if (pluginChain.size() == 1)
        {
            endLivePreviewSession(false);
            return;
        }
    }
    else if (container == nullptr || ! juce::isPositiveAndBelow(path.branchIndex, container->size()))
    {
        return;
    }

    // True both for an exact path match (removing the selected top-level slot,
    // or the selected branch slot) AND for removing a top-level
    // ConditionalChainSlot that CONTAINS the currently-selected branch slot --
    // that erase below destroys the whole entry, including whichever branch
    // slot the editor/watcher are currently pointed at, so the teardown below
    // must run in that case too, not just on an exact path match.
    const bool removingSelected = selectedChainSlot.has_value()
        && (*selectedChainSlot == path || (isTopLevel && selectedChainSlot->topIndex == path.topIndex));

    // Must happen before tearing down the editor (if this is the selected
    // slot) or releasing this slot's plugin below: an in-flight worker pass
    // may still be inside that plugin's processBlock(), and per the
    // MeldaProduction precedent already in PROJECT.md, some third-party
    // plugins do unsafe things internally when their editor/UI is torn down
    // concurrently with their own audio thread.
    livePreviewWorker.waitUntilIdle();

    // The removed slot's live ramps (if it happened to be the selected one)
    // are about to be discarded along with the plugin itself -- unlike a
    // plain reselection, there's nothing to write back first.
    if (removingSelected)
    {
        leftColumn.setEditorPanel(nullptr);
        pluginEditorWindow.reset();
        pluginEditorPanel.reset();
        pluginParamWatcher.attachTo(nullptr);
    }

    if (isTopLevel)
    {
        auto& entry = pluginChain[(size_t) path.topIndex];

        if (auto* slot = std::get_if<ChainSlot>(&entry))
        {
            if (slot->plugin != nullptr)
                slot->plugin->releaseResources();
        }
        else
        {
            auto& conditional = std::get<ConditionalChainSlot>(entry);
            for (auto& s : conditional.branchA)
                if (s.plugin != nullptr)
                    s.plugin->releaseResources();
            for (auto& s : conditional.branchB)
                if (s.plugin != nullptr)
                    s.plugin->releaseResources();
        }

        pluginChain.erase(pluginChain.begin() + path.topIndex);
    }
    else
    {
        auto& slot = (*container)[(size_t) path.branchIndex];
        if (slot.plugin != nullptr)
            slot.plugin->releaseResources();
        container->erase(container->begin() + path.branchIndex);
    }

    if (removingSelected)
    {
        // Reset first so selectChainSlot() below doesn't mistake this for a
        // no-op reselection (it early-returns on an identical path).
        selectedChainSlot.reset();

        if (isTopLevel)
        {
            if (! pluginChain.empty())
                selectChainSlot({ juce::jmin(path.topIndex, (int) pluginChain.size() - 1), std::nullopt, -1 });
        }
        else if (container != nullptr && ! container->empty())
        {
            selectChainSlot({ path.topIndex, path.branch, juce::jmin(path.branchIndex, (int) container->size() - 1) });
        }

        // If neither branch resolves to a plugin to select (e.g. the branch is
        // now empty, or the fallback landed on a ConditionalChainSlot with no
        // single editor), selectedChainSlot is correctly left as nullopt --
        // "chain open, nothing selected" is an already-supported state.
    }
    else if (selectedChainSlot.has_value())
    {
        auto& sel = *selectedChainSlot;

        if (isTopLevel && sel.topIndex > path.topIndex)
            --sel.topIndex; // same logical slot, shifted down by the erase -- no editor swap needed
        else if (! isTopLevel && sel.topIndex == path.topIndex && sel.branch == path.branch
                 && sel.branchIndex > path.branchIndex)
            --sel.branchIndex;
        // Every other case: no change needed.
    }

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
    // relocates the ChainEntry/unique_ptr *value*, never the heap-allocated
    // AudioPluginInstance(s) it owns, and the vector never grows past its prior
    // size -- so any raw AudioPluginInstance* already captured in an in-flight
    // request stays valid regardless of where in pluginChain it now sits.
    auto moved = std::move(pluginChain[(size_t) from]);
    pluginChain.erase(pluginChain.begin() + from);
    pluginChain.insert(pluginChain.begin() + to, std::move(moved));

    // to is "index in the resulting array" -- same convention removeChainSlot's
    // own selectedChainSlot shift already uses. Every slot strictly between
    // from and to (exclusive/inclusive as appropriate) shifts by one to make
    // room for the moved slot; anything outside that range is untouched. Only
    // topIndex ever needs adjusting here -- a selection nested inside a
    // branch travels with its parent conditional slot's topIndex
    // automatically, since moving the whole conditional slot never touches
    // its internal branch indices.
    if (selectedChainSlot.has_value())
    {
        auto& sel = *selectedChainSlot;

        if (sel.topIndex == from)
            sel.topIndex = to;
        else if (from < to && sel.topIndex > from && sel.topIndex <= to)
            --sel.topIndex;
        else if (to < from && sel.topIndex >= to && sel.topIndex < from)
            ++sel.topIndex;
    }

    refreshLivePreview();
    refreshEffectChainPanel();
}

void MainComponent::moveBranchSlot(int conditionalIndex, Branch branch, int from, int to)
{
    auto* container = resolveBranchContainer(conditionalIndex, branch);
    if (container == nullptr)
        return;
    if (! juce::isPositiveAndBelow(from, container->size()) || ! juce::isPositiveAndBelow(to, container->size()))
        return;
    if (from == to)
        return;

    auto moved = std::move((*container)[(size_t) from]);
    container->erase(container->begin() + from);
    container->insert(container->begin() + to, std::move(moved));

    if (selectedChainSlot.has_value() && selectedChainSlot->topIndex == conditionalIndex
        && selectedChainSlot->branch == branch)
    {
        auto& idx = selectedChainSlot->branchIndex;

        if (idx == from)
            idx = to;
        else if (from < to && idx > from && idx <= to)
            --idx;
        else if (to < from && idx >= to && idx < from)
            ++idx;
    }

    refreshLivePreview();
    refreshEffectChainPanel();
}

void MainComponent::toggleChainSlotBypass(ChainPath path)
{
    if (! juce::isPositiveAndBelow(path.topIndex, pluginChain.size()))
        return;

    if (! path.branch.has_value())
    {
        // ChainSlot and ConditionalChainSlot both have a `bypassed` field of
        // the same name -- one generic visitor covers either alternative,
        // toggling the whole conditional slot (both branches skipped) when
        // this path names one.
        std::visit([](auto& entry) { entry.bypassed = ! entry.bypassed; }, pluginChain[(size_t) path.topIndex]);
    }
    else
    {
        auto* slot = resolveChainSlot(path);
        if (slot == nullptr)
            return;

        slot->bypassed = ! slot->bypassed;
    }

    refreshLivePreview();
    refreshEffectChainPanel();
}

void MainComponent::refreshEffectChainPanel()
{
    effectChainPanel.rebuild(pluginChain, selectedChainSlot);
}

void MainComponent::setConditionalSlotCondition(int topIndex, PixelCondition condition)
{
    if (! juce::isPositiveAndBelow(topIndex, pluginChain.size()))
        return;

    auto* conditional = std::get_if<ConditionalChainSlot>(&pluginChain[(size_t) topIndex]);
    if (conditional == nullptr)
        return;

    conditional->condition = condition;
    refreshLivePreview();
}

void MainComponent::setConditionalSlotMode(int topIndex, CompositingMode mode)
{
    if (! juce::isPositiveAndBelow(topIndex, pluginChain.size()))
        return;

    auto* conditional = std::get_if<ConditionalChainSlot>(&pluginChain[(size_t) topIndex]);
    if (conditional == nullptr)
        return;

    conditional->mode = mode;
    refreshLivePreview();
}

MainComponent::ChainSlotSnapshot MainComponent::captureOneSlotSnapshot(const ChainSlot& slot, const ChainPath& path) const
{
    ChainSlotSnapshot snapshot;
    snapshot.description = slot.plugin->getPluginDescription();
    slot.plugin->getStateInformation(snapshot.pluginState); // same call savePresetClicked() below already makes
    snapshot.bypassed = slot.bypassed;

    // The currently-selected slot's live ramps live in
    // pluginEditorPanel's ParameterAutomationPanel until a different
    // slot is selected (or deselected) writes them back into
    // ChainSlot::ramps -- same live-vs-frozen precedence already used
    // when building a LivePreviewWorker::Request, so the snapshot
    // matches whatever was actually last rendered.
    snapshot.ramps = (selectedChainSlot.has_value() && *selectedChainSlot == path && pluginEditorPanel != nullptr)
                          ? pluginEditorPanel->getParameterRamps()
                          : slot.ramps;

    return snapshot;
}

std::vector<MainComponent::ChainEntrySnapshot> MainComponent::captureChainSnapshot() const
{
    std::vector<ChainEntrySnapshot> result;

    for (int i = 0; i < (int) pluginChain.size(); ++i)
    {
        auto& entry = pluginChain[(size_t) i];

        if (auto* slot = std::get_if<ChainSlot>(&entry))
        {
            result.push_back(captureOneSlotSnapshot(*slot, { i, std::nullopt, -1 }));
        }
        else
        {
            auto& conditional = std::get<ConditionalChainSlot>(entry);

            ConditionalChainSlotSnapshot condSnapshot;
            condSnapshot.condition = conditional.condition;
            condSnapshot.mode = conditional.mode;
            condSnapshot.bypassed = conditional.bypassed;

            for (int j = 0; j < (int) conditional.branchA.size(); ++j)
                condSnapshot.branchA.push_back(captureOneSlotSnapshot(conditional.branchA[(size_t) j], { i, Branch::a, j }));
            for (int j = 0; j < (int) conditional.branchB.size(); ++j)
                condSnapshot.branchB.push_back(captureOneSlotSnapshot(conditional.branchB[(size_t) j], { i, Branch::b, j }));

            result.push_back(std::move(condSnapshot));
        }
    }

    return result;
}

std::optional<ChainSlot> MainComponent::instantiateSlotFromSnapshot(const ChainSlotSnapshot& entry, juce::StringArray& failedNames)
{
    juce::String errorMessage;
    auto plugin = PluginHost::createInstance(scanner.getFormatManager(), entry.description,
                                               sampleRate, blockSize, errorMessage);
    if (plugin == nullptr)
    {
        failedNames.add(entry.description.name);
        return std::nullopt;
    }

    plugin->setStateInformation(entry.pluginState.getData(), (int) entry.pluginState.getSize());

    ChainSlot slot;
    slot.plugin = std::move(plugin);
    slot.ramps = entry.ramps;
    slot.bypassed = entry.bypassed;
    return slot;
}

void MainComponent::restoreChainFromSnapshot(const std::vector<ChainEntrySnapshot>& snapshot)
{
    // Defensive -- always empty here in practice, since Undo/Redo are only
    // ever active while pluginChain.empty() already holds (see
    // getCommandInfo()'s undoCommand/redoCommand cases), but this mirrors
    // endLivePreviewSession()'s own teardown rather than assuming.
    for (auto& entry : pluginChain)
    {
        if (auto* slot = std::get_if<ChainSlot>(&entry))
        {
            if (slot->plugin != nullptr)
                slot->plugin->releaseResources();
        }
        else
        {
            auto& conditional = std::get<ConditionalChainSlot>(entry);
            for (auto& s : conditional.branchA)
                if (s.plugin != nullptr)
                    s.plugin->releaseResources();
            for (auto& s : conditional.branchB)
                if (s.plugin != nullptr)
                    s.plugin->releaseResources();
        }
    }
    pluginChain.clear();

    juce::StringArray failedNames;

    for (const auto& entrySnapshot : snapshot)
    {
        if (auto* slotSnapshot = std::get_if<ChainSlotSnapshot>(&entrySnapshot))
        {
            if (auto slot = instantiateSlotFromSnapshot(*slotSnapshot, failedNames))
                pluginChain.push_back(std::move(*slot));
        }
        else
        {
            auto& condSnapshot = std::get<ConditionalChainSlotSnapshot>(entrySnapshot);

            ConditionalChainSlot conditional;
            conditional.condition = condSnapshot.condition;
            conditional.mode = condSnapshot.mode;
            conditional.bypassed = condSnapshot.bypassed;

            for (auto& s : condSnapshot.branchA)
                if (auto slot = instantiateSlotFromSnapshot(s, failedNames))
                    conditional.branchA.push_back(std::move(*slot));
            for (auto& s : condSnapshot.branchB)
                if (auto slot = instantiateSlotFromSnapshot(s, failedNames))
                    conditional.branchB.push_back(std::move(*slot));

            pluginChain.push_back(std::move(conditional));
        }
    }

    selectedChainSlot.reset(); // matches deselectChainSlot()'s "chain open, nothing selected" state

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
    if (! selectedChainSlot.has_value())
        return;

    auto* slot = resolveChainSlot(*selectedChainSlot);
    if (slot == nullptr)
        return;

    auto& plugin = *slot->plugin;

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

    // Converts one plain plugin slot into its LivePreviewWorker-facing
    // request, sourcing ramps from whichever is actually live right now: the
    // selected slot's ramps are still live in its open ParameterAutomationPanel
    // -- read them from there, not from ChainSlot::ramps (only refreshed when
    // a *different* slot is selected, or when deselected entirely). Every
    // other slot's ramps (and this one's, if nothing is currently selected)
    // are exactly its own frozen ChainSlot::ramps.
    auto convertSlot = [this](const ChainSlot& slot, const ChainPath& path) -> LivePreviewWorker::ChainSlotRequest
    {
        LivePreviewWorker::ChainSlotRequest request;
        request.plugin = slot.plugin.get();
        request.bypassed = slot.bypassed;
        request.ramps = (selectedChainSlot.has_value() && *selectedChainSlot == path && pluginEditorPanel != nullptr)
                             ? pluginEditorPanel->getParameterRamps()
                             : slot.ramps;
        return request;
    };

    auto convertBranch = [&convertSlot](const std::vector<ChainSlot>& branchSlots, int topIndex, Branch which)
    {
        std::vector<LivePreviewWorker::ChainSlotRequest> result;
        result.reserve(branchSlots.size());
        for (int j = 0; j < (int) branchSlots.size(); ++j)
            result.push_back(convertSlot(branchSlots[(size_t) j], { topIndex, which, j }));
        return result;
    };

    LivePreviewWorker::Request request;
    request.chain.reserve(pluginChain.size());

    for (int i = 0; i < (int) pluginChain.size(); ++i)
    {
        auto& entry = pluginChain[(size_t) i];

        if (auto* slot = std::get_if<ChainSlot>(&entry))
        {
            request.chain.push_back(convertSlot(*slot, { i, std::nullopt, -1 }));
        }
        else
        {
            auto& conditional = std::get<ConditionalChainSlot>(entry);

            LivePreviewWorker::ConditionalChainSlotRequest condRequest;
            condRequest.condition = conditional.condition;
            condRequest.mode = conditional.mode;
            condRequest.bypassed = conditional.bypassed;
            condRequest.branchA = convertBranch(conditional.branchA, i, Branch::a);
            condRequest.branchB = convertBranch(conditional.branchB, i, Branch::b);

            request.chain.push_back(std::move(condRequest));
        }
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
    pluginEditorWindow.reset();
    pluginEditorPanel.reset();
    pluginParamWatcher.attachTo(nullptr);

    // Apply and Cancel both fully tear down the *whole* chain, not just the
    // currently-selected slot -- an empty chain is what "no session open"
    // means throughout this class (see pluginChain's own doc comment).
    for (auto& entry : pluginChain)
    {
        if (auto* slot = std::get_if<ChainSlot>(&entry))
        {
            if (slot->plugin != nullptr)
                slot->plugin->releaseResources();
        }
        else
        {
            auto& conditional = std::get<ConditionalChainSlot>(entry);
            for (auto& s : conditional.branchA)
                if (s.plugin != nullptr)
                    s.plugin->releaseResources();
            for (auto& s : conditional.branchB)
                if (s.plugin != nullptr)
                    s.plugin->releaseResources();
        }
    }
    pluginChain.clear();
    selectedChainSlot.reset();

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
    // handleAsyncUpdate()) - but that means a refreshLivePreview() submit can
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
    for (auto& entry : pluginChain)
    {
        if (auto* slot = std::get_if<ChainSlot>(&entry))
        {
            if (! slot->bypassed && slot->plugin != nullptr)
                activeNames.add(slot->plugin->getName());
        }
        else if (auto* conditional = std::get_if<ConditionalChainSlot>(&entry))
        {
            if (! conditional->bypassed)
                activeNames.add("Condition");
        }
    }

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
// "this was the last top-level slot" by delegating to endLivePreviewSession(false),
// so that case still ends the whole session.
void MainComponent::cancelEditorClicked()
{
    if (! selectedChainSlot.has_value())
        return;

    auto* slot = resolveChainSlot(*selectedChainSlot);
    if (slot == nullptr)
        return;

    const auto removedName = slot->plugin->getName();
    const auto path = *selectedChainSlot;
    removeChainSlot(path);
    setStatus("Removed " + removedName + " from the chain.");
}
