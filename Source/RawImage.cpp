#include "RawImage.h"
#include <array>
#include <cstring>
#include <vector>

namespace
{
    uint32_t readU32LE(const uint8_t* p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); }
    uint16_t readU16LE(const uint8_t* p) { return (uint16_t) (p[0] | (p[1] << 8)); }
    int32_t  readI32LE(const uint8_t* p) { return (int32_t) readU32LE(p); }

    void writeU32LE(uint8_t* p, uint32_t v) { p[0] = (uint8_t) v; p[1] = (uint8_t) (v >> 8); p[2] = (uint8_t) (v >> 16); p[3] = (uint8_t) (v >> 24); }
    void writeU16LE(uint8_t* p, uint16_t v) { p[0] = (uint8_t) v; p[1] = (uint8_t) (v >> 8); }
    void writeI32LE(uint8_t* p, int32_t v) { writeU32LE(p, (uint32_t) v); }

    // Row stride (bytes per row, incl. 4-byte padding) for an arbitrary source
    // bit depth. Widened to int64_t so width*bitCount can't overflow a signed
    // int; callers bound width against maxDimension first, and bitCount is <= 32,
    // so the product stays well within int64 range.
    int64_t computeSourceBmpRowStride(int32_t width, int bitCount)
    {
        const int64_t bitsPerRow  = (int64_t) width * (int64_t) bitCount;
        const int64_t bytesPerRow = (bitsPerRow + 7) / 8;
        return ((bytesPerRow + 3) / 4) * 4;
    }

    // Synthesises a fresh, self-consistent 54-byte BMP header (BITMAPFILEHEADER +
    // 40-byte BITMAPINFOHEADER) describing a 24-bit uncompressed image. Used when
    // a non-24bpp source is expanded into a full 24-bit BGR buffer at load time:
    // the original header (with its own bfOffBits/biBitCount/palette) no longer
    // describes the converted pixel data, so it's discarded and this is emitted in
    // its place. signedHeight carries the orientation (positive == bottom-up).
    juce::MemoryBlock buildBmp24Header(int32_t width, int32_t signedHeight, int64_t pixelDataSize)
    {
        juce::MemoryBlock header;
        header.setSize(54, true); // zero-initialised: all resolution/palette-count fields default to 0

        auto* b = static_cast<uint8_t*>(header.getData());
        b[0] = 'B'; b[1] = 'M';                       // bfType
        writeU32LE(b + 2,  (uint32_t) (54 + pixelDataSize)); // bfSize
        writeU32LE(b + 10, 54);                       // bfOffBits
        writeU32LE(b + 14, 40);                       // biSize (BITMAPINFOHEADER)
        writeI32LE(b + 18, width);                    // biWidth
        writeI32LE(b + 22, signedHeight);             // biHeight (sign == orientation)
        writeU16LE(b + 26, 1);                        // biPlanes
        writeU16LE(b + 28, 24);                       // biBitCount
        writeU32LE(b + 30, 0);                        // biCompression (BI_RGB)
        writeU32LE(b + 34, (uint32_t) pixelDataSize); // biSizeImage
        // biXPelsPerMeter/biYPelsPerMeter/biClrUsed/biClrImportant left at 0.
        return header;
    }

    int popCount32(uint32_t v)
    {
        int count = 0;
        while (v) { count += (int) (v & 1u); v >>= 1; }
        return count;
    }

    int trailingZeros32(uint32_t v)
    {
        if (v == 0) return 0;
        int shift = 0;
        while (((v >> shift) & 1u) == 0u) ++shift;
        return shift;
    }
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

    static const uint8_t pngSignature[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };

    if (probe.getSize() >= 8 && std::memcmp(probe.getData(), pngSignature, 8) == 0)
        return loadPng(file, errorMessage);

    // JPEG's SOI (Start Of Image) marker -- every JPEG file, regardless of
    // variant (JFIF/EXIF/etc.), begins with these 2 bytes.
    if (probe.getSize() >= 2 && (uint8_t) probe[0] == 0xFF && (uint8_t) probe[1] == 0xD8)
        return loadJpeg(file, errorMessage);

    errorMessage = "Unrecognised format - only 24-bit uncompressed BMP, raw PNM (P5/P6), PNG, and JPEG are supported.";
    return nullptr;
}

