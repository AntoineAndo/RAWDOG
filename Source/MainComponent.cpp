#include "MainComponent.h"
#include "PluginHost.h"
#include "SampleFormat.h"

void MainComponent::PluginListModel::paintListBoxItem(int rowNumber, juce::Graphics& g,
                                                        int width, int height, bool rowIsSelected)
{
    if (rowIsSelected)
        g.fillAll(juce::Colours::lightblue);

    if (auto* desc = getType(rowNumber))
    {
        g.setColour(enabled ? juce::Colours::white : juce::Colours::grey);
        g.drawText(desc->name + "  —  " + desc->manufacturerName + "  (" + desc->pluginFormatName + ")",
                    4, 0, width - 8, height, juce::Justification::centredLeft);
    }
}

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

    juce::MenuBarModel::setMacMainMenu(this);

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

    setStatus("Load a BMP or PNM image, then double-click a plugin to load it and tweak/apply.");
    updatePluginListEnablement();

    setSize(900, 700);

    refreshPluginList();
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

juce::StringArray MainComponent::getMenuBarNames()
{
    return { "File", "Edit" };
}

juce::PopupMenu MainComponent::getMenuForIndex(int topLevelMenuIndex, const juce::String& /*menuName*/)
{
    juce::PopupMenu menu;

    const bool panelOpen = pluginEditorPanel != nullptr;

    if (topLevelMenuIndex == 0)
    {
        menu.addItem(loadImageMenuItem, "Load Image...", ! panelOpen);
        menu.addItem(exportImageMenuItem, "Export Image...", workingImage != nullptr);
        menu.addItem(resetMenuItem, "Reset to Original", originalImage != nullptr && ! panelOpen);
        menu.addSeparator();
        menu.addItem(rescanPluginsMenuItem, "Rescan Plugins", ! scanner.isScanning() && ! panelOpen);
    }
    else if (topLevelMenuIndex == 1)
    {
        menu.addCommandItem(&commandManager, undoCommand);
        menu.addCommandItem(&commandManager, redoCommand);
    }

    return menu;
}

void MainComponent::menuItemSelected(int menuItemID, int /*topLevelMenuIndex*/)
{
    switch (menuItemID)
    {
        case loadImageMenuItem:      loadImageClicked(); break;
        case exportImageMenuItem:    exportImageClicked(); break;
        case resetMenuItem:          resetClicked(); break;
        case rescanPluginsMenuItem:  refreshPluginList(); break;
        default: break;
    }
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
    menuItemsChanged();

    scanner.scanAll([this]
    {
        listModel.refresh();
        pluginListBox.updateContent();
        pluginListBox.repaint();
        setStatus("Found " + juce::String(scanner.getKnownPluginList().getNumTypes()) + " plugin(s).");
        menuItemsChanged();
    });
}

void MainComponent::loadImageClicked()
{
    fileChooser = std::make_unique<juce::FileChooser>("Load image (24-bit BMP or raw PNM)",
                                                        juce::File(), "*.bmp;*.pnm;*.ppm;*.pgm");

    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (! file.existsAsFile())
                return;

            juce::String errorMessage;
            auto image = RawImage::loadFromFile(file, errorMessage);

            if (image == nullptr)
            {
                setStatus("Failed to load image: " + errorMessage);
                return;
            }

            originalImage = std::move(image);
            workingImage = std::make_unique<RawImage>(*originalImage);
            undoStack.clear();
            redoStack.clear();
            updateWaveform(true);
            updatePreview(true);
            updatePluginListEnablement();
            menuItemsChanged();
            setStatus("Loaded " + file.getFileName() + " (" + juce::String(workingImage->pixelBytes.getSize()) + " bytes of pixel data).");
        });
}

void MainComponent::exportImageClicked()
{
    if (workingImage == nullptr)
    {
        setStatus("Nothing to export — load an image first.");
        return;
    }

    // writeToFile() always writes back the original format's header+pixel bytes
    // verbatim, so the export dialog must only offer the extension matching the
    // format the image was actually loaded as (e.g. a loaded .pnm exported as
    // "x.bmp" would silently contain PNM bytes under a .bmp name otherwise).
    const auto suggestedFile = juce::File::getCurrentWorkingDirectory()
                                    .getChildFile("export" + workingImage->getDefaultExportExtension());

    fileChooser = std::make_unique<juce::FileChooser>("Export image", suggestedFile, workingImage->getExportWildcard());

    fileChooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
        [this](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File())
                return;

            if (workingImage->writeToFile(file))
                setStatus("Exported to " + file.getFullPathName());
            else
                setStatus("Failed to write file.");
        });
}

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
    menuItemsChanged();

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

    auto buffer = SampleFormat::bytesToBuffer(workingImage->pixelBytes);

    if (! selection.isEmpty())
    {
        const int start = selection.getStart();
        const int length = selection.getLength();
        juce::AudioBuffer<float> selectedBuffer(1, length);
        selectedBuffer.copyFrom(0, 0, buffer, 0, start, length);
        PluginHost::processWholeBuffer(plugin, selectedBuffer, blockSize);
        buffer.copyFrom(0, start, selectedBuffer, 0, 0, length);
    }
    else
    {
        PluginHost::processWholeBuffer(plugin, buffer, blockSize);
    }

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
    waveformView.setBuffer(SampleFormat::bytesToBuffer(livePreviewBytes), false);
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
    menuItemsChanged();
}

void MainComponent::applyClicked()
{
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
    menuItemsChanged();
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
    menuItemsChanged();
    setStatus("Redid last action.");
}

void MainComponent::pushUndoState()
{
    if (workingImage != nullptr)
        undoStack.push_back({ workingImage->pixelBytes, waveformView.getSelectionSampleRange() });

    redoStack.clear();
    menuItemsChanged();
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

void MainComponent::updatePreview(bool resetView)
{
    if (workingImage != nullptr)
        imagePreview.setImage(workingImage->toJuceImage(waveformView.getSelectionSampleRange()), resetView);
}

void MainComponent::updateWaveform(bool resetView)
{
    if (workingImage == nullptr)
        return;

    waveformView.setBuffer(SampleFormat::bytesToBuffer(workingImage->pixelBytes), resetView);

    if (resetView)
        horizontalZoomSlider.setValue(1.0, juce::dontSendNotification);
}

void MainComponent::updatePluginListEnablement()
{
    const bool hasImage = workingImage != nullptr;
    pluginListBox.setEnabled(hasImage);
    listModel.setEnabled(hasImage);
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
