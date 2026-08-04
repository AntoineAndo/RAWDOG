#include "MainComponent.h"
#include "PluginHost.h"
#include "RawdogLookAndFeel.h"
#include "SampleFormat.h"
#if JUCE_MAC
 #include "MacAppearance.h"
#endif

MainComponent::MainComponent()
{
    addAndMakeVisible(leftColumn);
    addAndMakeVisible(rightColumn);
    addAndMakeVisible(outerResizerBar);

    // Outer split (left column vs right column): seeded once here, matching
    // today's initial 260px left-column width; the resizer bar re-drives it from
    // then on without ever re-seeding on ordinary resizes.
    outerLayout.setItemLayout(0, 200, -0.85, 260); // left column: min 200px, max 85%, preferred 260px
    outerLayout.setItemLayout(1, 8, 8, 8);          // resizer bar: fixed 8px
    outerLayout.setItemLayout(2, 250, -1.0, -1.0);  // right column: min 250px, fills remainder

    // Must run before building extraAppleMenu below: PopupMenu::addCommandItem
    // resolves openSettingsCommand's info immediately, so commandManager needs
    // it registered first.
    commandManager.registerAllCommandsForTarget(this);
    commandManager.setFirstCommandTarget(this);
    addKeyListener(commandManager.getKeyMappings());
    setWantsKeyboardFocus(true);

    // Adds "Settings..." to the native "RAWDOG" app menu itself (the one with
    // auto-generated About/Services/Hide/Quit), not the File/Edit menu bar -
    // extraAppleMenu is deep-copied by setMacMainMenu() internally, so it's
    // safe as a local that goes out of scope right after this call.
    juce::PopupMenu extraAppleMenu;
    extraAppleMenu.addCommandItem(&commandManager, aboutCommand);
    extraAppleMenu.addSeparator();
    extraAppleMenu.addCommandItem(&commandManager, openSettingsCommand);
    juce::MenuBarModel::setMacMainMenu(&menuModel, &extraAppleMenu);

    imagePreview.onClickWithNoImage = [this] { loadImageClicked(); };
    imagePreview.onClick = [this] { clearCurrentSelection(); };

    imagePreview.onHighlightRegionDragStart = [this]
    {
        // Same "don't push an undo entry mid-live-preview-session" rationale
        // as waveformView.onBeforeSelectionChange below. Keyed on the chain
        // itself, not on whether a specific slot's editor is mounted --
        // deselecting a slot (OK button) doesn't end the session, so a
        // selection change with nothing selected still shouldn't push its
        // own undo entry.
        if (pluginChain.empty())
            pushUndoState();
    };

    imagePreview.onHighlightRegionChanged = [this](juce::Range<int> rowRange)
    {
        if (workingImage == nullptr)
            return;

        const auto scope = getCurrentSelectionScope();
        const auto newRange = scope.channel.has_value()
            ? workingImage->rowRangeToChannelHighlightSampleRange(rowRange)
            : workingImage->rowRangeToHighlightByteRange(rowRange);

        if (splitModeToggle.getToggleState() && activeSelectionChannel.has_value())
            channelWaveformViews[(size_t) *activeSelectionChannel].setSelectionSampleRange(newRange);
        else
            waveformView.setSelectionSampleRange(newRange);
    };

    horizontalScrollBar.addListener(this);
    waveformView.onViewChanged = [this] { syncScrollBarToView(); };
    waveformView.onSelectionChanged = [this] { triggerAsyncUpdate(); };
    waveformView.onBeforeSelectionChange = [this]
    {
        // While a chain session is open, a new selection drag only rescopes
        // the uncommitted preview (see onSelectionChanged above) - nothing has
        // been committed yet, so this must not push an undo entry.
        if (pluginChain.empty())
            pushUndoState();
    };

    splitModeToggle.setClickingTogglesState(true);
    splitModeToggle.onClick = [this] { setSplitMode(splitModeToggle.getToggleState()); };

    sampleModeCombo.addItem("Bipolar", 1);
    sampleModeCombo.addItem("Unipolar", 2);
    sampleModeCombo.setSelectedId(1, juce::dontSendNotification);
    sampleModeCombo.onChange = [this] { sampleModeChanged(); };

    // Always visible, matching the design mockup, which keeps Sample Mode
    // visible-but-greyed rather than hidden outright. updatePluginListEnablement()
    // below still gates whether it's actually *interactive* on chainSessionOpen.

    for (int c = 0; c < 4; ++c)
    {
        auto* view = &channelWaveformViews[(size_t) c];
        view->onViewChanged = [this] { syncScrollBarToView(); };
        view->onSelectionChanged = [this] { triggerAsyncUpdate(); };
        view->onBeforeSelectionChange = [this, c]
        {
            activeSelectionChannel = (RawImage::Channel) c;

            // Only one lane has an active selection at a time - starting a new
            // one elsewhere clears the others. setSelectionSampleRange({}) (not
            // clearSelection()) deliberately, since clearSelection() itself fires
            // onBeforeSelectionChange and would recurse into pushUndoState() for
            // the "losing" lane.
            for (int other = 0; other < 4; ++other)
                if (other != c)
                    channelWaveformViews[(size_t) other].setSelectionSampleRange({});

            if (pluginChain.empty())
                pushUndoState();
        };
    }

    // Hidden until refreshChannelWaveforms() shows it for a loaded PNG that
    // actually has an alpha channel - see hasAlphaChannel().
    channelWaveformViews[3].setVisible(false);

    livePreviewWorker.onResultReady = [this](LivePreviewWorker::Result result) { applyLivePreviewResult(std::move(result)); };
    previewBusySpinner.isBusy = [this] { return livePreviewWorker.isBusy() || imageLoadInProgress; };

    pluginParamWatcher.onPluginParametersChanged = [this] { refreshLivePreview(); };
    pluginParamWatcher.onParameterValueChanged = [this](const juce::String& parameterName, const juce::String& valueText)
    {
        if (selectedChainSlot >= 0)
            setStatus(pluginChain[(size_t) selectedChainSlot].plugin->getName() + " - " + parameterName + ": " + valueText);
    };

    effectChainPanel.onSelectSlot = [this](int index) { selectChainSlot(index); };
    effectChainPanel.onRemoveSlot = [this](int index) { removeChainSlot(index); };
    effectChainPanel.onReorderSlot = [this](int from, int to) { moveChainSlot(from, to); };
    effectChainPanel.onToggleBypass = [this](int index) { toggleChainSlotBypass(index); };
    effectChainPanel.onAddEffectClicked = [this] { leftColumn.showPluginsTab(); };
    effectChainPanel.onApplyClicked = [this] { applyClicked(); };

    // Every other rebuild() call happens after a chain mutation -- this is
    // the one-time "initial empty state" case, needed so Content actually
    // constructs its Add Effect row/connectors (built fresh inside rebuild(),
    // never in Content's own constructor) before the very first resized()
    // pass computes the rack's preferred scroll height against them.
    refreshEffectChainPanel();

    pluginListBox.setModel(&listModel);
    pluginListBox.setColour(juce::ListBox::backgroundColourId, juce::Colours::darkgrey.darker());
    listModel.onDoubleClick = [this](int row) { addPluginToChain(row); };
    listModel.onFavouritesChanged = [this] { pluginListBox.updateContent(); pluginListBox.repaint(); };
    listModel.onPluginHidden = [this] { pluginListBox.updateContent(); pluginListBox.repaint(); };

    // The model has no reference back to pluginListBox (it deliberately doesn't
    // own UI components, same as every other filter/mutation callback here), so
    // the deselectAllRows() call the plan calls for lives here rather than in the
    // model: ListBox selection tracks by row *number*, not item identity, so an
    // expand/collapse toggle that shifts row numbers around could otherwise leave
    // the highlight appearing to jump to an unrelated row.
    listModel.onGroupExpansionChanged = [this]
    {
        pluginListBox.deselectAllRows();
        pluginListBox.updateContent();
        pluginListBox.repaint();
    };

    leftColumn.onTabChanged = [this](int tabIndex)
    {
        listModel.setShowFavouritesOnly(tabIndex == 1);
        listModel.setGroupByVendor(tabIndex == 2);
        pluginListBox.updateContent();
        pluginListBox.repaint();
    };

    leftColumn.onSearchChanged = [this](const juce::String& query)
    {
        listModel.setSearchQuery(query);
        pluginListBox.updateContent();
        pluginListBox.repaint();
    };

    setStatus("Ready - open an image to begin.");
    updatePluginListEnablement();

    setSize(900, 700);

    if (scanner.loadCachedPluginList())
    {
        listModel.refresh();
        pluginListBox.updateContent();
        pluginListBox.repaint();
        setStatus("Ready - open an image to begin. ("
                    + juce::String(scanner.getKnownPluginList().getNumTypes()) + " cached plugin(s) loaded.)");
    }
    else
    {
        // First launch on this machine - nothing cached yet, so an initial
        // scan is unavoidable. Every subsequent launch loads the cache above
        // instead.
        refreshPluginList();
    }
}

