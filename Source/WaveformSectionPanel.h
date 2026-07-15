#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

// Ports today's fixed-pixel waveform sub-layout (waveform view + vertical zoom
// column + horizontal zoom/scrollbar row) into its own container, unchanged.
// Not a user-resizable split, just scoped to this panel's local coordinates.
class WaveformSectionPanel : public juce::Component
{
public:
    WaveformSectionPanel(juce::Component& waveformViewIn, juce::Slider& waveformZoomSliderIn,
                         juce::Label& waveformZoomLabelIn, juce::Slider& horizontalZoomSliderIn,
                         juce::Component& horizontalScrollBarIn)
        : waveformViewRef(waveformViewIn), waveformZoomSliderRef(waveformZoomSliderIn),
          waveformZoomLabelRef(waveformZoomLabelIn), horizontalZoomSliderRef(horizontalZoomSliderIn),
          horizontalScrollBarRef(horizontalScrollBarIn)
    {
        addAndMakeVisible(waveformViewRef);
        addAndMakeVisible(waveformZoomSliderRef);
        addAndMakeVisible(waveformZoomLabelRef);
        addAndMakeVisible(horizontalZoomSliderRef);
        addAndMakeVisible(horizontalScrollBarRef);
    }

    void resized() override
    {
        auto area = getLocalBounds();

        auto waveformTop = area.removeFromTop(100);
        area.removeFromTop(4);

        auto zoomArea = waveformTop.removeFromRight(40);
        waveformTop.removeFromRight(8);
        waveformZoomLabelRef.setBounds(zoomArea.removeFromTop(16));
        waveformZoomSliderRef.setBounds(zoomArea);
        waveformViewRef.setBounds(waveformTop);

        auto horizontalZoomArea = area.removeFromLeft(120);
        area.removeFromLeft(8);
        horizontalZoomSliderRef.setBounds(horizontalZoomArea);
        horizontalScrollBarRef.setBounds(area);
    }

private:
    juce::Component& waveformViewRef;
    juce::Slider& waveformZoomSliderRef;
    juce::Label& waveformZoomLabelRef;
    juce::Slider& horizontalZoomSliderRef;
    juce::Component& horizontalScrollBarRef;
};
