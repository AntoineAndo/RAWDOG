#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <utility>

// Displays an image with pinch-to-zoom (centred on the cursor) and
// two-finger trackpad panning. Double-click resets to fit-the-viewport.
// mouseDown/mouseDrag are currently no-ops, reserved for a future feature.
// Deliberately knows nothing about RawImage or "selections" -- the highlight
// overlay is plain image-space line coordinates, kept generic/reusable.
class ZoomableImageView : public juce::Component
{
public:
    // resetView: true resets zoom/pan to fit-the-viewport (a genuinely new image);
    // false keeps the current zoom/pan (an in-place refresh, e.g. after Apply).
    void setImage(juce::Image newImage, bool resetView = true);

    // Image-space line coordinates (not screen space) -- paint() transforms
    // them the same way it transforms the image itself, so the overlay always
    // tracks correctly regardless of current zoom/pan. Nullopt clears it. This
    // is far cheaper than a full setImage() call (no pixel data touched at
    // all), so a selection-only change should call this instead of rebuilding
    // the whole image.
    void setHighlightLines(std::optional<std::pair<juce::Line<float>, juce::Line<float>>> lines, juce::Colour colour);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void mouseMagnify(const juce::MouseEvent& e, float scaleFactor) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;

    // Fired on a plain click while no image is loaded, so a host can treat the
    // empty preview area as a shortcut into its own "load image" action.
    std::function<void()> onClickWithNoImage;

    // Trades resample quality for speed while rapid setImage() refreshes are
    // expected (a live-preview session delivering results several times a
    // second): the cachedRender rebuild's drawImageTransformed() uses
    // nearest-neighbour (lowResamplingQuality) instead of the default
    // interpolation, cutting the per-delivery message-thread cost (measured
    // ~21-28ms for a 6240x4160 source -- see PROJECT.md's live-preview
    // performance note). Self-invalidates the cache on change, so quality
    // returns as soon as it's turned back off.
    void setFastResampling(bool shouldUseFastResampling);

private:
    void fitToView();
    void applyZoom(float factor, juce::Point<float> anchorScreenPos);
    juce::AffineTransform getImageToScreenTransform() const;

    juce::Image image;
    float scale = 1.0f;
    juce::Point<float> offset;

    std::optional<std::pair<juce::Line<float>, juce::Line<float>>> highlightLines;
    juce::Colour highlightColour = juce::Colours::yellow;

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
};
