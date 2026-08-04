#include "MainComponent.h"
#include "SampleFormat.h"

void MainComponent::openFileModifierClicked()
{
    if (workingImage == nullptr)
        return;

    if (! pluginChain.empty())
    {
        setStatus("Finish with the effect chain first (Apply, or remove every effect).");
        return;
    }

    if (headerEditorPanel != nullptr || fileModifierPanel != nullptr)
        return;

    fileModifierPanel = std::make_unique<FileModifierPanel>(
        [this] { chooseModifierFileClicked(); },
        [this] { refreshFileModifierPreview(); },
        [this] { applyFileModifierClicked(); },
        [this] { cancelFileModifierClicked(); });

    leftColumn.setEditorPanel(fileModifierPanel.get());

    // Re-seed the left/right split to this panel's own preferred width, same
    // as openHeaderEditorClicked() does -- FileModifierPanel isn't a
    // PluginEditorPanel, so LeftColumnPanel::setEditorPanel() won't auto-seed
    // it (see that function's own dynamic_cast comment).
    outerLayout.setItemLayout(0, 200, -0.5, fileModifierPanel->getPreferredWidth());
    resized();

    updatePluginListEnablement();
    menuModel.menuItemsChanged();
}

void MainComponent::chooseModifierFileClicked()
{
    if (fileModifierPanel == nullptr)
        return;

    fileChooser = std::make_unique<juce::FileChooser>("Choose a file to mix into the image", juce::File(), "*");

    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();

            if (fileModifierPanel == nullptr || ! file.existsAsFile())
                return;

            // Staged into a local block first -- a failed read must leave
            // modifierFileBytes (and whatever was last previewed from it)
            // exactly as it was, not silently emptied out from under a
            // still-showing preview of the previous file.
            juce::MemoryBlock loaded;

            if (! file.loadFileAsData(loaded))
            {
                setStatus("Couldn't read " + file.getFileName() + ".");
                return;
            }

            modifierFileBytes = std::move(loaded);
            fileModifierPanel->setModifierFileInfo(file.getFileName(), file.getSize());
            refreshFileModifierPreview();
        });
}

void MainComponent::refreshFileModifierPreview()
{
    if (workingImage == nullptr || fileModifierPanel == nullptr)
        return;

    const auto scope = getCurrentSelectionScope();

    // Keep the highlight line(s) tracking the live selection even before a
    // modifier file has been chosen yet -- handleAsyncUpdate() routes every
    // selection-drag frame through here while this panel is open, the same
    // way it routes through refreshLivePreview() while the chain is open.
    updateHighlightOverlay(*workingImage, scope);

    if (modifierFileBytes.isEmpty())
        return;

    const auto op = fileModifierPanel->getSelectedOperation();
    const auto blend = fileModifierPanel->getBlend();
    const auto scale = fileModifierPanel->getScale();

    if (scope.channel.has_value())
    {
        const auto& source = workingImage->getChannelPlane(*scope.channel);
        const auto range = scope.range.isEmpty() ? juce::Range<int>(0, (int) source.getSize()) : scope.range;

        fileModifierPreviewChannel = scope.channel;
        fileModifierCandidateChannelBytes = FileByteMixer::mixBytes(source, range, modifierFileBytes, op, blend, scale);

        imagePreview.setImage(workingImage->toJuceImageFromBytes(
            workingImage->previewWithChannelBytes(*scope.channel, fileModifierCandidateChannelBytes)), false);

        channelWaveformViews[(size_t) *scope.channel].setBuffer(
            SampleFormat::bytesToBuffer(fileModifierCandidateChannelBytes, workingImage->getSampleMode()), false);
    }
    else
    {
        const auto& source = workingImage->getVisualOrderedPixelBytes();
        const auto range = scope.range.isEmpty() ? juce::Range<int>(0, (int) source.getSize()) : scope.range;

        fileModifierPreviewChannel.reset();
        fileModifierCandidateVisualOrderBytes = FileByteMixer::mixBytes(source, range, modifierFileBytes, op, blend, scale);

        imagePreview.setImage(workingImage->toJuceImageFromBytes(
            workingImage->previewWithVisualOrderedBytes(fileModifierCandidateVisualOrderBytes)), false);

        waveformView.setBuffer(
            SampleFormat::bytesToBuffer(fileModifierCandidateVisualOrderBytes, workingImage->getSampleMode()), false);
    }
}

void MainComponent::applyFileModifierClicked()
{
    const bool haveCandidate = fileModifierPreviewChannel.has_value() ? ! fileModifierCandidateChannelBytes.isEmpty()
                                                                       : ! fileModifierCandidateVisualOrderBytes.isEmpty();

    if (workingImage == nullptr || ! haveCandidate)
        return;

    pushUndoState(); // captures the OLD pixel bytes, before committing

    if (fileModifierPreviewChannel.has_value())
        workingImage->applyChannelBytes(*fileModifierPreviewChannel, fileModifierCandidateChannelBytes);
    else
        workingImage->applyVisualOrderedBytes(fileModifierCandidateVisualOrderBytes);

    setStatus("Applied file modifier.");

    endFileModifierSession();
    updatePreview();
    updateWaveform();
}

void MainComponent::cancelFileModifierClicked()
{
    endFileModifierSession(); // workingImage was never touched
    updatePreview();
    updateWaveform();
    setStatus("Cancelled - image unchanged.");
}

void MainComponent::endFileModifierSession()
{
    leftColumn.setEditorPanel(nullptr);
    fileModifierPanel.reset();
    modifierFileBytes.reset();
    fileModifierCandidateChannelBytes.reset();
    fileModifierCandidateVisualOrderBytes.reset();
    fileModifierPreviewChannel.reset();
    updatePluginListEnablement();
    menuModel.menuItemsChanged();
}
