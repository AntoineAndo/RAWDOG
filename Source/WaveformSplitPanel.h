#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

// Purely presentational: stacks the channel waveform lanes vertically with a
// small colour-coded label identifying each. Holds no selection/business
// logic of its own — MainComponent drives each WaveformView's buffer and
// selection directly; this just parents and lays them out.
//
// The 4th (alpha) lane is always constructed and parented, but only laid
// out/shown when MainComponent has made it visible (a loaded PNG with a real
// alpha channel) — for every other source (BMP, PNM, an alpha-less PNG) it
// stays hidden and the other 3 lanes share the full height.
class WaveformSplitPanel : public juce::Component
{
public:
    WaveformSplitPanel(juce::Component& redViewIn, juce::Component& greenViewIn,
                        juce::Component& blueViewIn, juce::Component& alphaViewIn)
        : redView(redViewIn), greenView(greenViewIn), blueView(blueViewIn), alphaView(alphaViewIn)
    {
        addAndMakeVisible(redView);
        addAndMakeVisible(greenView);
        addAndMakeVisible(blueView);
        addAndMakeVisible(alphaView);

        redLabel.setColour(juce::Label::textColourId, juce::Colours::red);
        greenLabel.setColour(juce::Label::textColourId, juce::Colours::green);
        blueLabel.setColour(juce::Label::textColourId, juce::Colours::blue);
        alphaLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(redLabel);
        addAndMakeVisible(greenLabel);
        addAndMakeVisible(blueLabel);
        addAndMakeVisible(alphaLabel);
    }

    // Lets WaveformSectionPanel's mode label mention "A" only when this
    // image actually has an alpha lane -- avoids threading a 5th reference
    // through just to answer this one question.
    bool hasVisibleAlphaLane() const { return alphaView.isVisible(); }

    void resized() override
    {
        auto area = getLocalBounds();

        struct Lane { juce::Component& view; juce::Label& label; };
        Lane lanes[] = { { redView, redLabel }, { greenView, greenLabel },
                          { blueView, blueLabel }, { alphaView, alphaLabel } };

        int visibleCount = 0;
        for (auto& lane : lanes)
            if (lane.view.isVisible())
                ++visibleCount;

        if (visibleCount == 0)
            return;

        const int laneHeight = area.getHeight() / visibleCount;

        for (auto& lane : lanes)
        {
            lane.label.setVisible(lane.view.isVisible());

            if (! lane.view.isVisible())
                continue;

            auto lane_ = area.removeFromTop(laneHeight);
            lane.label.setBounds(lane_.removeFromLeft(20));
            lane.view.setBounds(lane_);
        }
    }

private:
    juce::Component& redView;
    juce::Component& greenView;
    juce::Component& blueView;
    juce::Component& alphaView;
    juce::Label redLabel { {}, "R" };
    juce::Label greenLabel { {}, "G" };
    juce::Label blueLabel { {}, "B" };
    juce::Label alphaLabel { {}, "A" };
};