MainComponent::~MainComponent()
{
    cancelPendingUpdate();

    juce::MenuBarModel::setMacMainMenu(nullptr);

    // Disconnect the spinner's poll before teardown proceeds: destruction
    // order destroys livePreviewWorker before previewBusySpinner, and if a
    // third-party plugin's destructor pumps the run loop (some AU/VST3s do),
    // a pending spinner tick could otherwise dispatch into the dead worker.
    // timerCallback() null-checks isBusy, so clearing it makes that safe.
    previewBusySpinner.isBusy = nullptr;

    pluginParamWatcher.attachTo(nullptr);

    // Clear leftColumn's non-owning pointer before pluginEditorPanel is destroyed
    // below (member destruction order); redundant since Component's own
    // destructor already self-detaches from its parent's child list, but
    // harmless to state explicitly.
    leftColumn.setEditorPanel(nullptr);

    // Neutralize any queued/future load completion before anything below is
    // torn down -- see imageLoadAliveToken's doc comment for why SafePointer
    // alone doesn't cover the run-loop-pumping-plugin-destructor edge.
    imageLoadAliveToken.reset();

    // A still-running image-load job holds no reference to any member (it
    // captures the file by value and reaches back only via a token-and-
    // SafePointer-guarded callAsync) -- this wait only keeps ThreadPool's own
    // destructor from force-killing a thread mid-ImageIO-decode. Generous
    // timeout: RAW decodes legitimately take seconds.
    imageLoaderPool.removeAllJobs(true, 15000);

    // Full shutdown, not just idle-draining: must happen before releasing any
    // chain slot's plugin below, since the worker may still be inside
    // plugin.processBlock() on one of them. stopThread() lets any in-flight
    // pass finish naturally first (run()'s loop only checks
    // threadShouldExit() between jobs) before forcing the issue.
    livePreviewWorker.shutdown(5000);

    for (auto& slot : pluginChain)
        if (slot.plugin != nullptr)
            slot.plugin->releaseResources();
}

