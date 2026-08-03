#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "RawdogLookAndFeel.h"
#include "WaveformSplitPanel.h"

namespace
{
    juce::String emDashChar() { return juce::String(juce::CharPointer_UTF8("\xE2\x80\x94")); }
}

// Fixed-pixel waveform sub-layout: a filled toolbar strip, then waveform view
// + horizontal scrollbar row, scoped to this panel's local coordinates (not a
// user-resizable split). Also lays out the split-channel toggle button and
// swaps which of the interleaved waveformViewRef / the 3-lane splitPanel
// occupies the waveform area, based on the toggle's current state.
// MainComponent owns the splitModeToggle button and drives its .onClick/
// business logic directly; this class only reflects whichever state the
// toggle is already in.
class WaveformSectionPanel : public juce::Component
{
public:
    WaveformSectionPanel(juce::Component& waveformViewIn,
                         juce::Component& horizontalScrollBarIn,
                         juce::Component& redWaveformIn, juce::Component& greenWaveformIn,
                         juce::Component& blueWaveformIn, juce::Component& alphaWaveformIn,
                         juce::Button& splitToggleIn)
        : waveformViewRef(waveformViewIn),
          horizontalScrollBarRef(horizontalScrollBarIn),
          splitPanel(redWaveformIn, greenWaveformIn, blueWaveformIn, alphaWaveformIn), splitToggleRef(splitToggleIn)
    {
        addAndMakeVisible(waveformViewRef);
        addAndMakeVisible(splitPanel);
        addAndMakeVisible(horizontalScrollBarRef);
        addAndMakeVisible(splitToggleRef);

        modeLabel.setFont(RawdogLookAndFeel::chromeFont(8.0f));
        modeLabel.setColour(juce::Label::textColourId, RawdogLookAndFeel::Palette::get().inkMuted);
        addAndMakeVisible(modeLabel);

        updateSplitVisibility();
    }

    // modeLabel's textColourId (set once in the constructor above) is cached
    // on the Label rather than looked up from the LookAndFeel per paint, so a
    // theme switch needs this reapplied explicitly.
    void lookAndFeelChanged() override
    {
        modeLabel.setColour(juce::Label::textColourId, RawdogLookAndFeel::Palette::get().inkMuted);
    }

    // Called by MainComponent (via RightColumnPanel's forwarding method)
    // whenever splitModeToggle's state may have changed — reads the toggle's
    // current state and swaps which of waveformViewRef/splitPanel is shown.
    void updateSplitVisibility()
    {
        const bool split = splitToggleRef.getToggleState();
        waveformViewRef.setVisible(! split);
        splitPanel.setVisible(split);

        // "A" only shown when this image has a real alpha lane.
        modeLabel.setText(split ? juce::String("SPLIT ") + emDashChar() + " R / G / B"
                                    + (splitPanel.hasVisibleAlphaLane() ? " / A" : "")
                                 : "RGB INTERLEAVED",
                           juce::dontSendNotification);

        // Lane visibility inside splitPanel (e.g. the alpha lane) can change
        // without splitPanel's own overall bounds changing, so force a relayout.
        splitPanel.resized();
        resized();
    }

    void paint(juce::Graphics& g) override
    {
        const auto& palette = RawdogLookAndFeel::Palette::get();
        g.setColour(palette.windowBg);
        g.fillRect(toolbarBounds);

        // Expanded by 1 so the outline sits just outside the waveform
        // component's own opaque fill rather than being painted over by it.
        g.setColour(palette.ink);
        g.drawRect(waveformBounds.expanded(1), 1);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        area.removeFromTop(6); 

        // toolbarBounds stays full-width for paint()'s background fill;
        // hPadding below insets its content, along with the waveform/
        // scrollbar, so all three line up on the same left/right edge.
        constexpr int hPadding = 10;
        toolbarBounds = area.removeFromTop(32);
        auto toolbar = toolbarBounds.reduced(hPadding, 6);
        splitToggleRef.setBounds(toolbar.removeFromLeft(84));
        toolbar.removeFromLeft(8);
        modeLabel.setBounds(toolbar);

        area.removeFromTop(6); 
        auto scrollbarArea = area.removeFromBottom(16);
        area.removeFromBottom(4);
        area = area.reduced(hPadding, 0);
        scrollbarArea = scrollbarArea.reduced(hPadding, 0);

        waveformBounds = area;
        waveformViewRef.setBounds(area);
        splitPanel.setBounds(area);

        horizontalScrollBarRef.setBounds(scrollbarArea);
    }

private:
    juce::Rectangle<int> toolbarBounds;
    juce::Rectangle<int> waveformBounds;
    juce::Component& waveformViewRef;
    juce::Component& horizontalScrollBarRef;
    WaveformSplitPanel splitPanel;
    juce::Button& splitToggleRef;
    juce::Label modeLabel;
};
