#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Displays an image with mouse-wheel zoom (centred on the cursor) and
// click-drag panning. Double-click resets to fit-the-viewport.
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
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;

private:
    void fitToView();
    void applyZoom(float factor, juce::Point<float> anchorScreenPos);

    juce::Image image;
    float scale = 1.0f;
    juce::Point<float> offset;

    juce::Point<float> dragStart;
    juce::Point<float> offsetAtDragStart;
};