void MainComponent::setStatus(const juce::String& text)
{
    statusLabel.setText(text, juce::dontSendNotification);
}

void MainComponent::refreshPluginList()
{
    if (scanner.isScanning())
        return;

    setStatus("Scanning for plugins...");

    // "Rescan Plugins" must gray out immediately (native mac menu enablement
    // otherwise goes stale until menuItemsChanged() is called explicitly -
    // see PROJECT.md) so a second click can't race the background scan.
    menuModel.menuItemsChanged();

    scanner.scanAll(pluginDirectoriesStore.getAsSearchPath(), [this]
    {
        // Seed a default-disabled state the first time a duplicate AU is
        // ever seen, without clobbering a user's later manual re-enable -
        // see PluginEnablementStore::hasDefaultBeenAssigned()'s doc comment.
        for (const auto& id : scanner.getLastDuplicateAudioUnitIdentifiers())
        {
            if (! pluginEnablementStore.hasDefaultBeenAssigned(id))
            {
                pluginEnablementStore.setEnabled(id, false);
                pluginEnablementStore.markDefaultAssigned(id);
            }
        }

        listModel.refresh();
        pluginListBox.updateContent();
        pluginListBox.repaint();
        scanner.saveCachedPluginListToDisk();

        auto status = "Found " + juce::String(scanner.getKnownPluginList().getNumTypes()) + " plugin(s).";

        // Plugins that crashed a previous scan are skipped rather than
        // re-probed (see PluginScanner's dead man's pedal file) - surface
        // that so a rescan showing fewer plugins than expected isn't
        // mistaken for a scanning bug.
        const auto& skippedCrashers = scanner.getLastSkippedCrashers();

        if (! skippedCrashers.isEmpty())
            status << " Skipped " << skippedCrashers.size() << " plugin(s) that crashed a previous scan.";

        setStatus(status);
        menuModel.menuItemsChanged();
    });
}

