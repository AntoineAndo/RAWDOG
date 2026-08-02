#include "ZoomableImageView.h"
#include "RawdogLookAndFeel.h"
#include <cmath>

ZoomableImageView::ZoomableImageView()
{
    // A real child, not just a click-anywhere affordance -- reads as an
    // actual call to action, matching the design mockup. Fires the exact
    // same callback the whole-area click already does; no new callback
    // surface needed.
    addAndMakeVisible(chooseFileButton);
    chooseFileButton.onClick = [this] { if (onClickWithNoImage) onClickWithNoImage(); };
}

void ZoomableImageView::setImage(juce::Image newImage, bool resetView)
{
    image = std::move(newImage);
    invalidateCachedRender();
    chooseFileButton.setVisible(! image.isValid());

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
    chooseFileButton.setBounds(getPlaceholderLayout().button);

    if (! image.isValid())
        return;

    // Keep the view sane if the component gets resized before any zoom/pan happened.
    if (scale <= 0.0f)
        fitToView();

    // A resize can leave a previously-fine pan position with the image now
    // entirely outside the (possibly shrunk) viewport.
    clampOffset();
    invalidateCachedRender();
}

juce::Rectangle<int> ZoomableImageView::getCardBounds() const
{
    auto bounds = getLocalBounds();

    // Roughly half the view's own size, centred -- clamped to a sane minimum
    // so the icon/title/subtitle/button block below still fits on a small
    // window rather than the card shrinking past its own content.
    const int width = juce::jmax(240, bounds.getWidth() / 2);
    const int height = juce::jmax(200, bounds.getHeight() / 2);

    return juce::Rectangle<int>(width, height).withCentre(bounds.getCentre());
}

ZoomableImageView::PlaceholderLayout ZoomableImageView::getPlaceholderLayout() const
{
    constexpr int iconSize = 60;
    constexpr int titleHeight = 22;
    constexpr int subtitleHeight = 16;
    constexpr int buttonWidth = 150;
    constexpr int buttonHeight = 28;
    constexpr int gapAfterIcon = 14;
    constexpr int gapAfterTitle = 6;
    constexpr int gapAfterSubtitle = 14;
    constexpr int totalHeight = iconSize + gapAfterIcon + titleHeight + gapAfterTitle
                                + subtitleHeight + gapAfterSubtitle + buttonHeight;

    auto bounds = getCardBounds();
    const int cx = bounds.getCentreX();
    int y = bounds.getCentreY() - totalHeight / 2;

    PlaceholderLayout result;
    result.icon = juce::Rectangle<int>(iconSize, iconSize).withCentre({ cx, y + iconSize / 2 });
    y += iconSize + gapAfterIcon;
    result.title = juce::Rectangle<int>(bounds.getWidth(), titleHeight).withX(bounds.getX()).withY(y);
    y += titleHeight + gapAfterTitle;
    result.subtitle = juce::Rectangle<int>(bounds.getWidth(), subtitleHeight).withX(bounds.getX()).withY(y);
    y += subtitleHeight + gapAfterSubtitle;
    result.button = juce::Rectangle<int>(buttonWidth, buttonHeight).withCentre({ cx, y + buttonHeight / 2 });

    return result;
}

