#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

// A plugin parameter's value over the span of whatever's currently being
// processed (a waveform selection, or the whole buffer when there's none) —
// lets Apply fade a parameter in/out across that span instead of holding it
// static for the whole pass.
enum class Easing { linear, easeIn, easeOut, easeInOut };

inline float applyEasing(Easing easing, float t)
{
    switch (easing)
    {
        case Easing::easeIn:    return t * t;
        case Easing::easeOut:   return t * (2.0f - t);
        case Easing::easeInOut: return t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t;
        case Easing::linear:
        default:                return t;
    }
}

struct RampSegment
{
    double startFraction = 0.0; // 0..1, fraction of the scope's total length -- the two ends of a
    double endFraction = 0.1;   // double-handled range slider, so both scale with selection length
    float initialValue = 0.0f;  // normalized 0..1, AudioProcessorParameter's own convention
    float targetValue = 1.0f;
    Easing easing = Easing::linear;
};

// Segments are expected sorted by startFraction and non-overlapping —
// ParameterAutomationPanel is responsible for keeping them that way.
struct ParameterAutomation
{
    int parameterIndex = -1; // index into AudioProcessor::getParameters()
    float originalValue = 0.0f; // the parameter's value before automation was added; restored if this entry is removed
    std::vector<RampSegment> segments;

    // totalScopeMs is the length (in ms) of whatever's currently being
    // processed (the selection sub-range, or the whole buffer) -- needed to
    // turn each segment's start/endFraction into actual timestamps.
    //
    // Before the first segment: holds at its initialValue. Between segments:
    // holds at the previous segment's targetValue — this is what turns "one
    // fade-in segment + one fade-out segment" into an in/sustain/out envelope
    // without needing a separate "sustain" concept. After the last segment:
    // holds at its targetValue.
    float evaluateAt(double timeMs, double totalScopeMs) const
    {
        if (segments.empty())
            return 0.0f;

        if (timeMs <= segments.front().startFraction * totalScopeMs)
            return segments.front().initialValue;

        for (size_t i = 0; i < segments.size(); ++i)
        {
            const auto& segment = segments[i];
            const double startMs = segment.startFraction * totalScopeMs;
            const double endMs = segment.endFraction * totalScopeMs;

            if (timeMs <= endMs)
            {
                if (timeMs < startMs) // gap since the previous segment: still holding its target
                    return segments[i - 1].targetValue;

                const double durationMs = endMs - startMs;
                const double t = durationMs > 0.0 ? (timeMs - startMs) / durationMs : 1.0;
                return segment.initialValue + (segment.targetValue - segment.initialValue)
                         * applyEasing(segment.easing, (float) juce::jlimit(0.0, 1.0, t));
            }
        }

        return segments.back().targetValue;
    }
};