void MainComponent::openSettingsClicked()
{
    if (settingsWindow == nullptr)
    {
        settingsWindow = std::make_unique<SettingsWindow>(
            pluginDirectoriesStore, pluginEnablementStore, appearanceSettingsStore, generalSettingsStore, scanner,
            [this] { refreshPluginList(); },
            [this] { listModel.refresh(); pluginListBox.updateContent(); pluginListBox.repaint(); },
            []
            {
                RawdogLookAndFeel::refreshAllWindows();
#if JUCE_MAC
                setNativeAppearanceDark(RawdogLookAndFeel::Palette::isDarkModeEnabled());
#endif
            });
    }

    settingsWindow->setVisible(true);
    settingsWindow->toFront(true);
}

void MainComponent::aboutClicked()
{
    if (aboutWindow == nullptr)
        aboutWindow = std::make_unique<AboutWindow>();

    aboutWindow->setVisible(true);
    aboutWindow->toFront(true);
}

void MainComponent::resetClicked()
{
    if (originalImage == nullptr)
        return;

    pushUndoState();

    workingImage = std::make_unique<RawImage>(*originalImage);
    updateWaveform(true);
    updatePreview(true);
    setStatus("Reset to original image.");
}

void MainComponent::undoClicked()
{
    if (undoStack.empty() || workingImage == nullptr)
        return;

    // Trivially empty in practice (Undo is only ever active while
    // pluginChain.empty() already holds -- see getCommandInfo()'s
    // undoCommand case), but captured uniformly via the same helper
    // pushUndoState() uses rather than special-casing "chain is always
    // empty here."
    const auto currentScope = getCurrentSelectionScope();
    redoStack.push_back({ workingImage->headerBytes, workingImage->pixelBytes, currentScope.channel,
                           currentScope.range, captureChainSnapshot() });
    const auto entry = undoStack.back();
    undoStack.pop_back();

    workingImage->restoreSnapshot(entry.headerBytes, entry.pixelBytes);
    updateWaveform();
    restoreSelectionScope(entry.selectionChannel, entry.selection);
    updatePreview();
    restoreChainFromSnapshot(entry.chain); // no-op (empty) unless this entry was pushed by applyClicked()
    menuModel.menuItemsChanged();
    setStatus("Undid last action.");
}

void MainComponent::redoClicked()
{
    if (redoStack.empty() || workingImage == nullptr)
        return;

    const auto currentScope = getCurrentSelectionScope();
    undoStack.push_back({ workingImage->headerBytes, workingImage->pixelBytes, currentScope.channel,
                           currentScope.range, captureChainSnapshot() });
    const auto entry = redoStack.back();
    redoStack.pop_back();

    workingImage->restoreSnapshot(entry.headerBytes, entry.pixelBytes);
    updateWaveform();
    restoreSelectionScope(entry.selectionChannel, entry.selection);
    updatePreview();
    restoreChainFromSnapshot(entry.chain);
    menuModel.menuItemsChanged();
    setStatus("Redid last action.");
}

void MainComponent::pushUndoState()
{
    if (workingImage != nullptr)
    {
        const auto scope = getCurrentSelectionScope();
        undoStack.push_back({ workingImage->headerBytes, workingImage->pixelBytes, scope.channel, scope.range,
                               captureChainSnapshot() });
    }

    redoStack.clear();
    menuModel.menuItemsChanged();
}

