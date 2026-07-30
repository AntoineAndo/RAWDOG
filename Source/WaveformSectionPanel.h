#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "RawdogLookAndFeel.h"
#include "WaveformSplitPanel.h"

// Ports today's fixed-pixel waveform sub-layout (a filled toolbar strip, then
// waveform view + horizontal scrollbar row) into its own container, unchanged.
// Not a user-resizable split, just scoped to this panel's local coordinates.
// Also lays out the split-channel toggle button (in the toolbar strip) and
// swaps which of the interleaved waveformViewRef / the 3-lane splitPanel
// occupies the waveform area, based on the toggle's current state —
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

        // Explicit, not just relying on setBounds() below noticing a size
        // change: which lanes are visible inside splitPanel (e.g. the alpha
        // lane) can change without splitPanel's own overall bounds changing.
        splitPanel.resized();
        resized();
    }

    void paint(juce::Graphics& g) override
    {
        const auto& palette = RawdogLookAndFeel::Palette::get();
        g.setColour(palette.windowBg);
        g.fillRect(toolbarBounds);

        // Frames the waveform lane so it reads as a bounded surface (same
        // convention RightColumnPanel uses for the image preview) rather than
        // its white fill bleeding straight into the grey toolbar/scrollbar
        // strips around it. Expanded by 1 so the outline sits just outside
        // the waveform component's own opaque fill rather than being painted
        // over by it.
        g.setColour(palette.ink);
        g.drawRect(waveformBounds.expanded(1), 1);
    }

    void resized() override
    {
        auto area = getLocalBounds();

        // A real toolbar strip (filled background, proper padding) rather than
        // the split toggle sitting bare at the waveform's top-right corner --
        // same filled-header treatment PluginListModel uses for its vendor
        // group headers, for visual consistency between the two panels.
        toolbarBounds = area.removeFromTop(28);
        area.removeFromTop(4);

        auto toolbar = toolbarBounds.reduced(8, 2);
        splitToggleRef.setBounds(toolbar.removeFromLeft(84));

        // Fixed-height scrollbar strip carved off the bottom first, so any
        // extra height the panel gains (dragging the outer resizer bar taller)
        // flows into the waveform view below instead of stretching the
        // scrollbar into an oversized handle.
        auto scrollbarArea = area.removeFromBottom(16);
        area.removeFromBottom(4);

        // No horizontal inset of its own here -- padding lives on the parent
        // RightColumnPanel now, which already insets this whole panel's
        // bounds from the window frame; adding another margin here would
        // double it up.
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
};
