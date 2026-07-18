#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

// Exact min/max peak cache for waveform rendering -- the standard audio-editor
// technique, single level (no pyramid: even the fully-zoomed-out worst case
// measured on a 77.9M-sample buffer needs only ~490 bucket lookups per column,
// comfortably within budget). Bucket k covers the absolute sample range
// [k*N, min((k+1)*N, numSamples)), N = samplesPerBucket, and stores that
// range's exact min and max -- min/max is associative, so aggregating buckets
// plus raw-scanning the partial head/tail of a query range reproduces a plain
// raw scan's result exactly. This is what lets WaveformView's per-column trace
// scan (measured at ~406ms per rebuild on the message thread, the dominant
// cause of the live-preview knob-drag stutter -- see PROJECT.md's live-preview
// performance note) drop to O(width + viewLength/N).
//
// Everything here is a pure free function over plain arrays/vectors -- no
// component, no juce::Image -- so both WaveformView (message thread) and
// LivePreviewWorker (background thread, see Partial below) can share it, and a
// CLI harness can verify it without a GUI.
namespace WaveformPeaks
{
    constexpr int samplesPerBucket = 512;

    inline int numBucketsFor (int numSamples)
    {
        return (numSamples + samplesPerBucket - 1) / samplesPerBucket;
    }

    // A bucket-aligned run of precomputed peaks: mins/maxs[i] belong to
    // absolute bucket (firstBucket + i). Produced by computePartial() -- on
    // LivePreviewWorker's background thread for delivered live-preview
    // results -- and spliced into a full peak cache by applyPartial().
    struct Partial
    {
        int firstBucket = 0;
        std::vector<float> mins, maxs;
    };

    // All buckets for a whole buffer, honoring the partial tail bucket.
    // samples may be nullptr only when numSamples == 0.
    inline void buildPeaks (const float* samples, int numSamples,
                            std::vector<float>& mins, std::vector<float>& maxs)
    {
        const int numBuckets = numBucketsFor (numSamples);
        mins.resize ((size_t) numBuckets);
        maxs.resize ((size_t) numBuckets);

        for (int k = 0; k < numBuckets; ++k)
        {
            const int start = k * samplesPerBucket;
            const int end = juce::jmin (start + samplesPerBucket, numSamples);
            const auto r = juce::FloatVectorOperations::findMinAndMax (samples + start, end - start);
            mins[(size_t) k] = r.getStart();
            maxs[(size_t) k] = r.getEnd();
        }
    }

    // Peaks for only the buckets FULLY contained in the absolute range
    // [rangeStart, rangeEnd): k in [ceilDiv(rangeStart, N), floorDiv(rangeEnd, N)).
    // May be empty (range shorter than a bucket, or straddling one boundary).
    // Deliberate asymmetry: when rangeEnd lands on the buffer's partial tail
    // bucket (rangeEnd == numSamples, numSamples % N != 0), that bucket fails
    // the containment test and is NOT supplied -- applyPartial() recomputes it
    // from the spliced data instead. Do not "fix" this by supplying it here:
    // this function has no way to know rangeEnd is the buffer's true end.
    //
    // rangeSamples is range-local: rangeSamples[i] is absolute sample
    // (rangeStart + i). This matches LivePreviewWorker::Result::waveformSamples,
    // which holds exactly the changed sub-range (or the whole buffer, in which
    // case rangeStart is 0 and the mapping is the identity).
    inline Partial computePartial (const float* rangeSamples, int rangeStart, int rangeEnd)
    {
        Partial partial;
        const int firstFull = (rangeStart + samplesPerBucket - 1) / samplesPerBucket;
        const int endFull = rangeEnd / samplesPerBucket;
        partial.firstBucket = firstFull;

        if (endFull <= firstFull)
            return partial;

        partial.mins.reserve ((size_t) (endFull - firstFull));
        partial.maxs.reserve ((size_t) (endFull - firstFull));

        for (int k = firstFull; k < endFull; ++k)
        {
            const int localStart = k * samplesPerBucket - rangeStart;
            const auto r = juce::FloatVectorOperations::findMinAndMax (rangeSamples + localStart, samplesPerBucket);
            partial.mins.push_back (r.getStart());
            partial.maxs.push_back (r.getEnd());
        }

        return partial;
    }

