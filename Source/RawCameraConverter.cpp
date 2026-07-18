#include "RawCameraConverter.h"

#include <ImageIO/ImageIO.h>
#include <CoreGraphics/CoreGraphics.h>
#include <cstring>
#include <memory>
#include <type_traits>
#include <vector>

namespace
{
    template <typename T>
    struct CFReleaser
    {
        void operator()(T ref) const noexcept { if (ref != nullptr) CFRelease(ref); }
    };

    template <typename T>
    using ScopedCFRef = std::unique_ptr<std::remove_pointer_t<T>, CFReleaser<T>>;

    // Mirrors RawImage's own private maxDimension bound (RawImage.cpp) --
    // duplicated here rather than shared, since no real camera sensor comes
    // remotely close to this; it only exists for an earlier, clearer error
    // message than a downstream BMP-loader rejection would otherwise give.
    constexpr int32_t maxDimension = 32768;

    void writeU32LE(uint8_t* p, uint32_t v) { p[0] = (uint8_t) v; p[1] = (uint8_t) (v >> 8); p[2] = (uint8_t) (v >> 16); p[3] = (uint8_t) (v >> 24); }
    void writeU16LE(uint8_t* p, uint16_t v) { p[0] = (uint8_t) v; p[1] = (uint8_t) (v >> 8); }
    void writeI32LE(uint8_t* p, int32_t v) { writeU32LE(p, (uint32_t) v); }

    // Fresh, self-consistent 54-byte BMP header (14-byte BITMAPFILEHEADER +
    // 40-byte BITMAPINFOHEADER) describing a bottom-up 24-bit uncompressed
    // image -- same byte layout as RawImage.cpp's own (private,
    // anonymous-namespace) buildBmp24Header(). Duplicated rather than shared
    // so this converter stays fully additive and never touches RawImage.cpp.
    juce::MemoryBlock buildBmp24Header(int32_t width, int32_t height, int64_t pixelDataSize)
    {
        juce::MemoryBlock header;
        header.setSize(54, true); // zero-initialised: all resolution/palette-count fields default to 0

        auto* b = static_cast<uint8_t*>(header.getData());
        b[0] = 'B'; b[1] = 'M';                               // bfType
        writeU32LE(b + 2,  (uint32_t) (54 + pixelDataSize));  // bfSize
        writeU32LE(b + 10, 54);                               // bfOffBits
        writeU32LE(b + 14, 40);                               // biSize (BITMAPINFOHEADER)
        writeI32LE(b + 18, width);                            // biWidth
        writeI32LE(b + 22, height);                           // biHeight (positive == bottom-up)
        writeU16LE(b + 26, 1);                                // biPlanes
        writeU16LE(b + 28, 24);                               // biBitCount
        writeU32LE(b + 30, 0);                                // biCompression (BI_RGB)
        writeU32LE(b + 34, (uint32_t) pixelDataSize);         // biSizeImage
        // biXPelsPerMeter/biYPelsPerMeter/biClrUsed/biClrImportant left at 0.
        return header;
    }

    int64_t computeBmpRowStride(int32_t width)
    {
        return ((((int64_t) width * 3) + 3) / 4) * 4;
    }

    bool readMagicBytes(const juce::File& file, uint8_t* buffer, int numBytes)
    {
        juce::FileInputStream stream(file);
        if (! stream.openedOk())
            return false;
        return stream.read(buffer, numBytes) == numBytes;
    }
}

bool RawCameraConverter::isRawCameraFile(const juce::File& file)
{
    uint8_t magic[16] = {};
    if (! readMagicBytes(file, magic, (int) sizeof(magic)))
        return false;

    static constexpr uint8_t rafMagic[] = { 'F','U','J','I','F','I','L','M','C','C','D','-','R','A','W' };
    if (std::memcmp(magic, rafMagic, sizeof(rafMagic)) == 0)
        return true;

    // TIFF magic, little- or big-endian -- DNG is TIFF-based (see header
    // comment: this only confirms "TIFF-shaped," not specifically DNG).
    if (magic[0] == 'I' && magic[1] == 'I' && magic[2] == 42 && magic[3] == 0)
        return true;
    if (magic[0] == 'M' && magic[1] == 'M' && magic[2] == 0 && magic[3] == 42)
        return true;

    return false;
}

bool RawCameraConverter::convertToBmp(const juce::File& sourceFile, const juce::File& destBmpFile, juce::String& errorMessage)
{
    const auto path = sourceFile.getFullPathName();
    ScopedCFRef<CFURLRef> url(CFURLCreateFromFileSystemRepresentation(
        kCFAllocatorDefault,
        (const UInt8*) path.toRawUTF8(),
        (CFIndex) path.getNumBytesAsUTF8(),
        false));

    if (url == nullptr)
    {
        errorMessage = "Could not resolve file path: " + path;
        return false;
    }

    ScopedCFRef<CGImageSourceRef> imageSource(CGImageSourceCreateWithURL(url.get(), nullptr));
    if (imageSource == nullptr)
    {
        errorMessage = "Unable to read " + sourceFile.getFileName()
                        + " -- not a recognised image/raw file, or its camera model isn't supported by this Mac's installed RAW decoder.";
        return false;
    }

    // ThumbnailFromImageAlways forces a full decode (not an embedded low-res
    // preview); WithTransform applies the EXIF/TIFF orientation tag, which
    // CGImageSourceCreateImageAtIndex alone does NOT do -- without it,
    // portrait-oriented photos would decode sideways/mirrored relative to
    // what Preview.app shows for the same file. MaxPixelSize is set high
    // enough that no real camera sensor gets downscaled.
    const int maxPixelSizeValue = 20000;
    ScopedCFRef<CFNumberRef> maxPixelSize(CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &maxPixelSizeValue));

    CFStringRef keys[]   = { kCGImageSourceCreateThumbnailFromImageAlways, kCGImageSourceCreateThumbnailWithTransform, kCGImageSourceThumbnailMaxPixelSize };
    CFTypeRef   values[] = { kCFBooleanTrue, kCFBooleanTrue, maxPixelSize.get() };
    ScopedCFRef<CFDictionaryRef> options(CFDictionaryCreate(kCFAllocatorDefault, (const void**) keys, (const void**) values, 3,
                                                             &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));

    ScopedCFRef<CGImageRef> cgImage(CGImageSourceCreateThumbnailAtIndex(imageSource.get(), 0, options.get()));
    if (cgImage == nullptr)
    {
        errorMessage = "Failed to decode " + sourceFile.getFileName() + " -- the file may be corrupt or use an unsupported raw variant.";
        return false;
    }

    return writeCGImageAsBmp(cgImage.get(), destBmpFile, errorMessage);
}

