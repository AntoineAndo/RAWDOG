#include "MainComponent.h"
#include "PluginHost.h"
#include "RawCameraConverter.h"
#include "SampleFormat.h"

namespace
{
    // Everything the async image-load job produces, bundled for the one
    // message-thread hand-off in loadImageClicked()'s completion. On failure,
    // working stays null and errorMessage says why.
    struct LoadedImage
    {
        std::unique_ptr<RawImage> original, working;
        juce::AudioBuffer<float> waveformSamples;
        WaveformPeaks::Partial waveformPeaks;
        juce::String errorMessage;
        juce::String fileName;
        juce::String fileBaseName;
    };

    // Runs on MainComponent::imageLoaderPool's thread. Touches only its
    // arguments and objects it creates itself -- in particular, warming
    // RawImage's lazy caches here is safe because nothing else can see these
    // objects until the completion installs them on the message thread.
    void loadImageOffThread(const juce::File& file, LoadedImage& result)
    {
        // Scoped to this job only. Destroyed (deleting the temp BMP) right
        // after RawImage::loadFromFile() below has read it fully into memory.
        std::unique_ptr<juce::TemporaryFile> tempBmp;
        juce::File fileToLoad = file;

        if (RawCameraConverter::isRawCameraFile(file))
        {
            tempBmp = std::make_unique<juce::TemporaryFile>(".bmp");

            if (! RawCameraConverter::convertToBmp(file, tempBmp->getFile(), result.errorMessage))
                return;

            fileToLoad = tempBmp->getFile();
        }

        result.original = RawImage::loadFromFile(fileToLoad, result.errorMessage);

        if (result.original == nullptr)
            return;

        result.working = std::make_unique<RawImage>(*result.original);

        // Pre-pay, off-thread, everything install time would otherwise have to
        // do on the message thread: warm the plain-render cache (so
        // the completion's toJuceImage() is a cheap handle copy), and build
        // the waveform float buffer + its peak buckets (a fresh RawImage
        // always defaults to bipolar, so this matches the sample-mode reset
        // the completion performs).
        result.working->toJuceImage();
        result.waveformSamples = SampleFormat::bytesToBuffer(result.working->getVisualOrderedPixelBytes(),
                                                             result.working->getSampleMode());

        if (result.waveformSamples.getNumSamples() > 0)
            result.waveformPeaks = WaveformPeaks::computePartial(result.waveformSamples.getReadPointer(0),
                                                                 0, result.waveformSamples.getNumSamples());
    }
}

void MainComponent::loadImageClicked()
{
    // Also reachable through imagePreview.onClickWithNoImage (a click on the
    // empty preview area), not just the menu item the busy state already
    // disables -- so the entry point itself needs the guard too.
    if (imageLoadInProgress)
        return;

    fileChooser = std::make_unique<juce::FileChooser>("Load image (24-bit BMP, raw PNM, PNG, JPEG, or RAF/DNG camera raw)",
                                                        juce::File(), "*.bmp;*.pnm;*.ppm;*.pgm;*.png;*.jpg;*.jpeg;*.dng;*.raf");

    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file.existsAsFile())
                loadImageFile(file);
        });
}