    // Splices `partial` into the full peak vectors, then recomputes every
    // bucket intersecting [rangeStart, rangeEnd) that `partial` did not supply
    // (derived from firstBucket/count -- typically the <=2 partial edge
    // buckets, but possibly all of them when partial is empty), reading from
    // `samples`, which must ALREADY contain the spliced new data. mins/maxs
    // must already be sized numBucketsFor(numSamples). The range is clamped to
    // [0, numSamples] -- mirroring the effective bounds WaveformView::
    // updateSampleRange() applies to the sample splice itself, so peaks and
    // data can't diverge.
    inline void applyPartial (std::vector<float>& mins, std::vector<float>& maxs,
                              const Partial& partial,
                              const float* samples, int numSamples,
                              int rangeStart, int rangeEnd)
    {
        rangeStart = juce::jlimit (0, numSamples, rangeStart);
        rangeEnd = juce::jlimit (rangeStart, numSamples, rangeEnd);

        if (rangeStart >= rangeEnd)
            return;

        jassert ((int) mins.size() == numBucketsFor (numSamples) && mins.size() == maxs.size());

        const int firstTouched = rangeStart / samplesPerBucket;
        const int lastTouched = (rangeEnd - 1) / samplesPerBucket; // inclusive; < numBucketsFor(numSamples)

        const int suppliedBegin = partial.firstBucket;
        const int suppliedEnd = partial.firstBucket + (int) partial.mins.size();

        for (int k = firstTouched; k <= lastTouched; ++k)
        {
            if (k >= suppliedBegin && k < suppliedEnd)
            {
                mins[(size_t) k] = partial.mins[(size_t) (k - suppliedBegin)];
                maxs[(size_t) k] = partial.maxs[(size_t) (k - suppliedBegin)];
            }
            else
            {
                const int start = k * samplesPerBucket;
                const int end = juce::jmin (start + samplesPerBucket, numSamples);
                const auto r = juce::FloatVectorOperations::findMinAndMax (samples + start, end - start);
                mins[(size_t) k] = r.getStart();
                maxs[(size_t) k] = r.getEnd();
            }
        }
    }

    struct MinMax
    {
        float minV, maxV;
    };

    // Exact min/max over [start, end) clamped to numSamples, byte-identical to
    // a plain raw scan: raw-scan the partial head, aggregate fully-covered
    // buckets, raw-scan the partial tail; degrades naturally to a pure raw
    // scan when the range covers no full bucket. start >= numSamples returns
    // {0, 0} -- preserving WaveformView's existing "column beyond the data"
    // semantics (see the trace-rebuild loop's own comment).
    inline MinMax columnMinMax (const float* samples, int numSamples,
                                int start, int end,
                                const std::vector<float>& mins, const std::vector<float>& maxs)
    {
        if (start >= numSamples)
            return { 0.0f, 0.0f };

        end = juce::jmin (end, numSamples);

        const int firstFull = (start + samplesPerBucket - 1) / samplesPerBucket;
        const int endFull = end / samplesPerBucket;

        if (firstFull >= endFull)
        {
            const auto r = juce::FloatVectorOperations::findMinAndMax (samples + start, end - start);
            return { r.getStart(), r.getEnd() };
        }

        // Fully-covered buckets first -- guaranteed non-empty here, so minV/maxV
        // are always seeded from real data. The peak vectors are contiguous, so
        // the aggregation itself can use the same SIMD primitives.
        float minV = juce::FloatVectorOperations::findMinimum (mins.data() + firstFull, endFull - firstFull);
        float maxV = juce::FloatVectorOperations::findMaximum (maxs.data() + firstFull, endFull - firstFull);

        if (start < firstFull * samplesPerBucket)
        {
            const auto r = juce::FloatVectorOperations::findMinAndMax (samples + start, firstFull * samplesPerBucket - start);
            minV = juce::jmin (minV, r.getStart());
            maxV = juce::jmax (maxV, r.getEnd());
        }

        if (endFull * samplesPerBucket < end)
        {
            const auto r = juce::FloatVectorOperations::findMinAndMax (samples + endFull * samplesPerBucket, end - endFull * samplesPerBucket);
            minV = juce::jmin (minV, r.getStart());
            maxV = juce::jmax (maxV, r.getEnd());
        }

        return { minV, maxV };
    }
}
