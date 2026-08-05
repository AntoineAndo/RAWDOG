#include "MainComponent.h"
#include "SampleFormat.h"

void MainComponent::openHeaderEditorClicked()
{
    if (workingImage == nullptr || workingImage->getFormat() != RawImage::Format::bmp)
    {
        setStatus("Header editing is only available for BMP images.");
        return;
    }

    if (! pluginChain.empty())
    {
        setStatus("Finish with the effect chain first (Apply, or remove every effect).");
        return;
    }

    // The entry point needs its own guard even though the menu item is
    // already disabled while a panel is open -- mirrors the same three-way
    // check in openFileModifierClicked().
    if (headerEditorPanel != nullptr || fileModifierPanel != nullptr)
        return;

    headerEditScratch = std::make_unique<RawImage>(*workingImage);

    const auto initialFields = workingImage->getBmpHeaderFields();

    headerEditorPanel = std::make_unique<HeaderEditorPanel>(
        initialFields,
        [this](const RawImage::BmpEditableHeaderFields& candidate) { refreshHeaderLivePreview(candidate); },
        [this] { applyHeaderEditClicked(); },
        [this] { cancelHeaderEditClicked(); });

    leftColumn.setEditorPanel(headerEditorPanel.get());

    // Re-seed the left/right split to this panel's own (much narrower than a
    // typical plugin editor) preferred width; the resizer bar re-drives it
    // from here without re-seeding on ordinary resizes.
    outerLayout.setItemLayout(0, 200, -0.5, headerEditorPanel->getPreferredWidth());
    resized();

    updatePluginListEnablement();
    menuModel.menuItemsChanged();

    // Validate + preview the unedited starting state immediately, so the
    // panel opens already showing a valid preview rather than a blank one.
    RawImage::BmpEditableHeaderFields initialCandidate;
    initialCandidate.bfOffBits = initialFields.bfOffBits;
    initialCandidate.biWidth = initialFields.biWidth;
    initialCandidate.biHeight = initialFields.biHeight;
    initialCandidate.biBitCount = initialFields.biBitCount;
    initialCandidate.biCompression = initialFields.biCompression;
    refreshHeaderLivePreview(initialCandidate);
}

void MainComponent::refreshHeaderLivePreview(const RawImage::BmpEditableHeaderFields& candidate)
{
    if (headerEditScratch == nullptr || headerEditorPanel == nullptr)
        return;

    auto validation = headerEditScratch->validateBmpHeaderFields(candidate);
    headerEditorPanel->showValidation(validation);

    if (! validation.ok)
        return; // keep showing the last good preview frame; Apply stays disabled

    headerEditScratch->applyBmpHeaderFields(candidate);

    // Rebuild the waveform first, on every field edit, not just when bfOffBits
    // moves the header/pixel boundary: getVisualOrderedPixelBytes()'s
    // reordering depends on width/rowStride/bottomUp too, so even a
    // width/height/bitcount edit alone (bfOffBits unchanged) can change the
    // correct visual-order layout despite pixelBytes' own content/length
    // staying the same. Done before reading back the selection below, so a
    // selection active during such an edit is re-clamped to the new sample
    // count first (setBuffer(..., resetView=false)) rather than the image
    // highlight momentarily using a stale, pre-edit-geometry range.
    waveformView.setBuffer(SampleFormat::bytesToBuffer(headerEditScratch->getVisualOrderedPixelBytes(), headerEditScratch->getSampleMode()), false);

    // The image preview must re-render on every field edit -- all 5 fields
    // affect how pixelBytes is interpreted even when its content is untouched.
    imagePreview.setImage(headerEditScratch->toJuceImage(), false);
    // No channel ever applies here -- split mode is force-disabled during header editing.
    updateHighlightOverlay(*headerEditScratch, { std::nullopt, waveformView.getSelectionSampleRange() });
    updateImageSizeLabel(*headerEditScratch); // width/height/bitcount are directly editable here
}

void MainComponent::applyHeaderEditClicked()
{
    if (workingImage == nullptr || headerEditScratch == nullptr)
        return;

    pushUndoState(); // captures the OLD header+pixels, before committing

    workingImage = std::move(headerEditScratch); // already reflects the last-previewed candidate
    setStatus("Applied header edits.");

    endHeaderEditSession();
    updatePreview();
    updateWaveform();
}

void MainComponent::cancelHeaderEditClicked()
{
    headerEditScratch.reset(); // workingImage was never touched
    endHeaderEditSession();
    updatePreview();
    updateWaveform();
    setStatus("Cancelled - header unchanged.");
}

void MainComponent::endHeaderEditSession()
{
    leftColumn.setEditorPanel(nullptr);
    headerEditorPanel.reset();
    headerEditScratch.reset(); // no-op if already moved-from by applyHeaderEditClicked()
    updatePluginListEnablement();
    menuModel.menuItemsChanged();
}
