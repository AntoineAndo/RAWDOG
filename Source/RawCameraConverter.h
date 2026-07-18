#pragma once

#include <juce_core/juce_core.h>
#include <CoreGraphics/CGImage.h>

// Converts Fujifilm RAF / Adobe DNG (and, incidentally, any other TIFF-based
// raw or plain TIFF file macOS's system RawCamera.bundle/ImageIO can decode)
// into a 24-bit uncompressed BMP that RawImage::loadFromFile() can already
// read natively -- so RawImage itself never has to learn IFD/mosaic/demosaic
// parsing. Uses CoreGraphics/ImageIO's CGImageSource C API directly (the same
// decoder backing Preview.app/QuickLook). Plain C API, no Objective-C(++).
//
// Gives the camera's/OS's demosaiced, colour-processed RGB image -- not the
// raw sensor mosaic bytes. There is no exposure/white-balance control; this
// is whatever macOS's default raw processing produces.
namespace RawCameraConverter
{
    // Cheap magic-byte sniff (reads only the first few bytes, not the whole
    // file -- camera raw files can be tens to hundreds of MB). Mirrors
    // RawImage::loadFromFile()'s own content-sniff style rather than trusting
    // the file extension:
    //  - RAF: literal ASCII "FUJIFILMCCD-RAW" at offset 0.
    //  - DNG: standard TIFF magic ("II*\0" little-endian or "MM\0*" big-endian)
    //    at offset 0 -- DNG is TIFF underneath, and confirming "this is
    //    specifically a DNG" rather than just "TIFF-shaped" would require
    //    parsing the IFD for tag 50706 (DNGVersion), the exact IFD-parsing
    //    complexity this project deliberately avoids project-wide (see
    //    PROJECT.md's TIFF-deferral note). Routing any TIFF-shaped file
    //    through ImageIO is a harmless, even useful, side effect (other
    //    TIFF-based camera raws decode too), not a false-positive risk.
    bool isRawCameraFile(const juce::File& file);

    // Decodes sourceFile via ImageIO and writes a fresh 24-bit uncompressed
    // BMP to destBmpFile (overwritten if it already exists). Returns true on
    // success; on failure returns false and fills errorMessage -- mirrors
    // RawImage::loadFromFile()'s own out-param convention.
    bool convertToBmp(const juce::File& sourceFile, const juce::File& destBmpFile, juce::String& errorMessage);

    // The pixel-normalisation/BMP-writing core, factored out of convertToBmp()
    // so a throwaway verification harness can drive it with a synthetic,
    // in-process CGImage instead of a real camera file -- see PROJECT.md's
    // "Build & run" section for this project's verification convention.
    // Ordinary callers should use convertToBmp() above instead.
    bool writeCGImageAsBmp(CGImageRef image, const juce::File& destBmpFile, juce::String& errorMessage);
}
