#include "ZoomableImageView.h"
#include "RawdogLookAndFeel.h"
#include <cmath>

void ZoomableImageView::setImage(juce::Image newImage, bool resetView)
{
    image = std::move(newImage);
    invalidateCachedRender();

    if (resetView)
        fitToView();

    repaint();
}

void ZoomableImageView::setHighlightRegion(std::optional<juce::Range<int>> imageRowRange, juce::Colour colour)
{
    highlightRegion = imageRowRange.has_value()
        ? std::optional<HighlightRegion>({ imageRowRange->getStart(), imageRowRange->getEnd() - 1 })
        : std::nullopt;
    highlightColour = colour;

    if (! highlightRegion.has_value())
    {
        hoveringHighlightRegion = false;
        highlightDragMode = HighlightDragMode::none;
    }

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
        RawdogLookAndFeel::drawDotMat(g, getLocalBounds());
        g.setColour(RawdogLookAndFeel::Palette::get().inkMuted);
        g.drawText("Click to load an image", getLocalBounds(), juce::Justification::centred);
        return;
    }

    ensureCachedRenderUpToDate();
    g.drawImageAt(cachedRender, 0, 0);

    if (highlightRegion.has_value())
    {
        const auto screenRect = getHighlightRegionScreenBounds();
        const bool activeTint = hoveringHighlightRegion || highlightDragMode != HighlightDragMode::none;

        g.setColour(highlightColour.withAlpha(activeTint ? 0.14f : 0.0f));
        g.fillRect(screenRect);
        g.setColour(highlightColour.withAlpha(activeTint ? 0.9f : 0.6f));
        g.drawRect(screenRect, 1.5f);

        // Handle bars at each edge: a thicker line plus a small centred grip
        // pill, full width -- the vertical-drag equivalent of WaveformView's
        // own selection-handle grip marks.
        constexpr float pillWidth = 28.0f, pillHeight = 5.0f;
        const float cx = screenRect.getCentreX();

        for (const float y : { screenRect.getY(), screenRect.getBottom() })
        {
            g.setColour(highlightColour.withAlpha(activeTint ? 0.9f : 0.6f));
            g.drawLine(screenRect.getX(), y, screenRect.getRight(), y, 2.0f);

            auto pill = juce::Rectangle<float>(pillWidth, pillHeight).withCentre({ cx, y });
            g.setColour(juce::Colours::white);
            g.fillRoundedRectangle(pill, pillHeight * 0.5f);
            g.setColour(RawdogLookAndFeel::Palette::get().ink);
            g.drawRoundedRectangle(pill, pillHeight * 0.5f, 1.0f);
        }
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
    // Halftone mat, not a flat fill -- shows through as letterboxing wherever
    // the image doesn't fill the viewport (fit-to-view margins, zoomed-out
    // pan).
    RawdogLookAndFeel::drawDotMat(cg, cachedRender.getBounds());

    const auto& palette = RawdogLookAndFeel::Palette::get();
    const auto imageRect = juce::Rectangle<float>(0.0f, 0.0f, (float) image.getWidth(), (float) image.getHeight())
                                .transformedBy(getImageToScreenTransform());

    // Hard offset drop shadow behind the image canvas itself -- not the whole
    // dot-matted viewport, which can be larger than the image at less than
    // 100% zoom -- matching the mockup's 1-bit `2px 2px 0 #000` shadow
    // convention. Drawn before the image (and re-baked here, not in paint(),
    // since it must track the image's current screen position/scale) so the
    // image itself, drawn next, covers all but the peeking bottom-right edge.
    constexpr float shadowOffset = 4.0f;
    cg.setColour(palette.ink);
    cg.fillRect(imageRect.translated(shadowOffset, shadowOffset));

    // Nearest-neighbour while a live-preview session is delivering rapid
    // refreshes -- see setFastResampling()'s doc comment. On macOS this maps
    // to kCGInterpolationNone (juce_CoreGraphicsContext_mac.mm), which reads
    // only ~one source pixel per destination pixel instead of interpolating
    // across the full-resolution source.
    if (fastResampling)
        cg.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);

    cg.drawImageTransformed(image, getImageToScreenTransform(), false);

    // Frames the image canvas so it reads as a bounded surface rather than
    // bleeding straight into the dot-matted mat around it.
    cg.setColour(palette.ink);
    cg.drawRect(imageRect, 1.0f);

    cachedRenderValid = true;
}

juce::AffineTransform ZoomableImageView::getImageToScreenTransform() const
{
    return juce::AffineTransform::scale(scale).translated(offset.x, offset.y);
}

juce::Rectangle<float> ZoomableImageView::getHighlightRegionScreenBounds() const
{
    if (! highlightRegion.has_value())
        return {};

    // Confined to the image's own rendered width (not the full component) --
    // both the visual band AND the hit-test in mouseDown/mouseMove (which
    // checks e.position.x against this same rect) should stop at the image's
    // actual edges, not the wider preview panel.
    const juce::Rectangle<float> imageRect(0.0f, (float) highlightRegion->topRow,
                                            (float) image.getWidth(),
                                            (float) (highlightRegion->bottomRow - highlightRegion->topRow + 1));
    return imageRect.transformedBy(getImageToScreenTransform());
}