// Widened to int64_t so the multiply can't overflow a signed int for huge
// width/channels; callers of this that accept untrusted width should still
// bound it against maxDimension first (loadBmp and the header-editor's
// validator both do).
int64_t RawImage::computeBmpRowStride(int32_t width, int channels)
{
    return ((((int64_t) width * (int64_t) channels) + 3) / 4) * 4;
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

    // Supported source encodings. Everything else (16bpp, RLE-compressed
    // palette formats, etc.) is still rejected. The 1/4/8/32bpp variants are
    // expanded into a full 24-bit BGR buffer below (see convertNon24Bpp path),
    // so the rest of the app only ever sees an ordinary 24-bit image.
    const bool is24Rgb        = (bitCount == 24 && compression == 0);
    const bool isPalette      = ((bitCount == 1 || bitCount == 4 || bitCount == 8) && compression == 0);
    const bool is32Rgb        = (bitCount == 32 && compression == 0);
    const bool is32Bitfields  = (bitCount == 32 && compression == 3);

    if (! (is24Rgb || isPalette || is32Rgb || is32Bitfields))
    {
        errorMessage = "Unsupported BMP encoding (got " + juce::String(bitCount)
                       + "bpp, compression " + juce::String(compression) + "). Supported: 1/4/8/24/32-bit "
                         "uncompressed and 32-bit BI_BITFIELDS.";
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

    const int      absHeight        = (int) std::abs((int64_t) height);
    const int64_t  availableAfterHeader = (int64_t) data.getSize() - (int64_t) offBits;

    // --- Native 24-bit uncompressed: keep the original header/pixel bytes
    //     verbatim, exactly as before this loader learned about other depths. ---
    if (is24Rgb)
    {
        auto result = std::make_unique<RawImage>();
        result->format = Format::bmp;
        result->width = width;
        result->height = absHeight;
        result->bottomUp = height > 0;
        result->channels = 3;

        const int64_t rowStride64 = computeBmpRowStride(width, result->channels);
        result->rowStride = (int) rowStride64;

        // Mirror loadPnm's validation: reject the file if its declared dimensions
        // claim more pixel data than actually remains after the header.
        const int64_t pixelDataSize64 = rowStride64 * (int64_t) result->height;

        if (pixelDataSize64 > availableAfterHeader)
        {
            errorMessage = "BMP file is smaller than its header declares.";
            return nullptr;
        }

        result->headerBytes.replaceWith(data.getData(), offBits);
        result->pixelBytes.replaceWith(static_cast<const uint8_t*>(data.getData()) + offBits, data.getSize() - offBits);

        return result;
    }

    // --- Non-24bpp: decode/expand into a fresh 24-bit BGR buffer once, here, so
    //     every downstream subsystem (waveform, channel planes, header editing,
    //     the apply pipeline) only ever sees an ordinary 24-bit BMP. ---
    const int64_t srcRowStride     = computeSourceBmpRowStride(width, (int) bitCount);
    const int64_t srcPixelDataSize = srcRowStride * (int64_t) absHeight;

    if (srcPixelDataSize > availableAfterHeader)
    {
        errorMessage = "BMP file is smaller than its header declares.";
        return nullptr;
    }

    // The palette (1/4/8bpp) and BI_BITFIELDS masks (32bpp) are both located
    // immediately after the DIB header, at a fixed offset that assumes the
    // classic 40-byte BITMAPINFOHEADER specifically -- a file using one of the
    // larger V4/V5 DIB header variants (108/124 bytes, common from modern
    // screenshot tools for BI_BITFIELDS) embeds its own mask fields *inside*
    // that larger header instead, so reading "right after biSize bytes" would
    // silently misinterpret unrelated bytes rather than the real masks/palette.
    // Reject cleanly rather than guess.
    if ((isPalette || is32Bitfields) && readU32LE(bytes + 14) != 40)
    {
        errorMessage = "This BMP uses a DIB header variant (V4/V5, " + juce::String(readU32LE(bytes + 14))
                       + " bytes) not supported for non-24bpp conversion — only the classic 40-byte "
                         "BITMAPINFOHEADER is supported.";
        return nullptr;
    }

    const int64_t dstRowStride     = computeBmpRowStride(width, 3);
    const int64_t dstPixelDataSize = dstRowStride * (int64_t) absHeight;

    juce::MemoryBlock outPixels;
    outPixels.setSize((size_t) dstPixelDataSize, true); // zero-init leaves row padding at 0
    auto* const       dst       = static_cast<uint8_t*>(outPixels.getData());
    const uint8_t* const srcPixels = bytes + offBits;

    if (isPalette)
    {
        // Palette lives immediately after the DIB header (offset 14 + biSize),
        // 4 bytes/entry (B, G, R, reserved). Entry count = biClrUsed if nonzero,
        // else 2^biBitCount, clamped to the depth's maximum.
        const uint32_t biSize       = readU32LE(bytes + 14);
        const int64_t  paletteStart = 14 + (int64_t) biSize;
        const uint32_t maxEntries   = 1u << bitCount;
        const uint32_t clrUsed      = readU32LE(bytes + 46);
        const uint32_t paletteCount = juce::jmin(clrUsed != 0 ? clrUsed : maxEntries, maxEntries);
        const int64_t  paletteBytes = (int64_t) paletteCount * 4;

        if (paletteStart < 54 || paletteStart + paletteBytes > (int64_t) offBits
            || paletteStart + paletteBytes > (int64_t) data.getSize())
        {
            errorMessage = "BMP colour palette is malformed or does not fit before the pixel data.";
            return nullptr;
        }

        // Full-size palette (defaulting unused entries to black) so any index in
        // the pixel data is always in bounds without a per-pixel range check.
        std::vector<std::array<uint8_t, 3>> palette((size_t) maxEntries, std::array<uint8_t, 3> { 0, 0, 0 });
        const uint8_t* const pal = bytes + paletteStart;

        for (uint32_t i = 0; i < paletteCount; ++i)
            palette[i] = { pal[i * 4 + 0], pal[i * 4 + 1], pal[i * 4 + 2] };

        for (int r = 0; r < absHeight; ++r)
        {
            const uint8_t* const srcRow = srcPixels + (int64_t) r * srcRowStride;
            uint8_t* const       dstRow = dst + (int64_t) r * dstRowStride;

            for (int x = 0; x < width; ++x)
            {
                uint32_t index = 0;

                if (bitCount == 8)
                {
                    index = srcRow[x];
                }
                else if (bitCount == 4)
                {
                    const uint8_t byte = srcRow[x / 2];
                    index = (x & 1) ? (byte & 0x0F) : (uint8_t) (byte >> 4); // high nibble = left pixel
                }
                else // bitCount == 1
                {
                    const uint8_t byte = srcRow[x / 8];
                    const int     bit  = 7 - (x & 7); // MSB-first: high bit = leftmost pixel
                    index = (byte >> bit) & 1u;
                }

                const auto& entry = palette[index];
                dstRow[x * 3 + 0] = entry[0]; // B
                dstRow[x * 3 + 1] = entry[1]; // G
                dstRow[x * 3 + 2] = entry[2]; // R
            }
        }
    }
    else if (is32Rgb)
    {
        // Implicit BGRX: drop the 4th (X/alpha) byte per pixel.
        for (int r = 0; r < absHeight; ++r)
        {
            const uint8_t* const srcRow = srcPixels + (int64_t) r * srcRowStride;
            uint8_t* const       dstRow = dst + (int64_t) r * dstRowStride;

            for (int x = 0; x < width; ++x)
            {
                const uint8_t* const p = srcRow + (int64_t) x * 4;
                dstRow[x * 3 + 0] = p[0]; // B
                dstRow[x * 3 + 1] = p[1]; // G
                dstRow[x * 3 + 2] = p[2]; // R
            }
        }
    }
    else // is32Bitfields
    {
        // BI_BITFIELDS: three (R, G, B order) DWORD channel masks stored
        // immediately after the 40-byte DIB header. Extract each channel
        // generally — shift by the mask's lowest set bit, width from its
        // popcount — then normalise to 8 bits (don't assume byte-aligned masks).
        const uint32_t biSize    = readU32LE(bytes + 14);
        const int64_t  maskStart = 14 + (int64_t) biSize;

        if (maskStart + 12 > (int64_t) offBits || maskStart + 12 > (int64_t) data.getSize())
        {
            errorMessage = "BMP BI_BITFIELDS masks are missing or do not fit before the pixel data.";
            return nullptr;
        }

        const uint32_t redMask   = readU32LE(bytes + maskStart + 0);
        const uint32_t greenMask = readU32LE(bytes + maskStart + 4);
        const uint32_t blueMask  = readU32LE(bytes + maskStart + 8);

        const auto extract = [] (uint32_t value, uint32_t mask) -> uint8_t
        {
            if (mask == 0)
                return 0;

            const int      shift    = trailingZeros32(mask);
            const int      bits     = popCount32(mask);
            const uint32_t maxVal   = (bits >= 32) ? 0xFFFFFFFFu : ((1u << bits) - 1u);
            const uint32_t channelV = (value & mask) >> shift;

            if (maxVal == 0)
                return 0;

            return (uint8_t) ((channelV * 255u + maxVal / 2) / maxVal); // rounded normalise to 0..255
        };

        for (int r = 0; r < absHeight; ++r)
        {
            const uint8_t* const srcRow = srcPixels + (int64_t) r * srcRowStride;
            uint8_t* const       dstRow = dst + (int64_t) r * dstRowStride;

            for (int x = 0; x < width; ++x)
            {
                const uint32_t value = readU32LE(srcRow + (int64_t) x * 4);
                dstRow[x * 3 + 0] = extract(value, blueMask);  // B
                dstRow[x * 3 + 1] = extract(value, greenMask); // G
                dstRow[x * 3 + 2] = extract(value, redMask);   // R
            }
        }
    }

    auto result = std::make_unique<RawImage>();
    result->format = Format::bmp;
    result->width = width;
    result->height = absHeight;
    result->bottomUp = height > 0;             // preserve source orientation
    result->channels = 3;
    result->rowStride = (int) dstRowStride;
    result->headerBytes = buildBmp24Header(width, height > 0 ? absHeight : -absHeight, dstPixelDataSize);
    result->pixelBytes = std::move(outPixels);

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

std::unique_ptr<RawImage> RawImage::loadPng(const juce::File& file, juce::String& errorMessage)
{
    juce::MemoryBlock fileBytes;
    file.loadFileAsData(fileBytes);

    // IHDR is always the very first chunk, at a fixed offset right after the
    // 8-byte signature (8 sig + 4 chunk length + 4 "IHDR" + 4 width + 4
    // height + 1 bit depth = colour type at byte 25) -- reading it directly
    // is the same "simple fixed-offset field, no general chunk-walking"
    // approach the BMP/PNM loaders already use. This is read from the file
    // itself rather than trusted from the decoded juce::Image below:
    // Image::hasAlphaChannel() can't be used for this, because
    // CoreGraphicsImageType silently upgrades every decoded image to ARGB on
    // macOS regardless of the source's real alpha (see
    // toJuceImageFromBytes()'s doc comment for the same quirk elsewhere in
    // this class) -- it would report "has alpha" even for a plain RGB PNG.
    if (fileBytes.getSize() < 26)
    {
        errorMessage = "PNG file too small to contain a valid header.";
        return nullptr;
    }

    const auto* fileHeader = static_cast<const uint8_t*>(fileBytes.getData());
    const uint8_t colourType = fileHeader[25];
    const bool hasAlpha = (colourType == 4 || colourType == 6); // 4 = grayscale+alpha, 6 = RGBA

    juce::Image image = juce::ImageFileFormat::loadFrom(file);

    if (! image.isValid())
    {
        errorMessage = "Could not decode PNG file.";
        return nullptr;
    }

    const int width = image.getWidth();
    const int height = image.getHeight();

    if (width <= 0 || height <= 0 || width > maxDimension || height > maxDimension)
    {
        errorMessage = "PNG dimensions out of supported range.";
        return nullptr;
    }

    const int channels = hasAlpha ? 4 : 3;

    auto result = std::make_unique<RawImage>();
    result->format = Format::png;
    result->width = width;
    result->height = height;
    result->channels = channels;
    result->rowStride = width * channels;
    result->bottomUp = false; // decoded top-down, like PNM -- headerBytes stays empty, nothing to derive orientation from
    result->pixelBytes = packImageToInterleavedBytes(image, channels);

    return result;
}

std::unique_ptr<RawImage> RawImage::loadJpeg(const juce::File& file, juce::String& errorMessage)
{
    juce::Image image = juce::ImageFileFormat::loadFrom(file);

    if (! image.isValid())
    {
        errorMessage = "Could not decode JPEG file.";
        return nullptr;
    }

    const int width = image.getWidth();
    const int height = image.getHeight();

    if (width <= 0 || height <= 0 || width > maxDimension || height > maxDimension)
    {
        errorMessage = "JPEG dimensions out of supported range.";
        return nullptr;
    }

    auto result = std::make_unique<RawImage>();
    result->format = Format::jpeg;
    result->width = width;
    result->height = height;
    result->channels = 3; // JPEG has no alpha channel
    result->rowStride = width * 3;
    result->bottomUp = false; // decoded top-down, like PNM/PNG -- headerBytes stays empty, nothing to derive orientation from
    result->pixelBytes = packImageToInterleavedBytes(image, 3);

    return result;
}

juce::MemoryBlock RawImage::packImageToInterleavedBytes(const juce::Image& image, int channels)
{
    const int width = image.getWidth();
    const int height = image.getHeight();

    juce::MemoryBlock outPixels;
    outPixels.setSize((size_t) width * (size_t) height * (size_t) channels, false);
    auto* const dst = static_cast<uint8_t*>(outPixels.getData());

    const juce::Image::BitmapData bitmap(image, juce::Image::BitmapData::readOnly);

    for (int y = 0; y < height; ++y)
    {
        uint8_t* const dstRow = dst + (size_t) y * (size_t) width * (size_t) channels;

        for (int x = 0; x < width; ++x)
        {
            const auto colour = bitmap.getPixelColour(x, y);
            uint8_t* const p = dstRow + (size_t) x * (size_t) channels;

            p[0] = colour.getRed();
            p[1] = colour.getGreen();
            p[2] = colour.getBlue();

            if (channels == 4)
                p[3] = colour.getAlpha();
        }
    }

    return outPixels;
}

RawImage::BmpHeaderFields RawImage::getBmpHeaderFields() const
{
    BmpHeaderFields fields;

    if (format != Format::bmp)
        return fields;

    const auto* bytes = static_cast<const uint8_t*>(headerBytes.getData());
    const size_t size = headerBytes.getSize();

    // A malformed real-world BMP can legally have a declared bfOffBits smaller
    // than minimumBmpHeaderSize (loadBmp() only rejects offBits >= file size,
    // not a lower bound), which would make headerBytes shorter than the 54
    // bytes these fields normally occupy. Only read a field if it actually
    // fits, rather than reading past the end of headerBytes.
    const auto has = [size] (size_t offset, size_t fieldWidth) { return offset + fieldWidth <= size; };

    if (has(0, 2))  fields.bfType      = readU16LE(bytes + 0);
    if (has(2, 4))  fields.bfSize      = readU32LE(bytes + 2);
    if (has(6, 2))  fields.bfReserved1 = readU16LE(bytes + 6);
    if (has(8, 2))  fields.bfReserved2 = readU16LE(bytes + 8);
    if (has(10, 4)) fields.bfOffBits   = readU32LE(bytes + 10);

    if (has(14, 4)) fields.biSize          = readU32LE(bytes + 14);
    if (has(18, 4)) fields.biWidth         = readI32LE(bytes + 18);
    if (has(22, 4)) fields.biHeight        = readI32LE(bytes + 22);
    if (has(26, 2)) fields.biPlanes        = readU16LE(bytes + 26);
    if (has(28, 2)) fields.biBitCount      = readU16LE(bytes + 28);
    if (has(30, 4)) fields.biCompression   = readU32LE(bytes + 30);
    if (has(34, 4)) fields.biSizeImage     = readU32LE(bytes + 34);
    if (has(38, 4)) fields.biXPelsPerMeter = readI32LE(bytes + 38);
    if (has(42, 4)) fields.biYPelsPerMeter = readI32LE(bytes + 42);
    if (has(46, 4)) fields.biClrUsed       = readU32LE(bytes + 46);
    if (has(50, 4)) fields.biClrImportant  = readU32LE(bytes + 50);

    return fields;
}

RawImage::HeaderEditResult RawImage::validateBmpHeaderFields(const BmpEditableHeaderFields& candidate) const
{
    HeaderEditResult result;

    if (candidate.biWidth <= 0)
        result.blockingErrors.add("Width must be positive.");

    if (candidate.biHeight == 0)
        result.blockingErrors.add("Height cannot be zero.");
    else if (candidate.biHeight == INT32_MIN)
        result.blockingErrors.add("Height is out of range.");

    if (candidate.biWidth > maxDimension
        || (candidate.biHeight != INT32_MIN && std::abs((int64_t) candidate.biHeight) > maxDimension))
        result.blockingErrors.add("Width/height must not exceed " + juce::String(maxDimension) + ".");

    const int64_t totalContentSize = (int64_t) headerBytes.getSize() + (int64_t) pixelBytes.getSize();

    if (candidate.bfOffBits < minimumBmpHeaderSize)
        result.blockingErrors.add("Pixel data offset must be at least " + juce::String((int) minimumBmpHeaderSize)
                                   + " bytes (the full BMP header) — this app is about to serialise a full header back into that space.");
    else if ((int64_t) candidate.bfOffBits >= totalContentSize)
        result.blockingErrors.add("Pixel data offset is past the end of the file.");
    else
    {
        // Warning-only: this app's own renderer already soft-clips out-of-range
        // byte offsets to black rather than crashing, so a header that declares
        // more pixel data than actually exists degrades gracefully — flag it,
        // don't block it.
        const int derivedChannels = juce::jmax(1, candidate.biBitCount / 8);
        const int64_t rowStride64 = computeBmpRowStride(candidate.biWidth, derivedChannels);
        const int64_t declaredHeight = std::abs((int64_t) candidate.biHeight);
        const int64_t declaredPixelSize = rowStride64 * declaredHeight;
        const int64_t availableAfterOffset = totalContentSize - (int64_t) candidate.bfOffBits;

        if (declaredPixelSize > availableAfterOffset)
            result.warnings.add("Declared pixel data (" + juce::String(declaredPixelSize) + " bytes) exceeds what's "
                                 "actually available (" + juce::String(availableAfterOffset) + ") — the image will "
                                 "render with black regions past the available bytes.");
    }

    if (candidate.biBitCount != 24)
        result.warnings.add("Bit depth " + juce::String(candidate.biBitCount) + " is not 24-bit — this app doesn't "
                             "actually decode other bit depths, so the render will look wrong (glitch, not a crash).");

    if (candidate.biCompression != 0)
        result.warnings.add("Compression " + juce::String(candidate.biCompression) + " is not BI_RGB (uncompressed) "
                             "— this app doesn't decode compressed pixel data, so the raw bytes will render as noise.");

    result.ok = result.blockingErrors.isEmpty();
    return result;
}

void RawImage::moveHeaderPixelBoundary(uint32_t newOffBits)
{
    const size_t oldOffBits = headerBytes.getSize();

    if ((size_t) newOffBits == oldOffBits)
        return;

    if ((size_t) newOffBits > oldOffBits)
    {
        const size_t delta = (size_t) newOffBits - oldOffBits;
        headerBytes.append(pixelBytes.getData(), delta);
        pixelBytes.removeSection(0, delta);
    }
    else
    {
        const size_t delta = oldOffBits - (size_t) newOffBits;
        const size_t moveStart = oldOffBits - delta; // == newOffBits
        pixelBytes.insert(static_cast<const uint8_t*>(headerBytes.getData()) + moveStart, delta, 0);
        headerBytes.setSize(moveStart, true);
    }
}

void RawImage::deriveBmpGeometryFromHeaderBytes()
{
    if (format != Format::bmp || headerBytes.getSize() < minimumBmpHeaderSize)
        return;

    const auto* bytes = static_cast<const uint8_t*>(headerBytes.getData());
    const int32_t declaredWidth  = readI32LE(bytes + 18);
    const int32_t declaredHeight = readI32LE(bytes + 22);
    const uint16_t bitCount      = readU16LE(bytes + 28);

    width = declaredWidth;
    height = (int) std::abs((int64_t) declaredHeight);
    bottomUp = declaredHeight > 0;
    channels = juce::jmax(1, (int) bitCount / 8);
    rowStride = (int) computeBmpRowStride(width, channels);
}

RawImage::HeaderEditResult RawImage::applyBmpHeaderFields(const BmpEditableHeaderFields& candidate)
{
    auto result = validateBmpHeaderFields(candidate);

    if (! result.ok)
        return result;

    moveHeaderPixelBoundary(candidate.bfOffBits);

    auto* bytes = static_cast<uint8_t*>(headerBytes.getData());
    writeU32LE(bytes + 10, candidate.bfOffBits);
    writeI32LE(bytes + 18, candidate.biWidth);
    writeI32LE(bytes + 22, candidate.biHeight);
    writeU16LE(bytes + 28, candidate.biBitCount);
    writeU32LE(bytes + 30, candidate.biCompression);

    deriveBmpGeometryFromHeaderBytes();
    planesDirty = true;
    visualOrderDirty = true;
    plainImageDirty = true;

    return result;
}

void RawImage::restoreSnapshot(juce::MemoryBlock newHeaderBytes, juce::MemoryBlock newPixelBytes)
{
    headerBytes = std::move(newHeaderBytes);
    pixelBytes = std::move(newPixelBytes);
    planesDirty = true;
    visualOrderDirty = true;
    plainImageDirty = true;

    if (format == Format::bmp)
        deriveBmpGeometryFromHeaderBytes();
}

void RawImage::setPixelBytes(juce::MemoryBlock newPixelBytes)
{
    pixelBytes = std::move(newPixelBytes);
    planesDirty = true;
    visualOrderDirty = true;
    plainImageDirty = true;
}

int RawImage::channelByteOffset(Format format, Channel channel)
{
    // BMP stores pixels as BGR; PNM P6 and PNG store RGB(A). BMP's alpha
    // case is unreachable in practice -- the loader always collapses 32-bit
    // BMP down to 3 channels -- but is given a definite value regardless.
    if (format == Format::bmp)
    {
        switch (channel)
        {
            case Channel::blue:  return 0;
            case Channel::green: return 1;
            case Channel::red:   return 2;
            case Channel::alpha: return 3;
        }
    }
    else
    {
        switch (channel)
        {
            case Channel::red:   return 0;
            case Channel::green: return 1;
            case Channel::blue:  return 2;
            case Channel::alpha: return 3;
        }
    }

    return 0;
}

void RawImage::ensurePlanesUpToDate() const
{
    if (! planesDirty)
        return;

    if (! hasChannelPlanes())
    {
        for (auto& plane : channelPlanes)
            plane.reset();

        planesDirty = false;
        return;
    }

    // channels is 3 (RGB) or 4 (RGBA) whenever hasChannelPlanes() is true --
    // only that many planes are actually populated; any plane beyond that
    // (the alpha plane, for a 3-channel image) is cleared instead.
    for (int c = 0; c < channels; ++c)
        channelPlanes[c].setSize((size_t) width * (size_t) height, false);
    for (int c = channels; c < 4; ++c)
        channelPlanes[c].reset();

    uint8_t* planePtrs[4] = { static_cast<uint8_t*>(channelPlanes[0].getData()),
                              static_cast<uint8_t*>(channelPlanes[1].getData()),
                              static_cast<uint8_t*>(channelPlanes[2].getData()),
                              channels == 4 ? static_cast<uint8_t*>(channelPlanes[3].getData()) : nullptr };

    const auto* px = static_cast<const uint8_t*>(pixelBytes.getData());
    const size_t available = pixelBytes.getSize();

    for (int y = 0; y < height; ++y)
    {
        const int sourceRow = bottomUp ? (height - 1 - y) : y;
        const size_t rowOffset = (size_t) sourceRow * (size_t) rowStride;

        for (int x = 0; x < width; ++x)
        {
            const size_t byteOffset = rowOffset + (size_t) x * (size_t) channels;
            const size_t planeIndex = (size_t) y * (size_t) width + (size_t) x;

            for (int c = 0; c < channels; ++c)
            {
                const size_t srcOffset = byteOffset + (size_t) channelByteOffset(format, (Channel) c);
                planePtrs[c][planeIndex] = srcOffset < available ? px[srcOffset] : 0;
            }
        }
    }

    planesDirty = false;
}

const juce::MemoryBlock& RawImage::getChannelPlane(Channel channel) const
{
    ensurePlanesUpToDate();
    return channelPlanes[(int) channel];
}

void RawImage::spliceChannelIntoInterleaved(uint8_t* interleavedBytes, size_t interleavedSize,
                                             const juce::MemoryBlock& newPlaneBytes, int channelOffset) const
{
    const auto* plane = static_cast<const uint8_t*>(newPlaneBytes.getData());
    const size_t planeSize = newPlaneBytes.getSize();

    for (int y = 0; y < height; ++y)
    {
        const int sourceRow = bottomUp ? (height - 1 - y) : y;
        const size_t rowOffset = (size_t) sourceRow * (size_t) rowStride;

        for (int x = 0; x < width; ++x)
        {
            const size_t byteOffset = rowOffset + (size_t) x * (size_t) channels + (size_t) channelOffset;
            const size_t planeIndex = (size_t) y * (size_t) width + (size_t) x;

            if (byteOffset < interleavedSize && planeIndex < planeSize)
                interleavedBytes[byteOffset] = plane[planeIndex];
        }
    }
}

void RawImage::applyChannelBytes(Channel channel, const juce::MemoryBlock& newPlaneBytes)
{
    if (! hasChannelPlanes())
        return;

    ensurePlanesUpToDate(); // guarantee all 3 caches are valid before overwriting just one

    spliceChannelIntoInterleaved(static_cast<uint8_t*>(pixelBytes.getData()), pixelBytes.getSize(),
                                 newPlaneBytes, channelByteOffset(format, channel));

    channelPlanes[(int) channel] = newPlaneBytes; // direct cache update -- the other two are untouched
    visualOrderDirty = true; // pixelBytes changed, so the whole-buffer visual-order cache is stale
    plainImageDirty = true;
}

juce::MemoryBlock RawImage::previewWithChannelBytes(Channel channel, const juce::MemoryBlock& newPlaneBytes) const
{
    juce::MemoryBlock result(pixelBytes);

    if (hasChannelPlanes())
        spliceChannelIntoInterleaved(static_cast<uint8_t*>(result.getData()), result.getSize(),
                                     newPlaneBytes, channelByteOffset(format, channel));

    return result;
}

void RawImage::remapPixelRowOrder(uint8_t* rawBytes, size_t rawSize, uint8_t* canonicalBytes, bool toCanonical) const
{
    const int rowBytes = width * channels;

    for (int y = 0; y < height; ++y)
    {
        const int sourceRow = bottomUp ? (height - 1 - y) : y;
        const size_t rawRowOffset = (size_t) sourceRow * (size_t) rowStride;
        const size_t canonicalRowOffset = (size_t) y * (size_t) rowBytes;
        const size_t copyLength = juce::jmin((size_t) rowBytes,
                                              rawRowOffset < rawSize ? rawSize - rawRowOffset : (size_t) 0);

        if (toCanonical)
        {
            if (copyLength > 0)
                std::memcpy(canonicalBytes + canonicalRowOffset, rawBytes + rawRowOffset, copyLength);

            if (copyLength < (size_t) rowBytes) // out-of-bounds tail (malformed/truncated source): leave at 0
                std::memset(canonicalBytes + canonicalRowOffset + copyLength, 0, (size_t) rowBytes - copyLength);
        }
        else if (copyLength > 0)
        {
            std::memcpy(rawBytes + rawRowOffset, canonicalBytes + canonicalRowOffset, copyLength);
        }
    }
}

void RawImage::ensureVisualOrderUpToDate() const
{
    if (! visualOrderDirty)
        return;

    const size_t canonicalSize = (size_t) width * (size_t) height * (size_t) channels;
    visualOrderedPixelBytes.setSize(canonicalSize, false);

    // Read-only direction (toCanonical): remapPixelRowOrder never writes through
    // rawBytes in this direction, so casting away pixelBytes' constness here is safe.
    auto* rawBytes = const_cast<uint8_t*>(static_cast<const uint8_t*>(pixelBytes.getData()));
    remapPixelRowOrder(rawBytes, pixelBytes.getSize(), static_cast<uint8_t*>(visualOrderedPixelBytes.getData()), true);

    visualOrderDirty = false;
}

const juce::MemoryBlock& RawImage::getVisualOrderedPixelBytes() const
{
    ensureVisualOrderUpToDate();
    return visualOrderedPixelBytes;
}

void RawImage::applyVisualOrderedBytes(const juce::MemoryBlock& newVisualOrderBytes)
{
    // Splice-back direction only reads newVisualOrderBytes and writes pixelBytes,
    // so casting away its constness here is safe.
    auto* canonicalBytes = const_cast<uint8_t*>(static_cast<const uint8_t*>(newVisualOrderBytes.getData()));
    remapPixelRowOrder(static_cast<uint8_t*>(pixelBytes.getData()), pixelBytes.getSize(), canonicalBytes, false);

    // Unlike applyChannelBytes()'s direct cache update, don't trust
    // newVisualOrderBytes verbatim as the new cache: if pixelBytes is shorter
    // than width*height*channels (the header editor's own tolerated "declared
    // pixel data exceeds available bytes" case — see validateBmpHeaderFields()),
    // the splice above silently drops whatever didn't fit, so the cache must
    // reflect that truncation rather than the possibly-larger input buffer.
    // Recomputing from the just-written pixelBytes guarantees that.
    visualOrderDirty = true;
    planesDirty = true; // pixelBytes changed, so the per-channel plane cache is stale
    plainImageDirty = true;
}

juce::MemoryBlock RawImage::previewWithVisualOrderedBytes(const juce::MemoryBlock& newVisualOrderBytes) const
{
    juce::MemoryBlock result(pixelBytes);

    auto* canonicalBytes = const_cast<uint8_t*>(static_cast<const uint8_t*>(newVisualOrderBytes.getData()));
    remapPixelRowOrder(static_cast<uint8_t*>(result.getData()), result.getSize(), canonicalBytes, false);

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

void RawImage::readRawRgbAt(const uint8_t* px, size_t available, size_t byteOffset,
                             juce::uint8& r, juce::uint8& g, juce::uint8& b, juce::uint8& a) const
{
    r = g = b = 0;
    a = 255; // opaque default -- overwritten below only for a real 4-channel (RGBA PNG) image

    if (byteOffset + (size_t) channels <= available)
    {
        if (channels == 3 || channels == 4)
        {
            if (format == Format::bmp)
            {
                // BMP stores pixels as BGR (never 4-channel in this app -- see channelByteOffset()).
                b = px[byteOffset + 0];
                g = px[byteOffset + 1];
                r = px[byteOffset + 2];
            }
            else
            {
                r = px[byteOffset + 0];
                g = px[byteOffset + 1];
                b = px[byteOffset + 2];

                if (channels == 4)
                    a = px[byteOffset + 3];
            }
        }
        else
        {
            r = g = b = px[byteOffset];
        }
    }
}

template <typename PixelType>
void RawImage::writePixelRows(const juce::Image::BitmapData& bitmap, const uint8_t* px, size_t available,
                               int startY, int endY) const
{
    for (int y = startY; y <= endY; ++y)
    {
        const int sourceRow = bottomUp ? (height - 1 - y) : y;
        const size_t rowOffset = (size_t) sourceRow * (size_t) rowStride;
        auto* dest = reinterpret_cast<PixelType*>(bitmap.getPixelPointer(0, y));

        for (int x = 0; x < width; ++x)
        {
            const size_t byteOffset = rowOffset + (size_t) x * (size_t) channels;
            juce::uint8 r, g, b, a;
            readRawRgbAt(px, available, byteOffset, r, g, b, a);
            dest[x].setARGB(a, r, g, b); // a is always 255 unless this is a real RGBA (alpha PNG) source

            // PixelARGB is JUCE's *premultiplied* internal storage (see
            // juce_PixelFormats.h) -- setARGB() above is a raw setter that just
            // stores whatever's given verbatim, so without this, every consumer
            // that assumes premultiplied storage (PNGImageFormat's writer,
            // Graphics::drawImage's compositing) would double-divide/composite
            // wrong for any translucent pixel. A no-op for PixelRGB (alpha is
            // always 0xff there), so unconditional here is correct for both
            // template instantiations.
            dest[x].premultiply();
        }
    }
}

// Explicit instantiation: these are the only two pixel layouts toJuceImage*()
// ever hands to writePixelRows() (selected once from bitmap.pixelFormat by
// the callers below) -- see writePixelRows()'s declaration in RawImage.h for
// why the branch must happen there and not per-pixel.
template void RawImage::writePixelRows<juce::PixelRGB>(const juce::Image::BitmapData&, const uint8_t*, size_t, int, int) const;
template void RawImage::writePixelRows<juce::PixelARGB>(const juce::Image::BitmapData&, const uint8_t*, size_t, int, int) const;

juce::Image RawImage::toJuceImage() const
{
    ensurePlainImageUpToDate();
    return cachedPlainImage; // cheap COW handle copy -- callers here only ever read it
}

void RawImage::ensurePlainImageUpToDate() const
{
    if (! plainImageDirty)
        return;

    cachedPlainImage = toJuceImageFromBytes(pixelBytes);
    plainImageDirty = false;
}

juce::Image RawImage::toJuceImageFromBytes(const juce::MemoryBlock& bytesToRender) const
{
    juce::Image image(hasAlphaChannel() ? juce::Image::ARGB : juce::Image::RGB, width, height, true);
    juce::Image::BitmapData bitmap(image, juce::Image::BitmapData::writeOnly);

    const auto* px = static_cast<const uint8_t*>(bytesToRender.getData());
    const size_t available = bytesToRender.getSize();

    // bitmap.pixelFormat is a runtime property of whichever ImageType actually
    // backed this image, not necessarily Image::RGB as requested above -- on
    // macOS specifically, CoreGraphicsImage silently upgrades RGB to ARGB (see
    // writePixelRows()'s doc comment in RawImage.h). Branching once here,
    // outside the pixel loop, is what lets writePixelRows() stay branch-free.
    switch (bitmap.pixelFormat)
    {
        case juce::Image::ARGB:  writePixelRows<juce::PixelARGB>(bitmap, px, available, 0, height - 1); break;
        case juce::Image::RGB:   writePixelRows<juce::PixelRGB>(bitmap, px, available, 0, height - 1); break;

        case juce::Image::SingleChannel:
        case juce::Image::UnknownFormat:
        default:
            jassertfalse; // toJuceImage*() always constructs an opaque RGB/ARGB colour image
            break;
    }

    return image;
}

juce::Image RawImage::toJuceImageFromBytesScoped(const juce::MemoryBlock& bytesToRender, int firstRow, int lastRow) const
{
    ensurePlainImageUpToDate();

    juce::Image result = cachedPlainImage;
    result.duplicateIfShared(); // REQUIRED: makes `result` a genuinely independent
                                 // pixel buffer before writing -- opening a writable
                                 // BitmapData does NOT do this automatically, and
                                 // without it this would silently corrupt the shared
                                 // cachedPlainImage.

    juce::Image::BitmapData bitmap(result, juce::Image::BitmapData::writeOnly);
    const auto* px = static_cast<const uint8_t*>(bytesToRender.getData());
    const size_t available = bytesToRender.getSize();

    const int startY = juce::jlimit(0, height - 1, juce::jmin(firstRow, lastRow));
    const int endY = juce::jlimit(0, height - 1, juce::jmax(firstRow, lastRow));

    // See toJuceImageFromBytes() above for why this branches on the bitmap's
    // actual runtime pixelFormat rather than assuming RGB.
    switch (bitmap.pixelFormat)
    {
        case juce::Image::ARGB:  writePixelRows<juce::PixelARGB>(bitmap, px, available, startY, endY); break;
        case juce::Image::RGB:   writePixelRows<juce::PixelRGB>(bitmap, px, available, startY, endY); break;

        case juce::Image::SingleChannel:
        case juce::Image::UnknownFormat:
        default:
            jassertfalse; // toJuceImage*() always constructs an opaque RGB/ARGB colour image
            break;
    }

    return result;
}

std::optional<RawImage::HighlightOverlay> RawImage::computeHighlightOverlay(juce::Range<int> highlightByteRange) const
{
    if (highlightByteRange.isEmpty())
        return std::nullopt;

    const int rowBytes = width * channels;
    const int topRow = juce::jlimit(0, height - 1, highlightByteRange.getStart() / rowBytes);
    const int bottomRow = juce::jlimit(0, height - 1, (highlightByteRange.getEnd() - 1) / rowBytes);

    HighlightOverlay overlay;
    overlay.topRow = topRow;
    overlay.bottomRow = bottomRow;
    // Full image width rather than the exact intersected sub-range for a
    // partial boundary row -- a deliberate simplification (the marker lines
    // read as "here's the vertical extent of the selection" rather than
    // precisely which columns of that one row are selected).
    overlay.topStartColumn = overlay.bottomStartColumn = 0;
    overlay.topEndColumn = overlay.bottomEndColumn = width;
    return overlay;
}

juce::Range<int> RawImage::rowRangeToHighlightByteRange(juce::Range<int> rowRange) const
{
    const int rowBytes = width * channels;
    return { rowRange.getStart() * rowBytes, rowRange.getEnd() * rowBytes };
}

juce::Range<int> RawImage::rowRangeToChannelHighlightSampleRange(juce::Range<int> rowRange) const
{
    return { rowRange.getStart() * width, rowRange.getEnd() * width };
}

std::optional<RawImage::HighlightOverlay> RawImage::computeChannelHighlightOverlay(juce::Range<int> highlightPlaneSampleRange) const
{
    if (highlightPlaneSampleRange.isEmpty())
        return std::nullopt;

    const int topRow = juce::jlimit(0, height - 1, highlightPlaneSampleRange.getStart() / width);
    const int bottomRow = juce::jlimit(0, height - 1, (highlightPlaneSampleRange.getEnd() - 1) / width);

    HighlightOverlay overlay;
    overlay.topRow = topRow;
    overlay.bottomRow = bottomRow;
    // Full image width rather than the exact intersected sub-range -- see
    // the identical note in computeHighlightOverlay() above.
    overlay.topStartColumn = overlay.bottomStartColumn = 0;
    overlay.topEndColumn = overlay.bottomEndColumn = width;
    return overlay;
}