void MainComponent::loadImageFile(const juce::File& file)
{
    if (imageLoadInProgress || ! file.existsAsFile())
        return;

    // The heavy work (RAW conversion, file parse, waveform float
    // conversion, first render -- ~2s of measured message-thread
    // stall on a ~78MB image when it all ran here synchronously)
    // happens on imageLoaderPool's thread; this thread only flips
    // into the "loading" state (spinner on via previewBusySpinner's
    // isBusy feed, image-mutating actions disabled) and installs the
    // finished result when it comes back.
    imageLoadInProgress = true;
    setStatus("Loading " + file.getFileName() + juce::String::fromUTF8("…"));
    updatePluginListEnablement();
    menuModel.menuItemsChanged();

    imageLoaderPool.addJob([file, safeThis = juce::Component::SafePointer<MainComponent>(this),
                            aliveWeak = std::weak_ptr<void>(imageLoadAliveToken)]
    {
        // ImageIO/CoreGraphics + toJuceImage() run on a raw pool
        // thread with no run-loop autorelease pool -- same insurance
        // as LivePreviewWorker::renderResult().
        JUCE_AUTORELEASEPOOL
        {
            auto result = std::make_shared<LoadedImage>();
            result->fileName = file.getFileName();
            result->fileBaseName = file.getFileNameWithoutExtension();
            loadImageOffThread(file, *result);

            // Two guards, both needed: the alive token covers the
            // shutdown window where ~MainComponent has started but
            // ~Component (which nulls SafePointers) hasn't run yet
            // (see imageLoadAliveToken); the SafePointer covers full
            // destruction after that.
            juce::MessageManager::callAsync([safeThis, aliveWeak, result]
            {
                if (aliveWeak.lock() == nullptr)
                    return;

                auto* self = safeThis.getComponent();
                if (self == nullptr)
                    return;

                // Belt-and-braces on the gating argument: while
                // imageLoadInProgress, no chain session or header
                // edit can open, so the install can never land into
                // one (see updatePluginListEnablement()/menuModel).
                jassert(self->pluginChain.empty() && self->headerEditorPanel == nullptr);

                if (result->working == nullptr)
                {
                    self->finishImageLoad("Failed to load image: " + result->errorMessage);
                    return;
                }

                self->originalImage = std::move(result->original);
                self->workingImage = std::move(result->working);
                self->loadedImageBaseName = result->fileBaseName;
                self->undoStack.clear();
                self->redoStack.clear();

                // A freshly-loaded RawImage always defaults to bipolar, but the
                // combo and waveform views are session-long UI state left over
                // from any previous image - reset them to match.
                self->sampleModeCombo.setSelectedId(1, juce::dontSendNotification);
                self->waveformView.setSampleMode(SampleFormat::Mode::bipolar);
                for (auto& view : self->channelWaveformViews)
                    view.setSampleMode(SampleFormat::Mode::bipolar);

                // Split mode is forced off for a new image, same as the
                // sample-mode reset above -- otherwise the channel lanes would
                // keep showing the previous image's planes. Must precede
                // setBuffer() below so its onViewChanged -> syncScrollBarToView()
                // reads the plain waveform as the primary view.
                self->setSplitMode(false);

                // The install-time equivalents of updateWaveform(true)/
                // updatePreview(true), minus the conversion/render costs the
                // job already paid off-thread.
                self->waveformView.setBuffer(std::move(result->waveformSamples), true,
                                             std::move(result->waveformPeaks));
                self->imagePreview.setImage(self->workingImage->toJuceImage(), true);
                self->updateHighlightOverlay(*self->workingImage, self->getCurrentSelectionScope());
                self->updateImageSizeLabel(*self->workingImage);

                self->finishImageLoad("Loaded " + result->fileName + " ("
                                      + juce::String(self->workingImage->pixelBytes.getSize())
                                      + " bytes of pixel data).");
            });
        }
    });
}

namespace
{
    // Same extension set as loadImageClicked()'s FileChooser filter above,
    // checked case-insensitively since a dragged file's extension case isn't
    // guaranteed (unlike the FileChooser, which the OS already filters).
    bool isAcceptableImageFile(const juce::File& file)
    {
        static const juce::StringArray extensions { "bmp", "pnm", "ppm", "pgm", "png", "jpg", "jpeg", "dng", "raf" };
        return extensions.contains(file.getFileExtension().trimCharactersAtStart(".").toLowerCase());
    }
}

bool MainComponent::isInterestedInFileDrag(const juce::StringArray& files)
{
    // Scoped to "no image currently open/loading" -- dropping a file to
    // replace an in-progress edit would be a worse surprise than just not
    // reacting to the drag at all (same reasoning as loadImageClicked()'s
    // Load Image menu item being disabled in those same states); a user who
    // wants to replace the current image can still use Load Image/Reset.
    if (workingImage != nullptr || imageLoadInProgress || headerEditorPanel != nullptr)
        return false;

    for (const auto& path : files)
        if (isAcceptableImageFile(juce::File(path)))
            return true;

    return false;
}

