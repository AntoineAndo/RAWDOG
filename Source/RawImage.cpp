#include "RawImage.h"
#include <vector>

namespace
{
    uint32_t readU32LE(const uint8_t* p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); }
    uint16_t readU16LE(const uint8_t* p) { return (uint16_t) (p[0] | (p[1] << 8)); }
    int32_t  readI32LE(const uint8_t* p) { return (int32_t) readU32LE(p); }
}

std::unique_ptr<RawImage> RawImage::loadFromFile(const juce::File& file, juce::String& errorMessage)
{
    if (! file.existsAsFile())
    {
        errorMessage = "File does not exist: " + file.getFullPathName();
        return nullptr;
    }

    juce::MemoryBlock probe;
    file.loadFileAsData(probe);

    if (probe.getSize() >= 2 && probe[0] == 'B' && probe[1] == 'M')
        return loadBmp(file, errorMessage);

    if (probe.getSize() >= 2 && probe[0] == 'P' && (probe[1] == '5' || probe[1] == '6'))
        return loadPnm(file, errorMessage);

    errorMessage = "Unrecognised format — only 24-bit uncompressed BMP and raw PNM (P5/P6) are supported.";
    return nullptr;
}

std::unique_ptr<RawImage> RawImage::loadBmp(const juce::File& file, juce::String& errorMessage)
{
    juce::MemoryBlock data;
    file.loadFileAsData(data);

    if (data.getSize() < 54)
    {
        errorMessage = "BMP file too small to contain a valid header.";
        return nullptr;
    }

    const auto* bytes = static_cast<const uint8_t*>(data.getData());

    const uint32_t offBits    = readU32LE(bytes + 10);
    const int32_t  width      = readI32LE(bytes + 18);
    const int32_t  height     = readI32LE(bytes + 22);
    const uint16_t bitCount   = readU16LE(bytes + 28);
    const uint32_t compression = readU32LE(bytes + 30);

    if (bitCount != 24 || compression != 0)
    {
        errorMessage = "Only uncompressed 24-bit BMP is supported (got " + juce::String(bitCount)
                       + "bpp, compression " + juce::String(compression) + "). Re-save as 24-bit uncompressed BMP.";
        return nullptr;
    }

    if (width <= 0 || height == 0)
    {
        errorMessage = "Invalid BMP dimensions.";
        return nullptr;
    }

    // Guard against attacker-controlled/corrupt width/height before any arithmetic
    // that could overflow: std::abs(INT32_MIN) is UB, and rowStride's
    // ((width*3+3)/4)*4 can overflow a signed int for huge width. Reject anything
    // outside a generous-but-finite bound (and reject INT32_MIN specifically, since
    // it has no valid positive absolute value) before doing any of that math. This
    // also caps the pixel-data size we're about to validate/allocate for, so a
    // maliciously huge declared size is rejected cleanly instead of attempting a
    // huge allocation.
    constexpr int32_t maxDimension = 32768;

    if (height == INT32_MIN || width > maxDimension || std::abs((int64_t) height) > maxDimension)
    {
        errorMessage = "BMP dimensions out of supported range.";
        return nullptr;
    }

    if (offBits >= data.getSize())
    {
        errorMessage = "BMP pixel data offset is out of range.";
        return nullptr;
    }

    auto result = std::make_unique<RawImage>();
    result->format = Format::bmp;
    result->width = width;
    result->height = (int) std::abs((int64_t) height);
    result->bottomUp = height > 0;
    result->channels = 3;

    // Widen to int64_t for the row-stride computation: width is now bounded by
    // maxDimension above, so this can't overflow, but keep the arithmetic itself
    // safely widened rather than relying solely on the earlier bound.
    const int64_t rowStride64 = ((((int64_t) width * 3) + 3) / 4) * 4;
    result->rowStride = (int) rowStride64;

    // Mirror loadPnm's validation: reject the file if its declared dimensions
    // claim more pixel data than actually remains after the header.
    const int64_t pixelDataSize64 = rowStride64 * (int64_t) result->height;
    const int64_t availableAfterHeader = (int64_t) data.getSize() - (int64_t) offBits;

    if (pixelDataSize64 > availableAfterHeader)
    {
        errorMessage = "BMP file is smaller than its header declares.";
        return nullptr;
    }

    result->headerBytes.replaceWith(data.getData(), offBits);
    result->pixelBytes.replaceWith(static_cast<const uint8_t*>(data.getData()) + offBits, data.getSize() - offBits);

    return result;
}

std::unique_ptr<RawImage> RawImage::loadPnm(const juce::File& file, juce::String& errorMessage)
{
    juce::MemoryBlock data;
    file.loadFileAsData(data);

    const auto* bytes = static_cast<const char*>(data.getData());
    const size_t size = data.getSize();

    if (size < 2)
    {
        errorMessage = "PNM file too small.";
        return nullptr;
    }

    const bool isColor = bytes[1] == '6';
    size_t pos = 2;

    auto skipWhitespaceAndComments = [&]
    {
        while (pos < size)
        {
            if (bytes[pos] == '#')
            {
                while (pos < size && bytes[pos] != '\n')
                    ++pos;
            }
            else if (std::isspace(static_cast<unsigned char>(bytes[pos])))
            {
                ++pos;
            }
            else
            {
                break;
            }
        }
    };

    auto readInt = [&]() -> int
    {
        skipWhitespaceAndComments();
        const size_t start = pos;
        while (pos < size && std::isdigit(static_cast<unsigned char>(bytes[pos])))
            ++pos;

        if (pos == start)
            return -1;

        return std::atoi(std::string(bytes + start, bytes + pos).c_str());
    };

    const int width = readInt();
    const int height = readInt();
    const int maxVal = readInt();

    if (width <= 0 || height <= 0 || maxVal <= 0)
    {
        errorMessage = "Malformed PNM header.";
        return nullptr;
    }

    if (maxVal > 255)
    {
        errorMessage = "Only 8-bit-per-sample PNM (maxval <= 255) is supported.";
        return nullptr;
    }

    // Exactly one whitespace byte separates the header from the raster data.
    if (pos < size && std::isspace(static_cast<unsigned char>(bytes[pos])))
        ++pos;

    const int channels = isColor ? 3 : 1;
    const size_t pixelDataSize = (size_t) width * (size_t) height * (size_t) channels;

    if (pos + pixelDataSize > size)
    {
        errorMessage = "PNM file is smaller than its header declares.";
        return nullptr;
    }

    auto result = std::make_unique<RawImage>();
    result->format = isColor ? Format::pnmBinary : Format::pnmGray;
    result->width = width;
    result->height = height;
    result->channels = channels;
    result->rowStride = width * channels;
    result->bottomUp = false;

    result->headerBytes.replaceWith(data.getData(), pos);
    result->pixelBytes.replaceWith(static_cast<const uint8_t*>(data.getData()) + pos, pixelDataSize);

    return result;
}

