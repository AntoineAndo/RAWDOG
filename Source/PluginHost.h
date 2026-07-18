#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>

// Instantiates a single plugin from a PluginDescription and runs a mono buffer
// through it in blockSize-sized chunks.
class PluginHost
{
public:
    static std::unique_ptr<juce::AudioPluginInstance> createInstance(
        juce::AudioPluginFormatManager& formatManager,
        const juce::PluginDescription& description,
        double sampleRate, int blockSize,
        juce::String& errorMessage);

    // beforeBlock, when supplied, is called with each block's starting sample
    // offset (0-based within monoBuffer) right before that block is processed —
    // lets a caller sweep parameter values across the buffer (e.g. a fade)
    // instead of holding them static for the whole pass.
    static void processWholeBuffer(juce::AudioPluginInstance& plugin,
                                    juce::AudioBuffer<float>& monoBuffer,
                                    int blockSize,
                                    std::function<void(int blockStartSample)> beforeBlock = nullptr);
};
