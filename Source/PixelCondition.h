#pragma once

#include "RawImage.h"

// A per-pixel predicate a ConditionalChainSlot evaluates to decide which branch a given
// sample belongs to. ConditionType is the single switch point a future condition kind
// (e.g. a specific channel's raw value, edge detection) extends - every call site
// downstream (ConditionalChainSlot, the UI) only ever goes through
// evaluatePixelCondition(), never inspects `type` itself.
enum class ConditionType { brightness, saturation };

enum class ComparisonOp { greaterOrEqual, lessThan };

struct PixelCondition
{
    ConditionType type = ConditionType::brightness;
    ComparisonOp op = ComparisonOp::greaterOrEqual;
    juce::uint8 threshold = 128; // 0-255 raw byte scale
};

// How a ConditionalChainSlot's two branches are recombined into one result - see
// ConditionalChainSlot.h and LivePreviewWorker.cpp's runConditionalSlot().
enum class CompositingMode { masked, compacted };

// Reads brightness directly from the image's real pixel data (its channel planes),
// never from the audio buffer being processed - a conditional slot's condition always
// reflects the image's pre-chain pixel values, regardless of what earlier slots in the
// same chain have already done to the buffer. A user wanting the condition to react to
// an upstream effect's output can put the conditional slot first in the chain.
//
// pixelIndex is the canonical top-down, padding-free index getChannelPlane() already
// uses (RawImage.cpp's ensurePlanesUpToDate(): pixelIndex = y*width + x), and is the same
// index into every channel's plane.
inline juce::uint8 computePixelBrightness(const RawImage& image, int pixelIndex)
{
    if (image.hasChannelPlanes())
    {
        const auto* r = static_cast<const juce::uint8*>(image.getChannelPlane(RawImage::Channel::red).getData());
        const auto* g = static_cast<const juce::uint8*>(image.getChannelPlane(RawImage::Channel::green).getData());
        const auto* b = static_cast<const juce::uint8*>(image.getChannelPlane(RawImage::Channel::blue).getData());
        return (juce::uint8) (((int) r[pixelIndex] + (int) g[pixelIndex] + (int) b[pixelIndex]) / 3);
    }

    // Grayscale (PNM P5): getChannelPlane() returns empty blocks whenever
    // ! hasChannelPlanes(), so read the single stored byte directly - valid because PNM
    // has no BMP-style row padding/bottom-up quirk (rowStride == width*channels), so
    // pixelIndex maps 1:1 onto pixelBytes.
    return static_cast<const juce::uint8*>(image.pixelBytes.getData())[pixelIndex];
}

// How vivid/colorful a pixel is (0 = grey, 255 = a fully saturated primary),
// computed the same cheap way as computePixelBrightness() above: max-min of
// the same three channel planes, no extra sampling or neighbor lookups
// needed. A grayscale image (no channel planes) has no color variation at
// all, so saturation is always zero there.
inline juce::uint8 computePixelSaturation(const RawImage& image, int pixelIndex)
{
    if (! image.hasChannelPlanes())
        return 0;

    const auto* r = static_cast<const juce::uint8*>(image.getChannelPlane(RawImage::Channel::red).getData());
    const auto* g = static_cast<const juce::uint8*>(image.getChannelPlane(RawImage::Channel::green).getData());
    const auto* b = static_cast<const juce::uint8*>(image.getChannelPlane(RawImage::Channel::blue).getData());

    const auto maxValue = juce::jmax(r[pixelIndex], g[pixelIndex], b[pixelIndex]);
    const auto minValue = juce::jmin(r[pixelIndex], g[pixelIndex], b[pixelIndex]);
    return (juce::uint8) (maxValue - minValue);
}

// The single switch point ConditionType's own doc comment refers to -- a new
// condition kind only ever needs a new case added here.
inline juce::uint8 computeConditionValue(const PixelCondition& condition, const RawImage& image, int pixelIndex)
{
    switch (condition.type)
    {
        case ConditionType::saturation: return computePixelSaturation(image, pixelIndex);
        case ConditionType::brightness:
        default:                        return computePixelBrightness(image, pixelIndex);
    }
}

inline bool evaluatePixelCondition(const PixelCondition& condition, const RawImage& image, int pixelIndex)
{
    const auto value = computeConditionValue(condition, image, pixelIndex);
    return condition.op == ComparisonOp::greaterOrEqual ? value >= condition.threshold
                                                          : value < condition.threshold;
}
