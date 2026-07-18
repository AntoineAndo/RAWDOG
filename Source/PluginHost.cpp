#include "PluginHost.h"

std::unique_ptr<juce::AudioPluginInstance> PluginHost::createInstance(
    juce::AudioPluginFormatManager& formatManager,
    const juce::PluginDescription& description,
    double sampleRate, int blockSize,
    juce::String& errorMessage)
{
    auto instance = formatManager.createPluginInstance(description, sampleRate, blockSize, errorMessage);

    if (instance != nullptr)
        instance->prepareToPlay(sampleRate, blockSize);

    return instance;
}

void PluginHost::processWholeBuffer(juce::AudioPluginInstance& plugin,
                                     juce::AudioBuffer<float>& monoBuffer,
                                     int blockSize,
                                     std::function<void(int blockStartSample)> beforeBlock)
{
    const int numChannels = juce::jmax(2, plugin.getTotalNumInputChannels(), plugin.getTotalNumOutputChannels());
    const int totalSamples = monoBuffer.getNumSamples();

    juce::AudioBuffer<float> workBuffer(numChannels, blockSize);
    juce::MidiBuffer midi;

    int pos = 0;
    while (pos < totalSamples)
    {
        if (beforeBlock)
            beforeBlock(pos);

        const int numThisBlock = juce::jmin(blockSize, totalSamples - pos);

        workBuffer.setSize(numChannels, numThisBlock, false, false, true);
        workBuffer.clear();

        for (int ch = 0; ch < numChannels; ++ch)
            workBuffer.copyFrom(ch, 0, monoBuffer, 0, pos, numThisBlock);

        midi.clear();
        plugin.processBlock(workBuffer, midi);

        monoBuffer.copyFrom(0, pos, workBuffer, 0, 0, numThisBlock);

        pos += numThisBlock;
    }
}
