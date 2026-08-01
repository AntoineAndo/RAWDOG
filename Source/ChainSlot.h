#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>
#include "ParameterAutomation.h"

// One entry in the effect chain, processed in vector order (index 0 first) --
// see MainComponent::pluginChain and LivePreviewWorker::ChainSlotRequest.
struct ChainSlot
{
    // Non-null for every element actually held in pluginChain -- only ever
    // pushed after PluginHost::createInstance() succeeds, so a failed load
    // never leaves a half-constructed slot in the chain.
    std::unique_ptr<juce::AudioPluginInstance> plugin;

    // This slot's own automation ramps. Frozen here while some OTHER slot is
    // selected; while THIS slot is selected, the live/being-edited copy lives
    // in PluginEditorPanel's ParameterAutomationPanel instead, and is written
    // back here the moment a different slot is selected -- see
    // MainComponent::selectChainSlot()/refreshLivePreview().
    std::vector<ParameterAutomation> ramps;

    bool bypassed = false;
};