MainComponent::SelectionScope MainComponent::getCurrentSelectionScope() const
{
    if (splitModeToggle.getToggleState() && activeSelectionChannel.has_value())
        return { activeSelectionChannel, channelWaveformViews[(size_t) *activeSelectionChannel].getSelectionSampleRange() };

    if (splitModeToggle.getToggleState())
        return { std::nullopt, {} }; // split mode on, but no lane has an active selection yet

    return { std::nullopt, waveformView.getSelectionSampleRange() };
}

void MainComponent::restoreSelectionScope(std::optional<RawImage::Channel> channel, juce::Range<int> range)
{
    waveformView.setSelectionSampleRange({});

    activeSelectionChannel = channel;
    setSplitMode(channel.has_value());

    if (channel.has_value())
        channelWaveformViews[(size_t) *channel].setSelectionSampleRange(range);
    else
        waveformView.setSelectionSampleRange(range);
}

void MainComponent::clearCurrentSelection()
{
    if (workingImage == nullptr)
        return;

    if (splitModeToggle.getToggleState() && activeSelectionChannel.has_value())
        channelWaveformViews[(size_t) *activeSelectionChannel].clearSelection();
    else
        waveformView.clearSelection();
}

void MainComponent::setSplitMode(bool enabled)
{
    splitModeToggle.setToggleState(enabled, juce::dontSendNotification);

    if (enabled)
    {
        // Must run before updateSplitVisibility() below -- it's what decides
        // whether the alpha lane is visible for this image, and the split
        // panel's layout needs to already reflect that when it (re)lays out.
        refreshChannelWaveforms(true);
    }
    else
    {
        activeSelectionChannel.reset();
        for (auto& view : channelWaveformViews)
            view.setSelectionSampleRange({});
    }

    rightColumn.updateSplitVisibility();
    updatePreview();
}

void MainComponent::refreshChannelWaveforms(bool resetView)
{
    if (workingImage == nullptr || ! workingImage->hasChannelPlanes())
        return;

    const bool hasAlpha = workingImage->hasAlphaChannel();
    channelWaveformViews[3].setVisible(hasAlpha);

    const RawImage::Channel channels[4] = { RawImage::Channel::red, RawImage::Channel::green,
                                             RawImage::Channel::blue, RawImage::Channel::alpha };
    const int numChannels = hasAlpha ? 4 : 3;

    for (int c = 0; c < numChannels; ++c)
        channelWaveformViews[(size_t) c].setBuffer(SampleFormat::bytesToBuffer(workingImage->getChannelPlane(channels[c]), workingImage->getSampleMode()), resetView);
}

WaveformView& MainComponent::primaryWaveformView()
{
    return splitModeToggle.getToggleState() ? channelWaveformViews[0] : waveformView;
}

juce::ApplicationCommandTarget* MainComponent::getNextCommandTarget()
{
    return nullptr;
}

void MainComponent::getAllCommands(juce::Array<juce::CommandID>& commands)
{
    commands.add(undoCommand);
    commands.add(redoCommand);
    commands.add(cancelEditorCommand);
    commands.add(loadImageCommand);
    commands.add(resetCommand);
    commands.add(exportImageCommand);
    commands.add(openSettingsCommand);
    commands.add(aboutCommand);
}

