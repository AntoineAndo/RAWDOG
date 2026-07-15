#include "RawImage.h"

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

    if (offBits >= data.getSize())
    {
        errorMessage = "BMP pixel data offset is out of range.";
        return nullptr;
    }

    auto result = std::make_unique<RawImage>();
    result->format = Format::bmp;
    result->width = width;
    result->height = std::abs(height);
    result->bottomUp = height > 0;
    result->channels = 3;
    result->rowStride = ((width * 3 + 3) / 4) * 4;

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

bool RawImage::writeToFile(const juce::File& file) const
{
    auto stream = file.createOutputStream();
    if (stream == nullptr)
        return false;

    stream->setPosition(0);
    stream->truncate();
    stream->write(headerBytes.getData(), headerBytes.getSize());
    stream->write(pixelBytes.getData(), pixelBytes.getSize());
    return true;
}

juce::Image RawImage::toJuceImage() const
{
    juce::Image image(juce::Image::RGB, width, height, true);
    juce::Image::BitmapData bitmap(image, juce::Image::BitmapData::writeOnly);

    const auto* px = static_cast<const uint8_t*>(pixelBytes.getData());
    const size_t available = pixelBytes.getSize();

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

            bitmap.setPixelColour(x, y, juce::Colour(r, g, b));
        }
    }

    return image;
}
