#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

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

    static void processWholeBuffer(juce::AudioPluginInstance& plugin,
                                    juce::AudioBuffer<float>& monoBuffer,
                                    int blockSize);
};
