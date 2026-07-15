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
    addAndMakeVisible(loadImageButton);
    addAndMakeVisible(exportImageButton);
    addAndMakeVisible(resetButton);
    addAndMakeVisible(imagePreview);
    addAndMakeVisible(waveformView);
    addAndMakeVisible(waveformZoomSlider);
    addAndMakeVisible(waveformZoomLabel);
    addAndMakeVisible(horizontalZoomSlider);
    addAndMakeVisible(horizontalScrollBar);
    addAndMakeVisible(pluginListBox);
    addAndMakeVisible(rescanButton);
    addAndMakeVisible(statusLabel);

    imagePreview.setImagePlacement(juce::RectanglePlacement::centred);

    loadImageButton.onClick = [this] { loadImageClicked(); };
    exportImageButton.onClick = [this] { exportImageClicked(); };
    resetButton.onClick = [this] { resetClicked(); };
    rescanButton.onClick = [this] { refreshPluginList(); };

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
    waveformView.onSelectionChanged = [this] { updatePreview(); };

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
    if (currentPlugin != nullptr)
        currentPlugin->releaseResources();
}

void MainComponent::setStatus(const juce::String& text)
{
    statusLabel.setText(text, juce::dontSendNotification);
}

void MainComponent::refreshPluginList()
{
    scanner.scanAll();
    listModel.refresh();
    pluginListBox.updateContent();
    pluginListBox.repaint();
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
            updateWaveform(true);
            updatePreview();
            updatePluginListEnablement();
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

    fileChooser = std::make_unique<juce::FileChooser>("Export image", juce::File(), "*.bmp;*.pnm;*.ppm;*.pgm");

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
        pluginWindow = nullptr;
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

    if (pluginWindow != nullptr)
    {
        pluginWindow->toFront(true);
        return;
    }

    auto* editor = currentPlugin->hasEditor() ? currentPlugin->createEditorIfNeeded()
                                               : new juce::GenericAudioProcessorEditor(*currentPlugin);

    if (editor == nullptr)
    {
        setStatus("This plugin has no editor UI.");
        return;
    }

    pluginWindow = std::make_unique<PluginWindow>(editor, [this]
    {
        applyClicked();

        if (pluginWindow != nullptr)
            pluginWindow->setVisible(false);
    });
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

    auto buffer = SampleFormat::bytesToBuffer(workingImage->pixelBytes);
    const auto selection = waveformView.getSelectionSampleRange();

    if (! selection.isEmpty())
    {
        const int start = selection.getStart();
        const int length = selection.getLength();

        juce::AudioBuffer<float> selectedBuffer(1, length);
        selectedBuffer.copyFrom(0, 0, buffer, 0, start, length);

        PluginHost::processWholeBuffer(*currentPlugin, selectedBuffer, blockSize);

        buffer.copyFrom(0, start, selectedBuffer, 0, 0, length);

        setStatus("Applied " + currentPlugin->getName() + " to selection [" + juce::String(start) + ", " + juce::String(start + length) + ").");
    }
    else
    {
        PluginHost::processWholeBuffer(*currentPlugin, buffer, blockSize);
        setStatus("Applied " + currentPlugin->getName() + " to the whole buffer.");
    }

    SampleFormat::bufferToBytes(buffer, workingImage->pixelBytes);

    updatePreview();
    updateWaveform();
}

void MainComponent::resetClicked()
{
    if (originalImage == nullptr)
        return;

    workingImage = std::make_unique<RawImage>(*originalImage);
    updateWaveform(true);
    updatePreview();
    setStatus("Reset to original image.");
}

void MainComponent::updatePreview()
{
    if (workingImage != nullptr)
        imagePreview.setImage(workingImage->toJuceImage(waveformView.getSelectionSampleRange()));
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

    auto leftColumn = area.removeFromLeft(260);
    area.removeFromLeft(8);

    rescanButton.setBounds(leftColumn.removeFromTop(30));
    leftColumn.removeFromTop(8);
    pluginListBox.setBounds(leftColumn);

    auto topRow = area.removeFromTop(30);
    loadImageButton.setBounds(topRow.removeFromLeft(140));
    topRow.removeFromLeft(8);
    exportImageButton.setBounds(topRow.removeFromLeft(140));
    topRow.removeFromLeft(8);
    resetButton.setBounds(topRow.removeFromLeft(140));

    area.removeFromTop(8);

    auto statusArea = area.removeFromBottom(24);
    area.removeFromBottom(8);
    auto waveformSection = area.removeFromBottom(140);
    area.removeFromBottom(8);

    imagePreview.setBounds(area);

    auto waveformTop = waveformSection.removeFromTop(100);
    waveformSection.removeFromTop(4);

    auto zoomArea = waveformTop.removeFromRight(40);
    waveformTop.removeFromRight(8);
    waveformZoomLabel.setBounds(zoomArea.removeFromTop(16));
    waveformZoomSlider.setBounds(zoomArea);
    waveformView.setBounds(waveformTop);

    auto horizontalZoomArea = waveformSection.removeFromLeft(120);
    waveformSection.removeFromLeft(8);
    horizontalZoomSlider.setBounds(horizontalZoomArea);
    horizontalScrollBar.setBounds(waveformSection);

    statusLabel.setBounds(statusArea);
}
