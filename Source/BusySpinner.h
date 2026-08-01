#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

// Small indeterminate activity spinner (a rotating open arc) that shows
// itself whenever `isBusy` reports work in flight and hides itself again when
// it doesn't -- used next to the status text to signal that a live-preview
// pass is still computing (on large images a pass takes a visible fraction of
// a second, so the preview lags the knob even though the UI stays responsive;
// this is the "something is happening" cue for that gap).
//
// Drives itself from a ~30Hz poll of `isBusy` rather than explicit show/hide
// calls: "busy" is derived state owned by LivePreviewWorker (a result landing
// doesn't mean idle -- a newer request may already be queued behind it), so
// polling the one source of truth at animation rate is both simpler and more
// correct than trying to mirror it event-by-event. An idle tick costs one
// brief lock + a bool check.
class BusySpinner : public juce::Component,
                    private juce::Timer
{
public:
    BusySpinner()
    {
        setVisible(false);
        setInterceptsMouseClicks(false, false);
        startTimerHz(30);
    }

    // Polled on the message thread at animation rate; must be cheap and safe
    // to call at any time after construction (it is only ever invoked from
    // timerCallback(), never during construction, so it may safely capture
    // members declared after the spinner).
    std::function<bool()> isBusy;

private:
    void paint(juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced(2.0f);
        const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;

        juce::Path arc;
        arc.addCentredArc(bounds.getCentreX(), bounds.getCentreY(), radius, radius,
                          rotationRadians, 0.0f, juce::MathConstants<float>::twoPi * 0.75f, true);

        g.setColour(findColour(juce::Label::textColourId));
        g.strokePath(arc, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void timerCallback() override
    {
        if (isBusy != nullptr && isBusy())
        {
            consecutiveIdleTicks = 0;
            rotationRadians = std::fmod(rotationRadians + juce::MathConstants<float>::twoPi / 24.0f,
                                        juce::MathConstants<float>::twoPi);
            setVisible(true);
            repaint();
            return;
        }

        // Hide only after a few consecutive idle polls: during a knob drag the
        // worker can be momentarily between "pass finished" and "next request
        // submitted", and hiding/reshowing across that sliver would flicker.
        if (isVisible() && ++consecutiveIdleTicks >= 3)
            setVisible(false);
    }

    float rotationRadians = 0.0f;
    int consecutiveIdleTicks = 0;
};
