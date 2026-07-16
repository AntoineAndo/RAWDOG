#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "WaveformSplitPanel.h"

// Ports today's fixed-pixel waveform sub-layout (waveform view + vertical zoom
// column + horizontal zoom/scrollbar row) into its own container, unchanged.
// Not a user-resizable split, just scoped to this panel's local coordinates.
// Also lays out the split-channel toggle button and swaps which of the
// interleaved waveformViewRef / the 3-lane splitPanel occupies the waveform
// area, based on the toggle's current state — MainComponent owns the
// splitModeToggle button and drives its .onClick/business logic directly
// (same convention as the zoom sliders below); this class only reflects
// whichever state the toggle is already in.
class WaveformSectionPanel : public juce::Component
{
public:
    WaveformSectionPanel(juce::Component& waveformViewIn, juce::Slider& waveformZoomSliderIn,
                         juce::Label& waveformZoomLabelIn, juce::Slider& horizontalZoomSliderIn,
                         juce::Component& horizontalScrollBarIn,
                         juce::Component& redWaveformIn, juce::Component& greenWaveformIn,
                         juce::Component& blueWaveformIn, juce::Button& splitToggleIn)
        : waveformViewRef(waveformViewIn), waveformZoomSliderRef(waveformZoomSliderIn),
          waveformZoomLabelRef(waveformZoomLabelIn), horizontalZoomSliderRef(horizontalZoomSliderIn),
          horizontalScrollBarRef(horizontalScrollBarIn),
          splitPanel(redWaveformIn, greenWaveformIn, blueWaveformIn), splitToggleRef(splitToggleIn)
    {
        addAndMakeVisible(waveformViewRef);
        addAndMakeVisible(splitPanel);
        addAndMakeVisible(waveformZoomSliderRef);
        addAndMakeVisible(waveformZoomLabelRef);
        addAndMakeVisible(horizontalZoomSliderRef);
        addAndMakeVisible(horizontalScrollBarRef);
        addAndMakeVisible(splitToggleRef);

        updateSplitVisibility();
    }

    // Called by MainComponent (via RightColumnPanel's forwarding method)
    // whenever splitModeToggle's state may have changed — reads the toggle's
    // current state and swaps which of waveformViewRef/splitPanel is shown.
    void updateSplitVisibility()
    {
        const bool split = splitToggleRef.getToggleState();
        waveformViewRef.setVisible(! split);
        splitPanel.setVisible(split);
        resized();
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

        auto toggleArea = waveformTop.removeFromRight(84);
        waveformTop.removeFromRight(8);
        splitToggleRef.setBounds(toggleArea.removeFromTop(24));

        waveformViewRef.setBounds(waveformTop);
        splitPanel.setBounds(waveformTop);

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
    WaveformSplitPanel splitPanel;
    juce::Button& splitToggleRef;
};
