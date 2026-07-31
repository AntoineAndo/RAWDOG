#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "RawdogLookAndFeel.h"

// Thin wrapper around juce::StretchableLayoutResizerBar -- the affordance
// (permanent hover-tint fill) is drawn entirely by
// RawdogLookAndFeel::drawStretchableLayoutResizerBar, so this class just
// exists to give the panel-split bars a distinct type to reference.
class GrippedResizerBar : public juce::StretchableLayoutResizerBar
{
public:
    GrippedResizerBar(juce::StretchableLayoutManager* layoutToUse, int itemIndexInLayout, bool isBarVertical)
        : juce::StretchableLayoutResizerBar(layoutToUse, itemIndexInLayout, isBarVertical)
    {
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GrippedResizerBar)
};