void MainComponent::getCommandInfo(juce::CommandID commandID, juce::ApplicationCommandInfo& result)
{
    switch (commandID)
    {
        case undoCommand:
            result.setInfo("Undo", "Undo the last action", "Edit", 0);
            result.addDefaultKeypress('z', juce::ModifierKeys::commandModifier);
            // ! imageLoadInProgress: without it, Cmd+Z mid-load would mutate
            // the outgoing image and push a redo entry that the install then
            // clobbers. JUCE re-queries this at key-press time, so gating here
            // covers the shortcut, not just the menu item.
            // Gated on pluginChain.empty(), not pluginEditorPanel -- a chain
            // session stays "open" (blocking Undo) even with nothing
            // currently selected (see deselectChainSlot()'s doc comment).
            result.setActive(! undoStack.empty() && pluginChain.empty() && headerEditorPanel == nullptr && ! imageLoadInProgress);
            break;

        case redoCommand:
            result.setInfo("Redo", "Redo the last undone action", "Edit", 0);
            result.addDefaultKeypress('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier);
            result.setActive(! redoStack.empty() && pluginChain.empty() && headerEditorPanel == nullptr && ! imageLoadInProgress);
            break;

        case cancelEditorCommand:
            // Not shown in any menu (setInfo's category/shortcut text is only
            // ever surfaced if this were added to a PopupMenu, which it isn't) -
            // purely a keyboard shortcut, mirroring whichever panel's own
            // Cancel button is currently on-screen. Deliberately still gated
            // on pluginEditorPanel specifically (not pluginChain.empty()):
            // Cancel removes whichever slot is selected, so there's
            // nothing for it to do with no slot mounted.
            result.setInfo("Cancel Editor", "Remove the selected chain slot, or cancel the header edit", "Edit", 0);
            result.addDefaultKeypress(juce::KeyPress::escapeKey, juce::ModifierKeys());
            result.setActive(pluginEditorPanel != nullptr || headerEditorPanel != nullptr);
            break;

        case loadImageCommand:
            // Backs the File > Load Image... menu item itself (see
            // menuModel's populateFileMenuLoadImageItem callback) as well as
            // the Cmd+O shortcut. Must mirror isPanelOpen() in
            // MainMenuModel::Callbacks so the shortcut and the menu item never
            // disagree, plus ! imageLoadInProgress, since loadImageClicked()
            // itself would otherwise just no-op mid-load anyway -- keeping it
            // inactive here is more honest than a shortcut that silently does
            // nothing.
            result.setInfo("Load Image...", "Load a BMP, PNM, PNG, or JPEG image", "File", 0);
            result.addDefaultKeypress('o', juce::ModifierKeys::commandModifier);
            result.setActive(pluginChain.empty() && headerEditorPanel == nullptr && ! imageLoadInProgress);
            break;

        case resetCommand:
            // Backs the File > Reset to Original menu item (see menuModel's
            // populateFileMenuResetItem callback) as well as the Cmd+Shift+R
            // shortcut. Must mirror hasOriginalImage() && isPanelOpen() in
            // MainMenuModel::Callbacks so the shortcut and the menu item never
            // disagree, plus ! imageLoadInProgress.
            result.setInfo("Reset to Original", "Discard all edits and revert to the originally loaded image", "File", 0);
            result.addDefaultKeypress('r', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier);
            result.setActive(originalImage != nullptr && pluginChain.empty()
                              && headerEditorPanel == nullptr && ! imageLoadInProgress);
            break;

        case exportImageCommand:
            // Backs the File > Export Image... menu item (see menuModel's
            // populateFileMenuExportItem callback) as well as the Cmd+S
            // shortcut. Must mirror hasWorkingImage() && ! panelOpen in
            // MainMenuModel::Callbacks so the shortcut and the menu item never
            // disagree, plus ! imageLoadInProgress.
            result.setInfo("Export Image...", "Export the current image as a PNG", "File", 0);
            result.addDefaultKeypress('s', juce::ModifierKeys::commandModifier);
            result.setActive(workingImage != nullptr && pluginChain.empty()
                              && headerEditorPanel == nullptr && ! imageLoadInProgress);
            break;

        case openSettingsCommand:
            // Deliberately always active: this backs an item in the native
            // "RAWDOG" app menu's extraAppleMenu (see the constructor), which
            // JUCE only rebuilds on a fresh setMacMainMenu() call, never on
            // menuModel.menuItemsChanged() - so a conditionally-active state
            // here would go stale. Settings doesn't touch pluginChain/
            // workingImage, so there's no real reason to disable it anyway.
            result.setInfo("Settings...", "Open plugin management settings", "General", 0);
            result.addDefaultKeypress(',', juce::ModifierKeys::commandModifier);
            result.setActive(true);
            break;

        case aboutCommand:
            // Same "deliberately always active" reasoning as
            // openSettingsCommand just above.
            result.setInfo("About", "Show version and app info", "General", 0);
            result.setActive(true);
            break;

        default:
            break;
    }
}

bool MainComponent::perform(const InvocationInfo& info)
{
    switch (info.commandID)
    {
        case undoCommand: undoClicked(); return true;
        case redoCommand: redoClicked(); return true;

        case cancelEditorCommand:
            if (pluginEditorPanel != nullptr)
                cancelEditorClicked();
            else if (headerEditorPanel != nullptr)
                cancelHeaderEditClicked();
            return true;

        case loadImageCommand:
            confirmDiscardChangesIfNeeded([this] { loadImageClicked(); });
            return true;

        case resetCommand:
            confirmDiscardChangesIfNeeded([this] { resetClicked(); });
            return true;

        case exportImageCommand:
            exportImageClicked();
            return true;

        case openSettingsCommand:
            openSettingsClicked();
            return true;

        case aboutCommand:
            aboutClicked();
            return true;

        default: return false;
    }
}

void MainComponent::confirmDiscardChangesIfNeeded(std::function<void()> proceed)
{
    if (undoStack.empty())
    {
        proceed();
        return;
    }

    juce::Component::SafePointer<MainComponent> safeThis(this);

    auto options = juce::MessageBoxOptions::makeOptionsOkCancel(
        juce::MessageBoxIconType::WarningIcon, "Unsaved Changes",
        "You have unsaved changes to the current image. Continuing will discard them.",
        "Discard Changes", "Cancel", this);

    juce::AlertWindow::showAsync(options, [safeThis, proceed](int result)
    {
        // Empirically 1 for the first button passed to makeOptionsOkCancel()
        // above ("Discard Changes") and 0 for the second ("Cancel") -- this
        // matches the "(buttonIndex + 1) % numButtons" convention documented
        // on the older AlertWindow::showOkCancelBox(), not a plain 0-based
        // button index.
        if (safeThis != nullptr && result == 1)
            proceed();
    });
}

void MainComponent::confirmQuit(std::function<void()> proceedToQuit)
{
    confirmDiscardChangesIfNeeded(std::move(proceedToQuit));
}

void MainComponent::updatePluginListEnablement()
{
    const bool hasImage = workingImage != nullptr;

    // Hard-walled off while the header editor is open or a load is in
    // flight, but deliberately NOT on hasImage -- browsing/searching the
    // plugin list is useful even with nothing loaded yet; only actually
    // *loading* a plugin needs an image, and addPluginToChain() itself
    // guards that (double-clicking a row without an image is a no-op there).
    const bool listBrowsable = headerEditorPanel == nullptr && ! imageLoadInProgress;
    pluginListBox.setEnabled(listBrowsable);
    listModel.setEnabled(listBrowsable);
    leftColumn.setListControlsEnabled(listBrowsable);
    pluginListBox.repaint();

    // The chain rack itself still requires an image (there's nothing for a
    // chain to process without one), unlike the browser above.
    const bool chainInteractive = hasImage && headerEditorPanel == nullptr && ! imageLoadInProgress;
    effectChainPanel.setEnabled(chainInteractive);
    effectChainPanel.setHasImage(hasImage);

    // pluginListBox.setEnabled() above doesn't reach its own internal
    // Viewport scrollbar -- juce::ScrollBar's mouseDown/mouseDrag never check
    // isEnabled() (that flag is purely informational for most components;
    // only widgets like Button/Slider that explicitly test it stop
    // responding), so a "disabled" list's scrollbar would otherwise stay
    // fully draggable and still light up on hover. setInterceptsMouseClicks
    // makes it swallow no mouse events at all while disabled -- so it can't
    // be dragged, scrolled, or hovered, and the cursor over it behaves as if
    // it isn't there.
    auto& listScrollBar = pluginListBox.getVerticalScrollBar();
    listScrollBar.setEnabled(listBrowsable);
    listScrollBar.setInterceptsMouseClicks(listBrowsable, listBrowsable);

    horizontalScrollBar.setEnabled(hasImage);

    // Grey out the waveform lane(s) whenever there's no image -- otherwise an
    // empty lane (blank white, no trace) reads identically to a real,
    // editable one with nothing selected.
    waveformView.setEnabled(hasImage);
    for (auto& channelView : channelWaveformViews)
        channelView.setEnabled(hasImage);

    horizontalScrollBar.setVisible(hasImage);
    splitModeToggle.setVisible(hasImage);
    rightColumn.setHasImage(hasImage);

    // The bipolar/unipolar dropdown is only relevant while a chain session is
    // open - this function already runs after every open/close. Keyed on
    // pluginChain (the session), not pluginEditorPanel (a specific mounted
    // slot): sample mode affects the whole chain's live preview regardless of
    // whether any one slot is currently selected for editing.
    const bool chainSessionOpen = ! pluginChain.empty();
    sampleModeLabel.setEnabled(chainSessionOpen);
    sampleModeCombo.setEnabled(chainSessionOpen);

    // Split-channel view only makes sense for a 3- or 4-channel chunky image
    // (BMP-24bit/PNM-P6/PNG) and never while the header editor is open (a live BMP
    // header edit can change biBitCount away from 24, making hasChannelPlanes()
    // false mid-edit, and header edits have no per-channel meaning at all).
    // Toggling it is *also* disabled while a chain session is open: the live-
    // preview worker thread renders directly from RawImage's own render/plane
    // caches whenever the chain is non-empty (see LivePreviewWorker's
    // live-preview-performance note), and those caches are only safe to touch
    // from one thread at a time -- toggling split mode would call
    // getChannelPlane()/toJuceImage() on the message thread concurrently with
    // the worker. Note this disables *toggling*, not the feature itself: an
    // already-on split mode started before the session opened stays on and
    // fully live-editable for the whole session (channel-scoped Apply is a
    // supported, unrelated code path).
    const bool splitMeaningful = hasImage && headerEditorPanel == nullptr && workingImage->hasChannelPlanes();
    splitModeToggle.setEnabled(splitMeaningful && ! chainSessionOpen && ! imageLoadInProgress);

    if (! splitMeaningful && splitModeToggle.getToggleState())
        setSplitMode(false);
}

void MainComponent::syncScrollBarToView()
{
    auto& primary = primaryWaveformView();
    const int numSamples = primary.getNumSamples();

    horizontalScrollBar.setRangeLimits(0.0, (double) juce::jmax(1, numSamples));
    horizontalScrollBar.setCurrentRange((double) primary.getViewStartSample(),
                                         (double) primary.getViewLengthSamples(),
                                         juce::dontSendNotification);
}

void MainComponent::scrollBarMoved(juce::ScrollBar* scrollBarThatHasMoved, double newRangeStart)
{
    if (scrollBarThatHasMoved != &horizontalScrollBar)
        return;

    // Scroll position is absolute, and a channel plane has a different total
    // sample count than the interleaved buffer (width*height vs.
    // width*height*channels) - convert to a fraction of the primary view's own
    // sample count, then re-apply that fraction to every view's own count, so
    // all of them (the plain waveform plus every channel lane) stay showing
    // the same proportional field of view.
    auto& primary = primaryWaveformView();
    const int primarySamples = primary.getNumSamples();
    const double fraction = primarySamples > 0 ? newRangeStart / (double) primarySamples : 0.0;

    waveformView.setViewStart((int) (fraction * waveformView.getNumSamples()));
    for (auto& view : channelWaveformViews)
        view.setViewStart((int) (fraction * view.getNumSamples()));
}

void MainComponent::handleAsyncUpdate()
{
    // Keyed on the chain itself, not on whether a specific slot happens to be
    // selected -- a still-open (non-empty) chain must keep reprocessing on
    // every selection change even with nothing currently mounted for editing.
    if (! pluginChain.empty())
        refreshLivePreview(); // must still reprocess bytes -- the glitched region itself re-scopes
    else if (workingImage != nullptr)
        updateHighlightOverlay(*workingImage, getCurrentSelectionScope()); // selection-only change: no image rebuild needed at all
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced(8);
    juce::Component* items[] = { &leftColumn, &outerResizerBar, &rightColumn };
    outerLayout.layOutComponents(items, 3, area.getX(), area.getY(),
                                  area.getWidth(), area.getHeight(),
                                  false /*side-by-side*/, true /*resizeOtherDimension*/);
}
