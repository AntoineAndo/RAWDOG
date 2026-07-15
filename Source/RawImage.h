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

    bool writeToFile(const juce::File& file) const;

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

    // What format this image was actually loaded from. writeToFile() always
    // writes back the original format's header+pixel bytes verbatim, so an
    // export dialog must restrict its wildcard/extension to match this rather
    // than letting the user pick an unrelated extension (see PROJECT.md).
    Format getFormat() const { return format; }
    juce::String getExportWildcard() const { return format == Format::bmp ? "*.bmp" : "*.pnm;*.ppm;*.pgm"; }
    juce::String getDefaultExportExtension() const
    {
        if (format == Format::bmp)
            return ".bmp";

        return format == Format::pnmGray ? ".pgm" : ".ppm";
    }

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
