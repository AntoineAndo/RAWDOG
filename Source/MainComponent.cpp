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
        g.setColour(juce::Colours::black);
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
    addAndMakeVisible(pluginListBox);
    addAndMakeVisible(rescanButton);
    addAndMakeVisible(loadPluginButton);
    addAndMakeVisible(openEditorButton);
    addAndMakeVisible(statusLabel);

    imagePreview.setImagePlacement(juce::RectanglePlacement::centred);

    loadImageButton.onClick = [this] { loadImageClicked(); };
    exportImageButton.onClick = [this] { exportImageClicked(); };
    resetButton.onClick = [this] { resetClicked(); };
    rescanButton.onClick = [this] { refreshPluginList(); };
    loadPluginButton.onClick = [this] { loadPluginClicked(); };
    openEditorButton.onClick = [this] { openEditorClicked(); };

    pluginListBox.setModel(&listModel);

    setStatus("Load a BMP or PNM image, load a plugin, then open its editor to tweak and apply.");

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
            updatePreview();
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

void MainComponent::loadPluginClicked()
{
    auto* desc = listModel.getType(pluginListBox.getSelectedRow());
    if (desc == nullptr)
    {
        setStatus("Select a plugin in the list first.");
        return;
    }

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
    PluginHost::processWholeBuffer(*currentPlugin, buffer, blockSize);
    SampleFormat::bufferToBytes(buffer, workingImage->pixelBytes);

    updatePreview();
    setStatus("Applied " + currentPlugin->getName() + " to the whole buffer.");
}

void MainComponent::resetClicked()
{
    if (originalImage == nullptr)
        return;

    workingImage = std::make_unique<RawImage>(*originalImage);
    updatePreview();
    setStatus("Reset to original image.");
}

void MainComponent::updatePreview()
{
    if (workingImage != nullptr)
        imagePreview.setImage(workingImage->toJuceImage());
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced(8);

    auto topRow = area.removeFromTop(30);
    loadImageButton.setBounds(topRow.removeFromLeft(140));
    topRow.removeFromLeft(8);
    exportImageButton.setBounds(topRow.removeFromLeft(140));
    topRow.removeFromLeft(8);
    resetButton.setBounds(topRow.removeFromLeft(140));

    area.removeFromTop(8);

    auto bottomSection = area.removeFromBottom(230);

    imagePreview.setBounds(area);

    auto listArea = bottomSection.removeFromTop(150);
    pluginListBox.setBounds(listArea);

    bottomSection.removeFromTop(8);
    auto buttonRow = bottomSection.removeFromTop(30);
    rescanButton.setBounds(buttonRow.removeFromLeft(140));
    buttonRow.removeFromLeft(8);
    loadPluginButton.setBounds(buttonRow.removeFromLeft(180));
    buttonRow.removeFromLeft(8);
    openEditorButton.setBounds(buttonRow.removeFromLeft(160));

    bottomSection.removeFromTop(8);
    statusLabel.setBounds(bottomSection);
}
