#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "RawdogLookAndFeel.h"

// juce::StretchableLayoutResizerBar's default paint() renders a plain flat
// bar with no affordance hinting that it's draggable -- easy to miss against
// the panels either side of it. This draws a small 3-dot grip indicator on
// top of the default background, oriented along the bar's long axis (stacked
// vertically for a tall/narrow bar dragged left/right, spread horizontally
// for a short/wide bar dragged up/down).
class GrippedResizerBar : public juce::StretchableLayoutResizerBar
{
public:
    GrippedResizerBar(juce::StretchableLayoutManager* layoutToUse, int itemIndexInLayout, bool isBarVertical)
        : juce::StretchableLayoutResizerBar(layoutToUse, itemIndexInLayout, isBarVertical),
          vertical(isBarVertical)
    {
    }

    void paint(juce::Graphics& g) override
    {
        juce::StretchableLayoutResizerBar::paint(g);

        constexpr int numDots = 3;
        constexpr float dotSize = 3.0f;
        constexpr float dotSpacing = 6.0f;

        g.setColour(RawdogLookAndFeel::Palette::get().ink.withAlpha(0.55f));

        const auto centre = getLocalBounds().toFloat().getCentre();

        for (int i = 0; i < numDots; ++i)
        {
            const float offset = ((float) i - (float) (numDots - 1) / 2.0f) * dotSpacing;
            const auto dotCentre = vertical ? centre.withY(centre.y + offset) : centre.withX(centre.x + offset);
            g.fillEllipse(juce::Rectangle<float>(dotSize, dotSize).withCentre(dotCentre));
        }
    }

private:
    bool vertical;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GrippedResizerBar)
};
