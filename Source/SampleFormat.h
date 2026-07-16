#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

// 8-bit unsigned PCM <-> float conversion, matching how raw pixel bytes are
// stored (one unsigned byte per channel value). Fixed for now; a format
// dropdown (mu-law, A-law, 16-bit, etc.) replaces this in a later milestone.
namespace SampleFormat
{
    // bipolar: byte 128 is the zero-crossing centre (correct for real audio).
    // unipolar: byte 0 is genuine silence/floor (correct for image intensity,
    // where "no colour" should stay silent under a gain-reducing effect
    // instead of reading as a full-scale-negative signal). See PROJECT.md.
    enum class Mode { bipolar, unipolar };

    inline juce::AudioBuffer<float> bytesToBuffer(const juce::MemoryBlock& bytes, Mode mode = Mode::bipolar)
    {
        const auto* data = static_cast<const juce::uint8*>(bytes.getData());
        const int numSamples = (int) bytes.getSize();

        juce::AudioBuffer<float> buffer(1, numSamples);
        auto* out = buffer.getWritePointer(0);

        for (int i = 0; i < numSamples; ++i)
            out[i] = mode == Mode::bipolar ? (((float) data[i] - 128.0f) / 128.0f)
                                            : ((float) data[i] / 255.0f);

        return buffer;
    }

    inline void bufferToBytes(const juce::AudioBuffer<float>& buffer, juce::MemoryBlock& bytes, Mode mode = Mode::bipolar)
    {
        auto* out = static_cast<juce::uint8*>(bytes.getData());
        const auto* in = buffer.getReadPointer(0);
        const int numSamples = juce::jmin((int) bytes.getSize(), buffer.getNumSamples());

        for (int i = 0; i < numSamples; ++i)
        {
            if (mode == Mode::bipolar)
            {
                const float clamped = juce::jlimit(-1.0f, 1.0f, in[i]);
                out[i] = (juce::uint8) juce::jlimit(0, 255, juce::roundToInt(clamped * 128.0f + 128.0f));
            }
            else
            {
                const float clamped = juce::jlimit(0.0f, 1.0f, in[i]);
                out[i] = (juce::uint8) juce::jlimit(0, 255, juce::roundToInt(clamped * 255.0f));
            }
        }
    }
}