void ZoomableImageView::paint(juce::Graphics& g)
{
    if (! image.isValid())
    {
        // Dot-matted "workspace mat" texture for the area OUTSIDE the dashed
        // card -- same background this view uses once an image is loaded and
        // doesn't fill the viewport (letterboxing), just without an image on
        // top of it yet.
        RawdogLookAndFeel::drawDotMat(g, getLocalBounds());

        const auto& palette = RawdogLookAndFeel::Palette::get();
        const auto layout = getPlaceholderLayout();

        // The card itself: plain white, matching the design mockup -- only
        // the area actually delimited by the dashed border is white, not the
        // whole view. Roughly half the view's size (see getCardBounds()), not
        // a fixed inset from the edges.
        auto dashBounds = getCardBounds().toFloat();
        g.setColour(palette.surface);
        g.fillRect(dashBounds);

        // Dashed inset border framing the drop zone. Thicker/darker while a
        // file is being dragged over, layered onto the same default look
        // rather than swapping to a wholly different one.
        juce::Path outline;
        outline.addRectangle(dashBounds);
        juce::Path dashedOutline;
        const float dashLengths[] = { 6.0f, 4.0f };
        juce::PathStrokeType(fileDragHover ? 2.0f : 1.0f).createDashedStroke(dashedOutline, outline, dashLengths, 2);
        g.setColour(fileDragHover ? palette.ink : palette.divider);
        g.fillPath(dashedOutline);

        // Small generic document icon: bordered rect, folded top-right
        // corner, dot-matted "content" patch -- evokes an image file without
        // needing a bundled icon asset.
        {
            auto iconBounds = layout.icon.toFloat();
            g.setColour(palette.surface);
            g.fillRect(iconBounds);
            g.setColour(palette.ink);
            g.drawRect(iconBounds, 1.5f);

            constexpr float foldSize = 14.0f;
            juce::Path fold;
            fold.startNewSubPath(iconBounds.getRight() - foldSize, iconBounds.getY());
            fold.lineTo(iconBounds.getRight(), iconBounds.getY() + foldSize);
            fold.lineTo(iconBounds.getRight() - foldSize, iconBounds.getY() + foldSize);
            fold.closeSubPath();
            g.setColour(palette.windowBg);
            g.fillPath(fold);
            g.setColour(palette.ink);
            g.strokePath(fold, juce::PathStrokeType(1.0f));

            auto contentPatch = iconBounds.reduced(8.0f).withTrimmedTop(foldSize);
            RawdogLookAndFeel::drawDotMat(g, contentPatch.toNearestInt());
            g.setColour(palette.ink);
            g.drawRect(contentPatch, 1.0f);
        }

        g.setColour(fileDragHover ? palette.ink : palette.inkMuted);
        g.setFont(RawdogLookAndFeel::chromeFont(13.0f));
        g.drawText(fileDragHover ? "DROP TO LOAD" : "DROP AN IMAGE HERE", layout.title, juce::Justification::centred);

        // The app's actual supported formats -- deliberately not copying the
        // design mockup's "PNG, JPEG, TIFF or BMP" verbatim: this app does
        // not support TIFF (see RawImage's deferred-TIFF note) and does
        // support PNM/PPM/PGM and RAF/DNG camera raw (same extension set as
        // MainComponentImageIO.cpp's isAcceptableImageFile()/FileChooser
        // wildcard).
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.setColour(palette.inkMuted);
        g.drawText("BMP, PNM, PNG, JPEG, RAF or DNG", layout.subtitle, juce::Justification::centred);
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
        // pill, full width.
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

void ZoomableImageView::setFileDragHover(bool isHovering)
{
    if (fileDragHover == isHovering)
        return;

    fileDragHover = isHovering;

    if (! image.isValid())
        repaint();
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
    clampOffset();
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
            // sits at (or near) row 0.
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
    clampOffset();

    invalidateCachedRender();
    repaint();
}

void ZoomableImageView::clampOffset()
{
    if (! image.isValid() || getWidth() == 0 || getHeight() == 0)
        return;

    const float imageW = (float) image.getWidth() * scale;
    const float imageH = (float) image.getHeight() * scale;
    const float viewW = (float) getWidth();
    const float viewH = (float) getHeight();

    // However far the image is panned/zoomed, at least this many pixels of it
    // must stay inside the viewport on every side -- enough to still see (and
    // grab) an edge to pan back from, without being so large it feels like
    // the image is stuck.
    constexpr float minVisible = 60.0f;

    // offset.x is the image's left edge in screen space; offset.x + imageW is
    // its right edge. Bounding the left edge to <= viewW - minVisible keeps it
    // from sliding past the right edge of the viewport; bounding it to
    // >= minVisible - imageW keeps the right edge from sliding past the left.
    // jmin/jmax (rather than assuming low <= high) guards the case where the
    // image is small enough relative to the viewport that these two
    // constraints would otherwise cross.
    const float minOffsetX = minVisible - imageW;
    const float maxOffsetX = viewW - minVisible;
    offset.x = juce::jlimit(juce::jmin(minOffsetX, maxOffsetX), juce::jmax(minOffsetX, maxOffsetX), offset.x);

    const float minOffsetY = minVisible - imageH;
    const float maxOffsetY = viewH - minVisible;
    offset.y = juce::jlimit(juce::jmin(minOffsetY, maxOffsetY), juce::jmax(minOffsetY, maxOffsetY), offset.y);
}