bool RawCameraConverter::writeCGImageAsBmp(CGImageRef image, const juce::File& destBmpFile, juce::String& errorMessage)
{
    const size_t width  = CGImageGetWidth(image);
    const size_t height = CGImageGetHeight(image);

    if (width == 0 || height == 0)
    {
        errorMessage = "Decoded image has invalid (zero) dimensions.";
        return false;
    }

    if (width > (size_t) maxDimension || height > (size_t) maxDimension)
    {
        errorMessage = "Decoded image is too large (" + juce::String(width) + "x" + juce::String(height) + ").";
        return false;
    }

    ScopedCFRef<CGColorSpaceRef> colourSpace(CGColorSpaceCreateWithName(kCGColorSpaceSRGB));
    const size_t srcBytesPerRow = width * 4; // tight BGRX8, stride chosen explicitly

    std::vector<uint8_t> topDownBuffer(srcBytesPerRow * height, 0);

    // kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little -> in-memory
    // byte order per pixel is [B, G, R, A] -- the standard BGRA8 idiom.
    // Premultiplied (rather than NoneSkipFirst) because CGContextDrawImage
    // reliably renders into it; every source pixel from a real camera-raw
    // photo is fully opaque, so premultiplication is a no-op on the RGB
    // values here. Caveat: isRawCameraFile() opportunistically also routes
    // any TIFF-shaped file through this path (see its own comment), so a
    // plain TIFF with genuine partial transparency would get composited
    // over this buffer's zero-initialised (transparent black) background
    // here, darkening its RGB values with no error surfaced -- acceptable
    // for this app's actual target (opaque camera sensor data), not for
    // arbitrary TIFFs.
    ScopedCFRef<CGContextRef> context(CGBitmapContextCreate(
        topDownBuffer.data(), width, height, 8, srcBytesPerRow, colourSpace.get(),
        kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little));

    if (context == nullptr)
    {
        errorMessage = "Could not create a bitmap context to decode the image into.";
        return false;
    }

    // No coordinate flip: CGContextDrawImage always draws with the source
    // image's row 0 at the top of the destination rect, independent of the
    // context's own coordinate orientation -- confirmed empirically (a plain,
    // unflipped draw already puts image row 0 at topDownBuffer's first row).
    // Adding a manual flip here would draw the image upside down.
    CGContextDrawImage(context.get(), CGRectMake(0, 0, (CGFloat) width, (CGFloat) height), image);

    const int64_t dstRowStride  = computeBmpRowStride((int32_t) width);
    const int64_t pixelDataSize = dstRowStride * (int64_t) height;

    juce::MemoryBlock pixelData;
    pixelData.setSize((size_t) pixelDataSize, true); // zero-init => row padding already 0
    auto* dst = static_cast<uint8_t*>(pixelData.getData());

    for (size_t bmpRow = 0; bmpRow < height; ++bmpRow)
    {
        const size_t srcRow = height - 1 - bmpRow; // BMP row 0 == image's visual bottom row
        const uint8_t* srcPixels = topDownBuffer.data() + srcRow * srcBytesPerRow;
        uint8_t* dstPixels = dst + (int64_t) bmpRow * dstRowStride;

        for (size_t x = 0; x < width; ++x)
        {
            const uint8_t* p = srcPixels + x * 4;
            dstPixels[x * 3 + 0] = p[0]; // B
            dstPixels[x * 3 + 1] = p[1]; // G
            dstPixels[x * 3 + 2] = p[2]; // R
        }
    }

    const auto header = buildBmp24Header((int32_t) width, (int32_t) height, pixelDataSize);

    if (destBmpFile.existsAsFile())
        destBmpFile.deleteFile();

    auto outputStream = destBmpFile.createOutputStream();
    if (outputStream == nullptr)
    {
        errorMessage = "Could not open " + destBmpFile.getFullPathName() + " for writing.";
        return false;
    }

    // Belt-and-braces alongside the deleteFile() above: guarantees the
    // "overwritten if it already exists" contract this function documents
    // even if deleteFile() silently failed (e.g. a locked/permission-denied
    // stale file) and FileOutputStream opened at some non-zero position.
    outputStream->setPosition(0);
    outputStream->truncate();

    if (! outputStream->write(header.getData(), header.getSize())
        || ! outputStream->write(pixelData.getData(), pixelData.getSize()))
    {
        errorMessage = "Failed to write BMP data to " + destBmpFile.getFullPathName();
        return false;
    }

    return true;
}