bool RawImage::writeToPngFile(const juce::File& file) const
{
    auto stream = file.createOutputStream();
    if (stream == nullptr)
        return false;

    stream->setPosition(0);
    stream->truncate();

    juce::PNGImageFormat pngFormat;
    return pngFormat.writeImageToStream(toJuceImage(), *stream);
}

juce::Image RawImage::toJuceImageFromBytes(const juce::MemoryBlock& bytesToRender, juce::Range<int> highlightByteRange) const
{
    juce::Image image(juce::Image::RGB, width, height, true);
    juce::Image::BitmapData bitmap(image, juce::Image::BitmapData::writeOnly);

    const auto* px = static_cast<const uint8_t*>(bytesToRender.getData());
    const size_t available = bytesToRender.getSize();

    // Outline thickness in pixels: a selected pixel is drawn as part of the
    // border if any pixel within this distance is unselected. Interior
    // pixels are left completely untinted, so the plugin's actual processed
    // colours stay visible — only a border around the selection is drawn,
    // rather than a full-area tint that used to obscure the result.
    constexpr int outlineThickness = 2;

    // Precompute each screen row's selected column range once (one
    // Range::getIntersectionWith() per row) rather than recomputing byte
    // offsets and re-intersecting for every pixel in a thickness x thickness
    // neighbourhood — turns an O(width*height*outlineThickness^2) scan into
    // O(width*height) with a cheap O(outlineThickness) per-pixel border
    // check below, which matters once a selection covers a large image.
    // An empty Range means "this row has no selected columns".
    std::vector<juce::Range<int>> rowSelection;

    if (! highlightByteRange.isEmpty())
    {
        rowSelection.resize((size_t) height);

        for (int y = 0; y < height; ++y)
        {
            const int sourceRow = bottomUp ? (height - 1 - y) : y;
            const size_t rowOffset = (size_t) sourceRow * (size_t) rowStride;
            const auto rowByteRange = juce::Range<int>((int) rowOffset,
                                                        (int) (rowOffset + (size_t) width * (size_t) channels));
            const auto intersection = highlightByteRange.getIntersectionWith(rowByteRange);

            if (! intersection.isEmpty())
                rowSelection[(size_t) y] = { (intersection.getStart() - (int) rowOffset) / channels,
                                              (intersection.getEnd() - (int) rowOffset + channels - 1) / channels };
        }
    }

    for (int y = 0; y < height; ++y)
    {
        const int sourceRow = bottomUp ? (height - 1 - y) : y;
        const size_t rowOffset = (size_t) sourceRow * (size_t) rowStride;

        for (int x = 0; x < width; ++x)
        {
            const size_t byteOffset = rowOffset + (size_t) x * (size_t) channels;
            juce::uint8 r = 0, g = 0, b = 0;

            if (byteOffset + (size_t) channels <= available)
            {
                if (channels == 3)
                {
                    if (format == Format::bmp)
                    {
                        // BMP stores pixels as BGR.
                        b = px[byteOffset + 0];
                        g = px[byteOffset + 1];
                        r = px[byteOffset + 2];
                    }
                    else
                    {
                        r = px[byteOffset + 0];
                        g = px[byteOffset + 1];
                        b = px[byteOffset + 2];
                    }
                }
                else
                {
                    r = g = b = px[byteOffset];
                }
            }

            auto colour = juce::Colour(r, g, b);

            if (! highlightByteRange.isEmpty() && rowSelection[(size_t) y].contains(x))
            {
                const auto& thisRow = rowSelection[(size_t) y];

                // Near the left/right edge of this row's selected span (O(1) —
                // just a distance check against the span's own bounds).
                bool isBorder = (x - thisRow.getStart() < outlineThickness)
                              || (thisRow.getEnd() - 1 - x < outlineThickness);

                // Near the top/bottom edge: a neighbouring row within
                // outlineThickness doesn't have this column selected. Only
                // outlineThickness row lookups (not a full 2D neighbourhood),
                // each an O(1) array index + Range::contains.
                for (int dy = 1; dy <= outlineThickness && ! isBorder; ++dy)
                {
                    const int above = y - dy;
                    const int below = y + dy;

                    if ((above < 0 || ! rowSelection[(size_t) above].contains(x))
                        || (below >= height || ! rowSelection[(size_t) below].contains(x)))
                        isBorder = true;
                }

                if (isBorder)
                    colour = juce::Colours::yellow;
            }

            bitmap.setPixelColour(x, y, colour);
        }
    }

    return image;
}
