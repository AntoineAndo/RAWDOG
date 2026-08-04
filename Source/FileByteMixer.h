#pragma once

#include <juce_core/juce_core.h>

// Mixes an arbitrary external file's raw bytes into a target byte buffer --
// the non-plugin counterpart to running pixelBytes through a VST/AU: plain
// byte math (XOR/wrapped-add/replace) instead of a DSP round-trip, tiled
// cyclically if the modifier file is shorter than the target range. Mirrors
// the copy-then-splice-subrange pattern LivePreviewWorker::processRequest()
// already uses to scope a plugin pass to a selection (LivePreviewWorker.cpp).
namespace FileByteMixer
{
    enum class Operation { xorOp, addWrap, replace };

    // Returns a full-size copy of `target`, byte-identical outside `range`,
    // with `range` mixed against `modifier` using `op`, then faded between
    // the original byte and that mixed result by `blend` (0 = original
    // unchanged, 1 = the operation's full-strength result). `scale` resamples
    // the modifier before tiling: at target offset `i - start`, the modifier
    // byte read is `floor((i - start) / scale)` -- scale > 1 stretches the
    // file's pattern out (each modifier byte covers more target bytes,
    // coarser/blockier), scale < 1 compresses it (denser, higher-frequency).
    // Nearest-byte resampling, no interpolation -- deliberately "steppy" at
    // extreme scales rather than smoothed, matching the rest of this app's
    // glitch aesthetic. An empty `modifier`, an empty `range`, or blend <= 0
    // is a no-op (guards a zero-byte file / no selection scoped to something
    // degenerate / the slider dragged to 0%) -- returns `target` unchanged.
    inline juce::MemoryBlock mixBytes(const juce::MemoryBlock& target,
                                       juce::Range<int> range,
                                       const juce::MemoryBlock& modifier,
                                       Operation op,
                                       float blend = 1.0f,
                                       float scale = 1.0f)
    {
        juce::MemoryBlock result(target);

        if (modifier.isEmpty() || range.isEmpty() || blend <= 0.0f)
            return result;

        auto* out = static_cast<juce::uint8*>(result.getData());
        const auto* mod = static_cast<const juce::uint8*>(modifier.getData());
        const size_t modSize = modifier.getSize();
        const float clampedBlend = juce::jmin(blend, 1.0f);
        const float clampedScale = juce::jmax(0.01f, scale);

        const int start = juce::jmax(0, range.getStart());
        const int end = juce::jmin((int) result.getSize(), range.getEnd());

        for (int i = start; i < end; ++i)
        {
            const size_t modIndex = (size_t) ((float) (i - start) / clampedScale) % modSize;
            const juce::uint8 m = mod[modIndex];
            const juce::uint8 original = out[i];
            juce::uint8 mixed = original;

            switch (op)
            {
                case Operation::xorOp:   mixed = (juce::uint8) (original ^ m); break;
                case Operation::addWrap: mixed = (juce::uint8) (original + m); break;
                case Operation::replace: mixed = m; break;
            }

            out[i] = clampedBlend >= 1.0f ? mixed
                                           : (juce::uint8) juce::roundToInt((float) original
                                                 + ((float) mixed - (float) original) * clampedBlend);
        }

        return result;
    }
}
