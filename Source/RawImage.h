#pragma once

#include <juce_graphics/juce_graphics.h>

// Loads/saves BMP (24-bit uncompressed) and PNM (raw P5/P6) files, keeping the
// header bytes verbatim and exposing only the pixel bytes for databending.
// This is the "protected region" boundary described in the design: header
// bytes are never touched, pixelBytes is the only thing a plugin may process.
class RawImage
{
public:
    static std::unique_ptr<RawImage> loadFromFile(const juce::File& file, juce::String& errorMessage);

    // Exports the current pixelBytes as a PNG, regardless of the format the
    // image was originally loaded from — encodes toJuceImage() (no selection
    // highlight) via juce::PNGImageFormat, so the exported file is always a
    // valid, widely-viewable image rather than a raw BMP/PNM reconstruction.
    bool writeToPngFile(const juce::File& file) const;

    // Renders the current pixelBytes (post- or pre-processing) as a juce::Image for preview.
    // highlightByteRange, if non-empty, tints the pixels whose bytes fall within it — used to
    // show which part of the image the current waveform selection maps onto.
    juce::Image toJuceImage(juce::Range<int> highlightByteRange = {}) const
    { return toJuceImageFromBytes(pixelBytes, highlightByteRange); }

    // Same rendering as toJuceImage(), but reads bytesToRender instead of this->pixelBytes —
    // format metadata (width/height/rowStride/channels/bottomUp/format) still comes from this.
    // Lets a caller render an uncommitted candidate buffer (e.g. a live plugin preview) for
    // display without ever mutating pixelBytes itself.
    juce::Image toJuceImageFromBytes(const juce::MemoryBlock& bytesToRender,
                                      juce::Range<int> highlightByteRange = {}) const;

    enum class Format { bmp, pnmBinary, pnmGray };

    // What format this image was actually loaded from (BMP vs PNM) — used
    // internally to interpret pixelBytes' layout (row order, channel order).
    Format getFormat() const { return format; }

    juce::MemoryBlock headerBytes;
    juce::MemoryBlock pixelBytes;

private:
    Format format = Format::bmp;
    int width = 0;
    int height = 0;
    int rowStride = 0;   // bytes per row, including any padding (BMP only; equals width*channels for PNM)
    int channels = 0;    // 3 for BMP/P6, 1 for P5
    bool bottomUp = true; // BMP only

    static std::unique_ptr<RawImage> loadBmp(const juce::File&, juce::String& errorMessage);
    static std::unique_ptr<RawImage> loadPnm(const juce::File&, juce::String& errorMessage);
};
