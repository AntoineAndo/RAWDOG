#pragma once

#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>

// Detects parameter/state changes on a currently-attached plugin instance so a
// live preview can be refreshed. AudioProcessorListener's callbacks may fire off
// the message thread (JUCE's own docs call this out), so this coalesces bursts
// (e.g. a knob drag firing many callbacks/sec) into a single message-thread
// callback per event-loop turn via AsyncUpdater — that's also a sufficient,
// free debounce, no separate Timer needed.
class PluginParameterWatcher : private juce::AudioProcessorListener,
                                private juce::AsyncUpdater
{
public:
    void attachTo(juce::AudioProcessor& plugin) { detach(); watched = &plugin; watched->addListener(this); }
    void attachTo(std::nullptr_t) { detach(); }
    void detach() { if (watched != nullptr) watched->removeListener(this); watched = nullptr; cancelPendingUpdate(); }

    std::function<void()> onPluginParametersChanged;

    // Fired (coalesced the same way as onPluginParametersChanged above) with
    // the display name and current value-as-text of whichever parameter last
    // changed — for surfacing "what just changed" in a status line. Only
    // fired from an actual per-parameter change (audioProcessorParameterChanged
    // below); the more general audioProcessorChanged() state-change
    // notification (program change, etc.) has no single parameter to report,
    // so it doesn't trigger this callback.
    std::function<void(const juce::String& parameterName, const juce::String& valueText)> onParameterValueChanged;

    ~PluginParameterWatcher() override { detach(); }

    // RAII guard for LivePreviewWorker's ramp-evaluation pass: parameter-automation
    // ramps drive a plugin parameter via setValueNotifyingHost() from the worker
    // thread (see LivePreviewWorker), which — unguarded — would look identical to a
    // real user knob-drag and re-trigger a live-preview recompute of the same
    // deterministic ramps, forever. The suppression is scoped to a thread_local
    // flag, so it only ever suppresses the ramp-writing thread's own writes —
    // a real gesture arriving concurrently on the message thread (or any other
    // thread) is never swallowed, since setValueNotifyingHost() invokes listeners
    // synchronously on whichever thread calls it.
    struct ScopedSelfWriteSuppression
    {
        ScopedSelfWriteSuppression() { ++suppressionDepth; }
        ~ScopedSelfWriteSuppression() { --suppressionDepth; }
    };

private:
    static bool isSuppressedOnThisThread() { return suppressionDepth > 0; }

    void audioProcessorParameterChanged(juce::AudioProcessor*, int parameterIndex, float) override
    {
        if (isSuppressedOnThisThread())
            return;

        lastChangedParameterIndex = parameterIndex;
        triggerAsyncUpdate();
    }

    void audioProcessorChanged(juce::AudioProcessor*, const juce::AudioProcessorListener::ChangeDetails&) override
    {
        if (isSuppressedOnThisThread())
            return;

        lastChangedParameterIndex = -1;
        triggerAsyncUpdate();
    }

    void handleAsyncUpdate() override
    {
        const int parameterIndex = lastChangedParameterIndex.exchange(-1);

        if (watched != nullptr && parameterIndex >= 0 && onParameterValueChanged != nullptr)
            if (auto* param = watched->getParameters()[parameterIndex]) // juce::Array::operator[] bounds-checks, returns nullptr if stale/out of range
                onParameterValueChanged(param->getName(128), param->getCurrentValueAsText());

        if (onPluginParametersChanged != nullptr)
            onPluginParametersChanged();
    }

    juce::AudioProcessor* watched = nullptr;

    // Written from audioProcessorParameterChanged(), which JUCE documents may
    // be called off the message thread; read from handleAsyncUpdate(), always
    // on the message thread. Atomic to make that cross-thread handoff safe.
    std::atomic<int> lastChangedParameterIndex { -1 };

    // Per-thread, not per-instance: only LivePreviewWorker's dedicated thread ever
    // sets this, for the narrow duration of a ramp-evaluation pass, so it can never
    // suppress a real gesture arriving on the message thread (or any other thread).
    // inline (C++17) so this header-only class doesn't need a matching .cpp just
    // for one static member's out-of-line definition.
    inline static thread_local int suppressionDepth = 0;
};