int ZoomableImageView::screenYToImageRow(float screenY) const
{
    juce::Point<float> point(0.0f, screenY);
    point.applyTransform(getImageToScreenTransform().inverted());
    return juce::jlimit(0, juce::jmax(0, image.getHeight() - 1), (int) std::floor(point.y));
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

void ZoomableImageView::mouseDown(const juce::MouseEvent& e)
{
    if (! image.isValid())
    {
        if (onClickWithNoImage != nullptr)
            onClickWithNoImage();
        return;
    }

    if (highlightRegion.has_value())
    {
        const auto screenRect = getHighlightRegionScreenBounds();

        // Horizontally confined to the image's own bounds -- unlike the y
        // checks below, this IS an x test: the rectangle (and its
        // interactive target) shouldn't grab a click that's outside the
        // image just because it's at the same row level as the selection.
        const bool withinImageX = e.position.x >= screenRect.getX() && e.position.x <= screenRect.getRight();

        if (withinImageX && std::abs(e.position.y - screenRect.getY()) <= (float) handleGrabPixels)
        {
            if (onHighlightRegionDragStart != nullptr)
                onHighlightRegionDragStart();

            highlightDragMode = HighlightDragMode::resizingTop;
            dragAnchorRow = highlightRegion->bottomRow;
            repaint();
            return;
        }

        if (withinImageX && std::abs(e.position.y - screenRect.getBottom()) <= (float) handleGrabPixels)
        {
            if (onHighlightRegionDragStart != nullptr)
                onHighlightRegionDragStart();

            highlightDragMode = HighlightDragMode::resizingBottom;
            dragAnchorRow = highlightRegion->topRow;
            repaint();
            return;
        }

        if (withinImageX && e.position.y > screenRect.getY() && e.position.y < screenRect.getBottom())
        {
            if (onHighlightRegionDragStart != nullptr)
                onHighlightRegionDragStart();

            highlightDragMode = HighlightDragMode::movingRegion;
            dragMoveLengthRows = highlightRegion->bottomRow - highlightRegion->topRow;
            dragMoveOffsetRows = screenYToImageRow(e.position.y) - highlightRegion->topRow;
            repaint();
            return;
        }
    }

    highlightDragMode = HighlightDragMode::none;

    if (onClick != nullptr)
        onClick();
}

void ZoomableImageView::mouseDrag(const juce::MouseEvent& e)
{
    if (highlightDragMode == HighlightDragMode::none || ! highlightRegion.has_value())
        return;

    const int lastRow = juce::jmax(0, image.getHeight() - 1);
    const int cursorRow = screenYToImageRow(e.position.y);

    switch (highlightDragMode)
    {
        case HighlightDragMode::resizingTop:
        {
            // jmax guards against an inverted jlimit range when the anchor
            // sits at (or near) row 0 -- same rationale as WaveformView's
            // resizingLeft guard.
            const int upperBound = juce::jmax(0, dragAnchorRow - 1);
            highlightRegion->topRow = juce::jlimit(0, upperBound, cursorRow);
            highlightRegion->bottomRow = dragAnchorRow;
            break;
        }

        case HighlightDragMode::resizingBottom:
        {
            const int lowerBound = juce::jmin(lastRow, dragAnchorRow + 1);
            highlightRegion->bottomRow = juce::jlimit(lowerBound, lastRow, cursorRow);
            highlightRegion->topRow = dragAnchorRow;
            break;
        }

        case HighlightDragMode::movingRegion:
        {
            const int upperBound = juce::jmax(0, lastRow - dragMoveLengthRows);
            const int newTop = juce::jlimit(0, upperBound, cursorRow - dragMoveOffsetRows);
            highlightRegion->topRow = newTop;
            highlightRegion->bottomRow = newTop + dragMoveLengthRows;
            break;
        }

        case HighlightDragMode::none:
        default:
            return;
    }

    if (onHighlightRegionChanged != nullptr)
        onHighlightRegionChanged({ highlightRegion->topRow, highlightRegion->bottomRow + 1 });

    repaint();
}

void ZoomableImageView::mouseUp(const juce::MouseEvent&)
{
    highlightDragMode = HighlightDragMode::none;
    repaint();
}

void ZoomableImageView::mouseMove(const juce::MouseEvent& e)
{
    if (! image.isValid() || ! highlightRegion.has_value())
    {
        setMouseCursor(juce::MouseCursor::NormalCursor);

        if (hoveringHighlightRegion)
        {
            hoveringHighlightRegion = false;
            repaint();
        }

        return;
    }

    const auto screenRect = getHighlightRegionScreenBounds();
    const bool withinImageX = e.position.x >= screenRect.getX() && e.position.x <= screenRect.getRight();
    const bool onEdge = withinImageX
                        && (std::abs(e.position.y - screenRect.getY()) <= (float) handleGrabPixels
                            || std::abs(e.position.y - screenRect.getBottom()) <= (float) handleGrabPixels);
    const bool inBody = withinImageX && e.position.y > screenRect.getY() && e.position.y < screenRect.getBottom();

    if (onEdge)
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    else if (inBody)
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    else
        setMouseCursor(juce::MouseCursor::NormalCursor);

    const bool nowHovering = onEdge || inBody;
    if (nowHovering != hoveringHighlightRegion)
    {
        hoveringHighlightRegion = nowHovering;
        repaint();
    }
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
