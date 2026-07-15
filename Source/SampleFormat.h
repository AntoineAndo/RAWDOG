#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

// 8-bit unsigned PCM <-> float conversion, matching how raw pixel bytes are
// stored (one unsigned byte per channel value). Fixed for now; a format
// dropdown (mu-law, A-law, 16-bit, etc.) replaces this in a later milestone.
namespace SampleFormat
{
    inline juce::AudioBuffer<float> bytesToBuffer(const juce::MemoryBlock& bytes)
    {
        const auto* data = static_cast<const juce::uint8*>(bytes.getData());
        const int numSamples = (int) bytes.getSize();

        juce::AudioBuffer<float> buffer(1, numSamples);
        auto* out = buffer.getWritePointer(0);

        for (int i = 0; i < numSamples; ++i)
            out[i] = ((float) data[i] - 128.0f) / 128.0f;

        return buffer;
    }

    inline void bufferToBytes(const juce::AudioBuffer<float>& buffer, juce::MemoryBlock& bytes)
    {
        auto* out = static_cast<juce::uint8*>(bytes.getData());
        const auto* in = buffer.getReadPointer(0);
        const int numSamples = juce::jmin((int) bytes.getSize(), buffer.getNumSamples());

        for (int i = 0; i < numSamples; ++i)
        {
            const float clamped = juce::jlimit(-1.0f, 1.0f, in[i]);
            out[i] = (juce::uint8) juce::jlimit(0, 255, juce::roundToInt(clamped * 128.0f + 128.0f));
        }
    }
}
