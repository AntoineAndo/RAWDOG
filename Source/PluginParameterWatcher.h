#pragma once

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

    ~PluginParameterWatcher() override { detach(); }

private:
    void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override { triggerAsyncUpdate(); }
    void audioProcessorChanged(juce::AudioProcessor*, const juce::AudioProcessorListener::ChangeDetails&) override { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override { if (onPluginParametersChanged) onPluginParametersChanged(); }

    juce::AudioProcessor* watched = nullptr;
};
