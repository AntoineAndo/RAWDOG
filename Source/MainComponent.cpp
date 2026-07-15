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
    waveformZoomSlider.onValueChange = [this] { waveformView.setVerticalZoom((float) waveformZoomSlider.getValue()); };
    waveformZoomLabel.setJustificationType(juce::Justification::centred);
    waveformZoomLabel.setFont(juce::Font(juce::FontOptions(12.0f)));

    horizontalZoomSlider.setRange(1.0, 200.0, 0.1);
    horizontalZoomSlider.setSkewFactorFromMidPoint(10.0);
    horizontalZoomSlider.setValue(1.0);
    horizontalZoomSlider.onValueChange = [this] { waveformView.setHorizontalZoom((float) horizontalZoomSlider.getValue()); };

    horizontalScrollBar.addListener(this);
    waveformView.onViewChanged = [this] { syncScrollBarToView(); };
    waveformView.onSelectionChanged = [this]
    {
        if (pluginEditorPanel != nullptr)
            refreshLivePreview();
        else
            updatePreview();
    };
    waveformView.onBeforeSelectionChange = [this]
    {
        // While a live-preview panel is open, a new selection drag only rescopes
        // the uncommitted preview (see onSelectionChanged above) — nothing has
        // been committed yet, so this must not push an undo entry.
        if (pluginEditorPanel == nullptr)
            pushUndoState();
    };

    pluginParamWatcher.onPluginParametersChanged = [this] { refreshLivePreview(); };

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

    setStatus("Load a BMP or PNM image, then double-click a plugin to load it and tweak/apply.");
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
    juce::MenuBarModel::setMacMainMenu(nullptr);

    pluginParamWatcher.attachTo(nullptr);

    // Clear leftColumn's non-owning pointer before pluginEditorPanel is destroyed
    // below (member destruction order), for consistency with the other teardown
    // sites (openEditorClicked/endLivePreviewSession) even though Component's own
    // destructor already self-detaches from its parent's child list.
    leftColumn.setEditorPanel(nullptr);

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

    redoStack.push_back({ workingImage->pixelBytes, waveformView.getSelectionSampleRange() });
    const auto entry = undoStack.back();
    undoStack.pop_back();

    workingImage->pixelBytes = entry.pixelBytes;
    updateWaveform();
    waveformView.setSelectionSampleRange(entry.selection);
    updatePreview();
    menuModel.menuItemsChanged();
    setStatus("Undid last action.");
}

void MainComponent::redoClicked()
{
    if (redoStack.empty() || workingImage == nullptr)
        return;

    undoStack.push_back({ workingImage->pixelBytes, waveformView.getSelectionSampleRange() });
    const auto entry = redoStack.back();
    redoStack.pop_back();

    workingImage->pixelBytes = entry.pixelBytes;
    updateWaveform();
    waveformView.setSelectionSampleRange(entry.selection);
    updatePreview();
    menuModel.menuItemsChanged();
    setStatus("Redid last action.");
}

void MainComponent::pushUndoState()
{
    if (workingImage != nullptr)
        undoStack.push_back({ workingImage->pixelBytes, waveformView.getSelectionSampleRange() });

    redoStack.clear();
    menuModel.menuItemsChanged();
}

juce::ApplicationCommandTarget* MainComponent::getNextCommandTarget()
{
    return nullptr;
}

void MainComponent::getAllCommands(juce::Array<juce::CommandID>& commands)
{
    commands.add(undoCommand);
    commands.add(redoCommand);
}

void MainComponent::getCommandInfo(juce::CommandID commandID, juce::ApplicationCommandInfo& result)
{
    switch (commandID)
    {
        case undoCommand:
            result.setInfo("Undo", "Undo the last action", "Edit", 0);
            result.addDefaultKeypress('z', juce::ModifierKeys::commandModifier);
            result.setActive(! undoStack.empty() && pluginEditorPanel == nullptr);
            break;

        case redoCommand:
            result.setInfo("Redo", "Redo the last undone action", "Edit", 0);
            result.addDefaultKeypress('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier);
            result.setActive(! redoStack.empty() && pluginEditorPanel == nullptr);
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
        default: return false;
    }
}

void MainComponent::updatePluginListEnablement()
{
    const bool hasImage = workingImage != nullptr;
    pluginListBox.setEnabled(hasImage);
    listModel.setEnabled(hasImage);
    leftColumn.setListControlsEnabled(hasImage);
    pluginListBox.repaint();

    waveformZoomSlider.setEnabled(hasImage);
    horizontalZoomSlider.setEnabled(hasImage);
    horizontalScrollBar.setEnabled(hasImage);
}

void MainComponent::syncScrollBarToView()
{
    const int numSamples = waveformView.getNumSamples();

    horizontalScrollBar.setRangeLimits(0.0, (double) juce::jmax(1, numSamples));
    horizontalScrollBar.setCurrentRange((double) waveformView.getViewStartSample(),
                                         (double) waveformView.getViewLengthSamples(),
                                         juce::dontSendNotification);
}

void MainComponent::scrollBarMoved(juce::ScrollBar* scrollBarThatHasMoved, double newRangeStart)
{
    if (scrollBarThatHasMoved == &horizontalScrollBar)
        waveformView.setViewStart((int) newRangeStart);
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced(8);
    juce::Component* items[] = { &leftColumn, &outerResizerBar, &rightColumn };
    outerLayout.layOutComponents(items, 3, area.getX(), area.getY(),
                                  area.getWidth(), area.getHeight(),
                                  false /*side-by-side*/, true /*resizeOtherDimension*/);
}
