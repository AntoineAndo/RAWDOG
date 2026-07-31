#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

// Displays an image with pinch-to-zoom (centred on the cursor) and
// two-finger trackpad panning. Double-click resets to fit-the-viewport.
// Deliberately knows nothing about RawImage or "selections" -- the highlight
// overlay is a plain image-space row range, kept generic/reusable; the host
// (MainComponent) is what translates it to/from an actual byte/sample range.
class ZoomableImageView : public juce::Component
{
public:
    // resetView: true resets zoom/pan to fit-the-viewport (a genuinely new image);
    // false keeps the current zoom/pan (an in-place refresh, e.g. after Apply).
    void setImage(juce::Image newImage, bool resetView = true);

    // Image-space row range (not screen space), following the same half-open
    // juce::Range convention as WaveformView::getSelectionSampleRange() --
    // i.e. [firstRow, lastRow + 1). Drawn as a filled rectangle confined to
    // the image's own rendered width, rather than reconstructed per-row
    // geometry -- the hover/drag target is confined to that same width (a
    // click at the right row but outside the image horizontally is not a
    // hit), matching the visible band exactly. paint() transforms its row
    // extent the same way it transforms the image itself, so it always
    // tracks correctly regardless of current zoom/pan. Nullopt clears it.
    // This is far cheaper than a full setImage() call (no pixel data touched
    // at all), so a selection-only change should call this instead of
    // rebuilding the whole image.
    //
    // The rectangle is interactive: dragging an edge resizes it (see
    // onHighlightRegionChanged below), dragging the body moves it, clicking
    // outside it fires onClick (a plain "deselect" gesture the host handles).
    void setHighlightRegion(std::optional<juce::Range<int>> imageRowRange, juce::Colour colour);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void mouseMagnify(const juce::MouseEvent& e, float scaleFactor) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;

    // Fired on a plain click while no image is loaded, so a host can treat the
    // empty preview area as a shortcut into its own "load image" action.
    std::function<void()> onClickWithNoImage;

    // Fired on a plain click while an image IS loaded, outside the highlight
    // rectangle (or when there is none) -- lets a host clear an unrelated
    // selection (e.g. a waveform time-range selection) when the user clicks
    // the image, without this class itself knowing anything about selections.
    // Not fired for a click that lands on the rectangle/its handles -- that's
    // captured as a resize/move drag instead, see onHighlightRegion* below.
    std::function<void()> onClick;

    // Fired once at the start of a highlight-region resize/move drag (mirrors
    // WaveformView::onBeforeSelectionChange) -- lets a host snapshot "before"
    // state for undo before the drag actually changes anything.
    std::function<void()> onHighlightRegionDragStart;

    // Fired on every drag frame while resizing/moving the highlight region,
    // with the new image-row range (same half-open convention as
    // setHighlightRegion's parameter, already clamped to the image's rows).
    // The host owns translating this back into whatever selection
    // representation (byte range / channel-plane sample range) it actually
    // cares about.
    std::function<void(juce::Range<int>)> onHighlightRegionChanged;

    // Trades resample quality for speed while rapid setImage() refreshes are
    // expected (a live-preview session delivering results several times a
    // second): the cachedRender rebuild's drawImageTransformed() uses
    // nearest-neighbour (lowResamplingQuality) instead of the default
    // interpolation, cutting the per-delivery message-thread cost (measured
    // ~21-28ms for a 6240x4160 source -- see PROJECT.md's live-preview
    // performance note). Self-invalidates the cache on change, so quality
    // returns as soon as it's turned back off.
    void setFastResampling(bool shouldUseFastResampling);

    // Purely a visual affordance for a host implementing
    // juce::FileDragAndDropTarget itself (this class knows nothing about
    // files/drag-and-drop) -- while true, the empty-state placeholder is
    // drawn with an emphasized border/text so hovering a dragged file over
    // the preview reads as "drop here", not just as a static hint. No-op
    // once an image is loaded.
    void setFileDragHover(bool isHovering);

private:
    void fitToView();
    void applyZoom(float factor, juce::Point<float> anchorScreenPos);
    juce::AffineTransform getImageToScreenTransform() const;

    // Screen-space bounds of the current highlightRegion, or an empty
    // rectangle if there is none -- shared by paint() and the hit-testing in
    // mouseDown/mouseMove.
    juce::Rectangle<float> getHighlightRegionScreenBounds() const;

    // Image row (clamped to [0, image height - 1]) under a screen-space Y
    // coordinate -- the inverse of getImageToScreenTransform(), used only to
    // update the region during a drag (hit-testing itself forward-transforms
    // the region's known edges into screen space instead, so handleGrabPixels
    // stays a fixed screen-pixel tolerance regardless of zoom level).
    int screenYToImageRow(float screenY) const;

    juce::Image image;
    float scale = 1.0f;
    juce::Point<float> offset;

    // Inclusive row indices, mirroring RawImage::HighlightOverlay's own
    // topRow/bottomRow fields (deliberately NOT a juce::Range<int>, whose
    // half-open convention would make the resize/move clamping below easy to
    // get subtly wrong -- see setHighlightRegion()'s doc comment for the
    // conversion at the public API boundary).
    struct HighlightRegion
    {
        int topRow, bottomRow;
    };
    std::optional<HighlightRegion> highlightRegion;
    juce::Colour highlightColour = juce::Colours::yellow;

    // Only relevant while highlightRegion has a value -- whether the mouse is
    // currently over the rectangle (drives the hover tint in paint()).
    bool hoveringHighlightRegion = false;

    enum class HighlightDragMode { none, movingRegion, resizingTop, resizingBottom };
    HighlightDragMode highlightDragMode = HighlightDragMode::none;
    int dragAnchorRow = 0;       // resize: the fixed (opposite) edge's row
    int dragMoveOffsetRows = 0;  // move: (row under cursor at grab) - topRow at grab
    int dragMoveLengthRows = 0;  // move: region height at grab, held constant while moving
    static constexpr int handleGrabPixels = 6; // hit-test tolerance around each handle, in screen pixels

    // Pre-transformed, viewport-sized (not source-image-sized) render cache.
    // drawImageTransformed() always resamples/composites the FULL source
    // image regardless of clip, which is expensive for a large photo -- so
    // paint() blits this cached bitmap instead of calling it every repaint.
    // Only regenerated when image/scale/offset actually change (see
    // invalidateCachedRender() call sites), never on a highlight-only change,
    // since a highlight-only selection drag was the actual reported slow path.
    void ensureCachedRenderUpToDate();
    void invalidateCachedRender() { cachedRenderValid = false; }
    juce::Image cachedRender;
    bool cachedRenderValid = false;
    bool fastResampling = false;
    bool fileDragHover = false;
};
