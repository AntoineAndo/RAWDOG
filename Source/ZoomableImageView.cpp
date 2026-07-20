#include "ZoomableImageView.h"
#include "PixelBenderLookAndFeel.h"

void ZoomableImageView::setImage(juce::Image newImage, bool resetView)
{
    image = std::move(newImage);
    invalidateCachedRender();

    if (resetView)
        fitToView();

    repaint();
}

void ZoomableImageView::setHighlightLines(std::optional<std::pair<juce::Line<float>, juce::Line<float>>> lines, juce::Colour colour)
{
    highlightLines = lines;
    highlightColour = colour;
    repaint();
}

void ZoomableImageView::setFastResampling(bool shouldUseFastResampling)
{
    if (fastResampling == shouldUseFastResampling)
        return;

    fastResampling = shouldUseFastResampling;
    invalidateCachedRender();
    repaint();
}

void ZoomableImageView::resized()
{
    if (! image.isValid())
        return;

    // Keep the view sane if the component gets resized before any zoom/pan happened.
    if (scale <= 0.0f)
        fitToView();
}

void ZoomableImageView::paint(juce::Graphics& g)
{
    if (! image.isValid())
    {
        const auto& palette = PixelBenderLookAndFeel::Palette::get();
        g.fillAll(palette.background);
        g.setColour(palette.textSecondary);
        g.drawText("Click to load an image", getLocalBounds(), juce::Justification::centred);
        return;
    }

    ensureCachedRenderUpToDate();
    g.drawImageAt(cachedRender, 0, 0);

    if (highlightLines.has_value())
    {
        const auto transform = getImageToScreenTransform();
        auto top = highlightLines->first;
        auto bottom = highlightLines->second;
        top.applyTransform(transform);
        bottom.applyTransform(transform);

        g.setColour(highlightColour);
        g.drawLine(top, 2.0f);
        g.drawLine(bottom, 2.0f);
    }
}

void ZoomableImageView::ensureCachedRenderUpToDate()
{
    const int w = juce::jmax(1, getWidth());
    const int h = juce::jmax(1, getHeight());

    if (cachedRenderValid && cachedRender.getWidth() == w && cachedRender.getHeight() == h)
        return;

    // Only reallocate when the size actually changed -- reuse the same
    // juce::Image object across same-size regenerations (e.g. repeated
    // zoom/pan frames at a stable viewport size) to avoid a fresh
    // malloc + CGBitmapContextCreate on every one of those too.
    if (cachedRender.getWidth() != w || cachedRender.getHeight() != h)
        cachedRender = juce::Image(juce::Image::RGB, w, h, false);

    juce::Graphics cg(cachedRender);
    cg.fillAll(PixelBenderLookAndFeel::Palette::get().background); // letterboxing when the image doesn't fill the viewport

    // Nearest-neighbour while a live-preview session is delivering rapid
    // refreshes -- see setFastResampling()'s doc comment. On macOS this maps
    // to kCGInterpolationNone (juce_CoreGraphicsContext_mac.mm), which reads
    // only ~one source pixel per destination pixel instead of interpolating
    // across the full-resolution source.
    if (fastResampling)
        cg.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);

    cg.drawImageTransformed(image, getImageToScreenTransform(), false);
    cachedRenderValid = true;
}

juce::AffineTransform ZoomableImageView::getImageToScreenTransform() const
{
    return juce::AffineTransform::scale(scale).translated(offset.x, offset.y);
}

void ZoomableImageView::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (! image.isValid())
        return;

    // JUCE reports two-finger trackpad scroll/pan gestures through wheel deltas
    // normalised to roughly [-1, 1] per tick; scale up to pixels for a natural pan speed.
    constexpr float panPixelsPerUnitDelta = 1000.0f;
    offset += juce::Point<float>(wheel.deltaX, wheel.deltaY) * panPixelsPerUnitDelta;
    invalidateCachedRender();
    repaint();
}

void ZoomableImageView::mouseMagnify(const juce::MouseEvent& e, float scaleFactor)
{
    if (! image.isValid())
        return;

    applyZoom(scaleFactor, e.position);
}

void ZoomableImageView::mouseDown(const juce::MouseEvent&)
{
    if (! image.isValid() && onClickWithNoImage != nullptr)
        onClickWithNoImage();
}

void ZoomableImageView::mouseDrag(const juce::MouseEvent&)
{
}

void ZoomableImageView::mouseDoubleClick(const juce::MouseEvent&)
{
    fitToView();
    repaint();
}

void ZoomableImageView::fitToView()
{
    invalidateCachedRender();

    if (! image.isValid() || getWidth() == 0 || getHeight() == 0)
    {
        scale = 1.0f;
        offset = {};
        return;
    }

    const float fitScale = juce::jmin((float) getWidth() / (float) image.getWidth(),
                                       (float) getHeight() / (float) image.getHeight());

    scale = fitScale;
    offset = { ((float) getWidth() - (float) image.getWidth() * scale) * 0.5f,
               ((float) getHeight() - (float) image.getHeight() * scale) * 0.5f };
}

void ZoomableImageView::applyZoom(float factor, juce::Point<float> anchorScreenPos)
{
    const float newScale = juce::jlimit(0.05f, 40.0f, scale * factor);
    const auto imagePointUnderCursor = (anchorScreenPos - offset) / scale;

    scale = newScale;
    offset = anchorScreenPos - imagePointUnderCursor * scale;

    invalidateCachedRender();
    repaint();
}
