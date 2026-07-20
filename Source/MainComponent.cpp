#include "MainComponent.h"
#include "PluginHost.h"
#include "SampleFormat.h"

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

    juce::MenuBarModel::setMacMainMenu(&menuModel);

    commandManager.registerAllCommandsForTarget(this);
    commandManager.setFirstCommandTarget(this);
    addKeyListener(commandManager.getKeyMappings());
    setWantsKeyboardFocus(true);

    waveformZoomSlider.setRange(1.0, 20.0, 0.1);
    waveformZoomSlider.setValue(1.0);
    waveformZoomSlider.onValueChange = [this]
    {
        const auto zoom = (float) waveformZoomSlider.getValue();
        waveformView.setVerticalZoom(zoom);
        for (auto& view : channelWaveformViews)
            view.setVerticalZoom(zoom);
    };
    waveformZoomLabel.setJustificationType(juce::Justification::centred);
    waveformZoomLabel.setFont(juce::Font(juce::FontOptions(12.0f)));

    horizontalZoomSlider.setRange(1.0, 200.0, 0.1);
    horizontalZoomSlider.setSkewFactorFromMidPoint(10.0);
    horizontalZoomSlider.setValue(1.0);
    horizontalZoomSlider.onValueChange = [this]
    {
        const auto zoom = (float) horizontalZoomSlider.getValue();
        waveformView.setHorizontalZoom(zoom);
        for (auto& view : channelWaveformViews)
            view.setHorizontalZoom(zoom);
    };

    imagePreview.onClickWithNoImage = [this] { loadImageClicked(); };

    horizontalScrollBar.addListener(this);
    waveformView.onViewChanged = [this] { syncScrollBarToView(); };
    waveformView.onSelectionChanged = [this] { triggerAsyncUpdate(); };
    waveformView.onBeforeSelectionChange = [this]
    {
        // While a live-preview panel is open, a new selection drag only rescopes
        // the uncommitted preview (see onSelectionChanged above) — nothing has
        // been committed yet, so this must not push an undo entry.
        if (pluginEditorPanel == nullptr)
            pushUndoState();
    };

    splitModeToggle.setClickingTogglesState(true);
    splitModeToggle.onClick = [this] { setSplitMode(splitModeToggle.getToggleState()); };

    sampleModeCombo.addItem("Bipolar", 1);
    sampleModeCombo.addItem("Unipolar", 2);
    sampleModeCombo.setSelectedId(1, juce::dontSendNotification);
    sampleModeCombo.onChange = [this] { sampleModeChanged(); };
    sampleModeLabel.setVisible(false);
    sampleModeCombo.setVisible(false); // hidden until a plugin panel opens

    for (int c = 0; c < 4; ++c)
    {
        auto* view = &channelWaveformViews[(size_t) c];
        view->onViewChanged = [this] { syncScrollBarToView(); };
        view->onSelectionChanged = [this] { triggerAsyncUpdate(); };
        view->onBeforeSelectionChange = [this, c]
        {
            activeSelectionChannel = (RawImage::Channel) c;

            // Only one lane has an active selection at a time — starting a new
            // one elsewhere clears the others. setSelectionSampleRange({}) (not
            // clearSelection()) deliberately, since clearSelection() itself fires
            // onBeforeSelectionChange and would recurse into pushUndoState() for
            // the "losing" lane.
            for (int other = 0; other < 4; ++other)
                if (other != c)
                    channelWaveformViews[(size_t) other].setSelectionSampleRange({});

            if (pluginEditorPanel == nullptr)
                pushUndoState();
        };
    }

    // Hidden until refreshChannelWaveforms() shows it for a loaded PNG that
    // actually has an alpha channel — see hasAlphaChannel().
    channelWaveformViews[3].setVisible(false);

    livePreviewWorker.onResultReady = [this](LivePreviewWorker::Result result) { applyLivePreviewResult(std::move(result)); };
    previewBusySpinner.isBusy = [this] { return livePreviewWorker.isBusy() || imageLoadInProgress; };

    pluginParamWatcher.onPluginParametersChanged = [this] { refreshLivePreview(); };
    pluginParamWatcher.onParameterValueChanged = [this](const juce::String& parameterName, const juce::String& valueText)
    {
        if (currentPlugin != nullptr)
            setStatus(currentPlugin->getName() + " — " + parameterName + ": " + valueText);
    };

    pluginListBox.setModel(&listModel);
    pluginListBox.setColour(juce::ListBox::backgroundColourId, juce::Colours::darkgrey.darker());
    listModel.onDoubleClick = [this](int row) { loadAndOpenPlugin(row); };
    listModel.onFavouritesChanged = [this] { pluginListBox.updateContent(); pluginListBox.repaint(); };

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

    setStatus("Load a BMP, PNM, or PNG image, then double-click a plugin to load it and tweak/apply.");
    updatePluginListEnablement();

    setSize(900, 700);

    if (scanner.loadCachedPluginList())
    {
        listModel.refresh();
        pluginListBox.updateContent();
        pluginListBox.repaint();
        setStatus("Loaded " + juce::String(scanner.getKnownPluginList().getNumTypes())
                    + " cached plugin(s). Use Rescan Plugins to refresh.");
    }
    else
    {
        // First launch on this machine — nothing cached yet, so an initial
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
    // below (member destruction order), for consistency with the other teardown
    // sites (openEditorClicked/endLivePreviewSession) even though Component's own
    // destructor already self-detaches from its parent's child list.
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

    // Full shutdown, not just idle-draining: must happen before
    // currentPlugin->releaseResources() below, since the worker may still be
    // inside plugin.processBlock() on it. stopThread() lets any in-flight
    // pass finish naturally first (run()'s loop only checks
    // threadShouldExit() between jobs) before forcing the issue.
    livePreviewWorker.shutdown(5000);

    if (currentPlugin != nullptr)
        currentPlugin->releaseResources();
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
    // otherwise goes stale until menuItemsChanged() is called explicitly —
    // see PROJECT.md) so a second click can't race the background scan.
    menuModel.menuItemsChanged();

    scanner.scanAll([this]
    {
        listModel.refresh();
        pluginListBox.updateContent();
        pluginListBox.repaint();
        scanner.saveCachedPluginListToDisk();

        auto status = "Found " + juce::String(scanner.getKnownPluginList().getNumTypes()) + " plugin(s).";

        // Plugins that crashed the scan last time are now being skipped
        // rather than re-probed (see PluginScanner's dead man's pedal file) —
        // surface that so a rescan showing fewer plugins than expected isn't
        // mistaken for a scanning bug.
        const auto& skippedCrashers = scanner.getLastSkippedCrashers();

        if (! skippedCrashers.isEmpty())
            status << " Skipped " << skippedCrashers.size() << " plugin(s) that crashed a previous scan.";

        setStatus(status);
        menuModel.menuItemsChanged();
    });
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

    const auto currentScope = getCurrentSelectionScope();
    redoStack.push_back({ workingImage->headerBytes, workingImage->pixelBytes, currentScope.channel, currentScope.range });
    const auto entry = undoStack.back();
    undoStack.pop_back();

    workingImage->restoreSnapshot(entry.headerBytes, entry.pixelBytes);
    updateWaveform();
    restoreSelectionScope(entry.selectionChannel, entry.selection);
    updatePreview();
    menuModel.menuItemsChanged();
    setStatus("Undid last action.");
}

