#include "MainComponent.h"
#include "PluginHost.h"
#include "SampleFormat.h"

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

            // A freshly-loaded RawImage always defaults to bipolar, but the combo
            // and waveform views are session-long UI state left over from any
            // previous image — reset them to match.
            sampleModeCombo.setSelectedId(1, juce::dontSendNotification);
            waveformView.setSampleMode(SampleFormat::Mode::bipolar);
            for (auto& view : channelWaveformViews)
                view.setSampleMode(SampleFormat::Mode::bipolar);

            updateWaveform(true);
            updatePreview(true);
            updatePluginListEnablement();
            menuModel.menuItemsChanged();
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

    // Export always produces a PNG, regardless of the format the image was
    // loaded from — a real, widely-viewable encoded image rather than a raw
    // BMP/PNM byte reconstruction.
    const auto suggestedFile = juce::File::getCurrentWorkingDirectory().getChildFile("export.png");

    fileChooser = std::make_unique<juce::FileChooser>("Export image", suggestedFile, "*.png");

    fileChooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
        [this](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File())
                return;

            if (workingImage->writeToPngFile(file))
                setStatus("Exported to " + file.getFullPathName());
            else
                setStatus("Failed to write file.");
        });
}

void MainComponent::updatePreview(bool resetView)
{
    if (workingImage == nullptr)
        return;

    const auto scope = getCurrentSelectionScope();

    if (scope.channel.has_value())
        imagePreview.setImage(workingImage->toJuceImage(*scope.channel, scope.range), resetView);
    else
        imagePreview.setImage(workingImage->toJuceImage(scope.range), resetView);
}

void MainComponent::updateWaveform(bool resetView)
{
    if (workingImage == nullptr)
        return;

    waveformView.setBuffer(SampleFormat::bytesToBuffer(workingImage->getVisualOrderedPixelBytes(), workingImage->getSampleMode()), resetView);

    if (resetView)
        horizontalZoomSlider.setValue(1.0, juce::dontSendNotification);
}
