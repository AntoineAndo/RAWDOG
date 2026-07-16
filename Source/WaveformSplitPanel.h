#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

// Purely presentational: stacks 3 channel waveform lanes vertically with a
// small colour-coded label identifying each. Holds no selection/business
// logic of its own — MainComponent drives each WaveformView's buffer and
// selection directly; this just parents and lays them out, same "dumb
// container" convention as WaveformSectionPanel/LeftColumnPanel/etc.
class WaveformSplitPanel : public juce::Component
{
public:
    WaveformSplitPanel(juce::Component& redViewIn, juce::Component& greenViewIn, juce::Component& blueViewIn)
        : redView(redViewIn), greenView(greenViewIn), blueView(blueViewIn)
    {
        addAndMakeVisible(redView);
        addAndMakeVisible(greenView);
        addAndMakeVisible(blueView);

        redLabel.setColour(juce::Label::textColourId, juce::Colours::red);
        greenLabel.setColour(juce::Label::textColourId, juce::Colours::green);
        blueLabel.setColour(juce::Label::textColourId, juce::Colours::blue);
        addAndMakeVisible(redLabel);
        addAndMakeVisible(greenLabel);
        addAndMakeVisible(blueLabel);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        const int laneHeight = area.getHeight() / 3;

        auto layoutLane = [&] (juce::Component& view, juce::Label& label)
        {
            auto lane = area.removeFromTop(laneHeight);
            label.setBounds(lane.removeFromLeft(20));
            view.setBounds(lane);
        };

        layoutLane(redView, redLabel);
        layoutLane(greenView, greenLabel);
        layoutLane(blueView, blueLabel);
    }

private:
    juce::Component& redView;
    juce::Component& greenView;
    juce::Component& blueView;
    juce::Label redLabel { {}, "R" };
    juce::Label greenLabel { {}, "G" };
    juce::Label blueLabel { {}, "B" };
};