void MainComponent::redoClicked()
{
    if (redoStack.empty() || workingImage == nullptr)
        return;

    const auto currentScope = getCurrentSelectionScope();
    undoStack.push_back({ workingImage->headerBytes, workingImage->pixelBytes, currentScope.channel, currentScope.range });
    const auto entry = redoStack.back();
    redoStack.pop_back();

    workingImage->restoreSnapshot(entry.headerBytes, entry.pixelBytes);
    updateWaveform();
    restoreSelectionScope(entry.selectionChannel, entry.selection);
    updatePreview();
    menuModel.menuItemsChanged();
    setStatus("Redid last action.");
}

void MainComponent::pushUndoState()
{
    if (workingImage != nullptr)
    {
        const auto scope = getCurrentSelectionScope();
        undoStack.push_back({ workingImage->headerBytes, workingImage->pixelBytes, scope.channel, scope.range });
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
            result.setActive(! undoStack.empty() && pluginEditorPanel == nullptr && headerEditorPanel == nullptr && ! imageLoadInProgress);
            break;

        case redoCommand:
            result.setInfo("Redo", "Redo the last undone action", "Edit", 0);
            result.addDefaultKeypress('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier);
            result.setActive(! redoStack.empty() && pluginEditorPanel == nullptr && headerEditorPanel == nullptr && ! imageLoadInProgress);
            break;

        case cancelEditorCommand:
            // Not shown in any menu (setInfo's category/shortcut text is only
            // ever surfaced if this were added to a PopupMenu, which it isn't) —
            // purely a keyboard shortcut, mirroring whichever panel's own
            // Cancel button is currently on-screen.
            result.setInfo("Cancel Editor", "Cancel the currently open plugin or header editor panel", "Edit", 0);
            result.addDefaultKeypress(juce::KeyPress::escapeKey, juce::ModifierKeys());
            result.setActive(pluginEditorPanel != nullptr || headerEditorPanel != nullptr);
            break;

        case loadImageCommand:
            // Not shown in any menu -- File > Load Image... stays a plain
            // (non-command) menu item; this is purely the keyboard-shortcut
            // path. Same gating as that menu item's own enablement
            // (isPanelOpen() in MainMenuModel::Callbacks) plus
            // ! imageLoadInProgress, since loadImageClicked() itself would
            // otherwise just no-op mid-load anyway -- keeping it inactive here
            // is more honest than a shortcut that silently does nothing.
            result.setInfo("Load Image...", "Load a BMP, PNM, or PNG image", "File", 0);
            result.addDefaultKeypress('o', juce::ModifierKeys::commandModifier);
            result.setActive(pluginEditorPanel == nullptr && headerEditorPanel == nullptr && ! imageLoadInProgress);
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

        case loadImageCommand: loadImageClicked(); return true;

        default: return false;
    }
}

void MainComponent::updatePluginListEnablement()
{
    const bool hasImage = workingImage != nullptr;

    // Hard-walled off while the header editor is open, separately from the
    // plugin-editor case: double-clicking a plugin mid-header-edit would
    // otherwise silently discard the in-progress edit via loadAndOpenPlugin()'s
    // own panel-swap logic, which is a worse surprise than just disabling the
    // list outright.
    const bool listInteractive = hasImage && headerEditorPanel == nullptr && ! imageLoadInProgress;
    pluginListBox.setEnabled(listInteractive);
    listModel.setEnabled(listInteractive);
    leftColumn.setListControlsEnabled(listInteractive);
    pluginListBox.repaint();

    waveformZoomSlider.setEnabled(hasImage);
    horizontalZoomSlider.setEnabled(hasImage);
    horizontalScrollBar.setEnabled(hasImage);

    // The bipolar/unipolar dropdown is only relevant while the plugin editor
    // panel is open — this function already runs after every open/close.
    const bool pluginPanelOpen = pluginEditorPanel != nullptr;
    sampleModeLabel.setVisible(pluginPanelOpen);
    sampleModeCombo.setVisible(pluginPanelOpen);

    // Split-channel view only makes sense for a 3- or 4-channel chunky image
    // (BMP-24bit/PNM-P6/PNG) and never while the header editor is open (a live BMP
    // header edit can change biBitCount away from 24, making hasChannelPlanes()
    // false mid-edit, and header edits have no per-channel meaning at all).
    // Toggling it is *also* disabled while a plugin panel is open: the live-
    // preview worker thread renders directly from RawImage's own render/plane
    // caches during a session (see LivePreviewWorker's live-preview-performance
    // note), and those caches are only safe to touch from one thread at a
    // time -- toggling split mode would call getChannelPlane()/toJuceImage()
    // on the message thread concurrently with the worker. Note this disables
    // *toggling*, not the feature itself: an already-on split mode started
    // before the panel opened stays on and fully live-editable for the whole
    // session (channel-scoped Apply is a supported, unrelated code path).
    const bool splitMeaningful = hasImage && headerEditorPanel == nullptr && workingImage->hasChannelPlanes();
    splitModeToggle.setEnabled(splitMeaningful && ! pluginPanelOpen && ! imageLoadInProgress);

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
    // width*height*channels) — convert to a fraction of the primary view's own
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
    if (pluginEditorPanel != nullptr)
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