void MainComponent::fileDragEnter(const juce::StringArray&, int, int)
{
    imagePreview.setFileDragHover(true);
}

void MainComponent::fileDragExit(const juce::StringArray&)
{
    imagePreview.setFileDragHover(false);
}

void MainComponent::filesDropped(const juce::StringArray& files, int, int)
{
    imagePreview.setFileDragHover(false);

    for (const auto& path : files)
    {
        juce::File file(path);
        if (isAcceptableImageFile(file))
        {
            loadImageFile(file);
            return;
        }
    }
}

void MainComponent::finishImageLoad(const juce::String& statusText)
{
    imageLoadInProgress = false;
    updatePluginListEnablement();
    menuModel.menuItemsChanged();
    setStatus(statusText);
}

void MainComponent::exportImageClicked()
{
    if (workingImage == nullptr)
    {
        setStatus("Nothing to export - load an image first.");
        return;
    }

    // Export always produces a PNG, regardless of the format the image was
    // loaded from - a real, widely-viewable encoded image rather than a raw
    // BMP/PNM byte reconstruction. Defaults to Documents (or wherever the
    // user last exported to - see exportSettingsStore) and suggests the
    // loaded file's own name with "_modified" appended, rather than a fixed
    // "export.png" that gives no hint which image it came from.
    const auto suggestedName = (loadedImageBaseName.isNotEmpty() ? loadedImageBaseName : "export") + "_modified.png";
    const auto suggestedFile = exportSettingsStore.getLastExportDirectory().getChildFile(suggestedName);

    fileChooser = std::make_unique<juce::FileChooser>("Export image", suggestedFile, "*.png");

    fileChooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
        [this](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File())
                return;

            exportSettingsStore.setLastExportDirectory(file.getParentDirectory());

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

    imagePreview.setImage(workingImage->toJuceImage(), resetView);
    updateHighlightOverlay(*workingImage, getCurrentSelectionScope());
    updateImageSizeLabel(*workingImage);
}

void MainComponent::updateImageSizeLabel(const RawImage& image)
{
    const auto megabytes = (double) image.pixelBytes.getSize() / (1024.0 * 1024.0);

    imageSizeLabel.setText(juce::String(image.getWidth()) + " x " + juce::String(image.getHeight())
                               + "  -  " + juce::String(megabytes, 1) + " MB",
                           juce::dontSendNotification);

    // Same choke-point covers every path where the loaded image's identity
    // can change (load, reset, undo/redo) -- cheap to call redundantly on
    // ordinary in-place refreshes (Apply, selection-highlight redraw) too,
    // since loadedImageBaseName doesn't change then.
    effectChainPanel.setInputLabel(loadedImageBaseName.isNotEmpty() ? loadedImageBaseName : "no image");
}

void MainComponent::updateHighlightOverlay(const RawImage& image, const SelectionScope& scope)
{
    static const juce::Colour channelColours[4] = { juce::Colours::red, juce::Colours::green,
                                                      juce::Colours::blue, juce::Colours::white };
    const auto colour = scope.channel.has_value() ? channelColours[(int) *scope.channel] : juce::Colours::yellow;

    const auto overlay = scope.channel.has_value()
        ? image.computeChannelHighlightOverlay(scope.range)
        : image.computeHighlightOverlay(scope.range);

    if (! overlay.has_value())
    {
        imagePreview.setHighlightRegion(std::nullopt, colour);
        return;
    }

    imagePreview.setHighlightRegion(juce::Range<int>(overlay->topRow, overlay->bottomRow + 1), colour);
}

void MainComponent::updateWaveform(bool resetView)
{
    if (workingImage == nullptr)
        return;

    waveformView.setBuffer(SampleFormat::bytesToBuffer(workingImage->getVisualOrderedPixelBytes(), workingImage->getSampleMode()), resetView);
}
