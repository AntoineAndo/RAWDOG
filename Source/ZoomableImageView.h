#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Displays an image with pinch-to-zoom (centred on the cursor) and
// two-finger trackpad panning. Double-click resets to fit-the-viewport.
// mouseDown/mouseDrag are currently no-ops, reserved for a future feature.
class ZoomableImageView : public juce::Component
{
public:
    // resetView: true resets zoom/pan to fit-the-viewport (a genuinely new image);
    // false keeps the current zoom/pan (an in-place refresh, e.g. after Apply or
    // while the waveform selection highlight is being redrawn).
    void setImage(juce::Image newImage, bool resetView = true);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void mouseMagnify(const juce::MouseEvent& e, float scaleFactor) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;

private:
    void fitToView();
    void applyZoom(float factor, juce::Point<float> anchorScreenPos);

    juce::Image image;
    float scale = 1.0f;
    juce::Point<float> offset;
};
