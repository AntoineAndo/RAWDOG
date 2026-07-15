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
    juce::Image toJuceImage() const;

    juce::MemoryBlock headerBytes;
    juce::MemoryBlock pixelBytes;

private:
    enum class Format { bmp, pnmBinary, pnmGray };

    Format format = Format::bmp;
    int width = 0;
    int height = 0;
    int rowStride = 0;   // bytes per row, including any padding (BMP only; equals width*channels for PNM)
    int channels = 0;    // 3 for BMP/P6, 1 for P5
    bool bottomUp = true; // BMP only

    static std::unique_ptr<RawImage> loadBmp(const juce::File&, juce::String& errorMessage);
    static std::unique_ptr<RawImage> loadPnm(const juce::File&, juce::String& errorMessage);
};
