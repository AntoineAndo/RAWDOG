#pragma once

#include <juce_graphics/juce_graphics.h>
#include <cstdint>
#include <vector>
#include "SampleFormat.h"

// Loads/saves BMP (24-bit uncompressed) and PNM (raw P5/P6) files. pixelBytes
// is always available for databending. For BMP specifically, headerBytes is
// no longer fully opaque: the 5 fields that drive this app's own decoding
// (bfOffBits/biWidth/biHeight/biBitCount/biCompression) can be user-edited via
// applyBmpHeaderFields() — see HeaderEditorPanel. The other 11 documented BMP
// header fields are readable via getBmpHeaderFields() but never written back.
// PNM's headerBytes remains frozen verbatim source text, with no rebuild path.
class RawImage
{
public:
    static std::unique_ptr<RawImage> loadFromFile(const juce::File& file, juce::String& errorMessage);

    // Read-only snapshot of all 16 documented BITMAPFILEHEADER + BITMAPINFOHEADER
    // fields, parsed fresh from headerBytes on every call (no cached state to
    // drift out of sync). Only meaningful when getFormat() == Format::bmp — for
    // PNM this returns a default-constructed struct. If headerBytes is shorter
    // than the 54 bytes these fields occupy (possible for a malformed real-world
    // BMP whose declared bfOffBits is itself < 54), fields past what's actually
    // present are left at their default value rather than read out of bounds.
    struct BmpHeaderFields
    {
        uint16_t bfType = 0, bfReserved1 = 0, bfReserved2 = 0;
        uint32_t bfSize = 0, bfOffBits = 0;
        uint32_t biSize = 0, biSizeImage = 0, biClrUsed = 0, biClrImportant = 0;
        int32_t  biWidth = 0, biHeight = 0, biXPelsPerMeter = 0, biYPelsPerMeter = 0;
        uint16_t biPlanes = 0, biBitCount = 0;
        uint32_t biCompression = 0;
    };

    // The subset of BmpHeaderFields a user can actually edit — the ones that
    // drive this app's own decode/render logic. Every other documented field is
    // display-only (see BmpHeaderFields) and is never written back to headerBytes.
    struct BmpEditableHeaderFields
    {
        uint32_t bfOffBits = 0;
        int32_t  biWidth = 0, biHeight = 0;
        uint16_t biBitCount = 24;
        uint32_t biCompression = 0;
    };

    struct HeaderEditResult
    {
        bool ok = false;
        juce::StringArray blockingErrors; // non-empty => ok==false, candidate was rejected
        juce::StringArray warnings;       // informational only, never blocks ok
    };

    BmpHeaderFields getBmpHeaderFields() const;

    // Pure check — does not mutate *this. Safe to call on every keystroke to
    // drive an editor's Apply-button enablement and warning text.
    HeaderEditResult validateBmpHeaderFields(const BmpEditableHeaderFields& candidate) const;

    // Re-validates internally; on a blocking failure, *this is left untouched
    // and the same result validateBmpHeaderFields() would give is returned. On
    // success: reshuffles bytes between headerBytes/pixelBytes if bfOffBits
    // changed, writes the 5 fields back into headerBytes, and re-derives
    // width/height/rowStride/channels/bottomUp to match.
    HeaderEditResult applyBmpHeaderFields(const BmpEditableHeaderFields& candidate);

    // Restores a prior (headerBytes, pixelBytes) pair — e.g. from an undo/redo
    // snapshot captured from this same RawImage earlier. For BMP, also
    // re-derives width/height/rowStride/channels/bottomUp from the restored
    // headerBytes. For PNM, geometry never changes across any edit in this app,
    // so this is just a plain restore of both blocks.
    void restoreSnapshot(juce::MemoryBlock newHeaderBytes, juce::MemoryBlock newPixelBytes);

    // Explicit setter for pixelBytes, replacing a direct field assignment from
    // outside this class — invalidates the per-channel plane cache below, the
    // same way restoreSnapshot()/applyBmpHeaderFields() already do internally.
    void setPixelBytes(juce::MemoryBlock newPixelBytes);

    enum class Channel { red = 0, green = 1, blue = 2 };

    // Only meaningful for a 3-channel chunky image (BMP-24bit, PNM-P6colour).
    // False for grayscale PNM, and — after a header edit that changes
    // biBitCount away from 24 — false for BMP too, since
    // deriveBmpGeometryFromHeaderBytes() re-derives channels from biBitCount.
    bool hasChannelPlanes() const { return channels == 3; }

    // One byte per pixel for the given channel, in visual top-down row-major
    // order (index i == pixel (x = i % width, y = i / width) — matching what
    // toJuceImage() displays, not pixelBytes' own row order/stride, which for
    // BMP can be bottom-up and 4-byte padded). Lazily (re)computed from
    // pixelBytes on first access after pixelBytes last changed; a cheap linear
    // pass even at 1080p, safe to call repeatedly. Returns an empty block if
    // ! hasChannelPlanes().
    const juce::MemoryBlock& getChannelPlane(Channel channel) const;

    // Re-interleaves newPlaneBytes (same order as getChannelPlane(), ideally
    // width*height bytes) back into pixelBytes for just this one channel,
    // leaving every other channel's interleaved bytes byte-for-byte untouched.
    // Updates that channel's own cache entry directly (no recompute) and
    // deliberately leaves the other two channels' cache entries alone.
    void applyChannelBytes(Channel channel, const juce::MemoryBlock& newPlaneBytes);

    // Pure/non-mutating: a full copy of pixelBytes with just one channel's
    // interleaved bytes replaced by newPlaneBytes, without touching *this or
    // its plane cache — for a non-destructive live preview tick that
    // shouldn't perturb cache state a real Apply will later rely on.
    juce::MemoryBlock previewWithChannelBytes(Channel channel, const juce::MemoryBlock& newPlaneBytes) const;

    // The whole-buffer (still interleaved, not deinterleaved-per-channel)
    // analogue of getChannelPlane(): pixelBytes reordered into visual
    // top-down, unpadded row-major order. Exists for the same reason
    // getChannelPlane() does — a bottom-up BMP's byte 0 is the image's
    // *bottom* row, so using pixelBytes' raw offset directly as a waveform
    // sample index made selecting the end of the (non-split) waveform
    // highlight the top of the image instead of the bottom, and was
    // inconsistent with the split-channel waveform (which already reorders
    // via getChannelPlane()). Lazily cached (visualOrderDirty), invalidated
    // at the same sites as the channel-plane cache. A no-op copy when the
    // layout is already canonical (PNM; a header-edited BMP with
    // bottomUp==false and no row padding) — the same remap loop handles both
    // cases without special-casing.
    const juce::MemoryBlock& getVisualOrderedPixelBytes() const;

    // Inverse of the above: splices newVisualOrderBytes (must be
    // width*height*channels bytes, same layout as getVisualOrderedPixelBytes())
    // back into pixelBytes' real (possibly bottom-up/padded) row layout.
    // Mutating; invalidates the channel-plane cache since pixelBytes changed.
    // The whole-buffer analogue of applyChannelBytes().
    void applyVisualOrderedBytes(const juce::MemoryBlock& newVisualOrderBytes);

    // Pure/non-mutating counterpart, for a live-preview tick — same
    // relationship previewWithChannelBytes() has to applyChannelBytes().
    juce::MemoryBlock previewWithVisualOrderedBytes(const juce::MemoryBlock& newVisualOrderBytes) const;

    // Exports the current pixelBytes as a PNG, regardless of the format the
    // image was originally loaded from — encodes toJuceImage() (no selection
    // highlight) via juce::PNGImageFormat, so the exported file is always a
    // valid, widely-viewable image rather than a raw BMP/PNM reconstruction.
    bool writeToPngFile(const juce::File& file) const;

    // Renders the current pixelBytes (post- or pre-processing) as a juce::Image for preview.
    // highlightByteRange, if non-empty, tints the pixels whose position falls within it — used
    // to show which part of the image the current (interleaved) waveform selection maps onto.
    // The range is in *visual top-down, unpadded* byte order (see getVisualOrderedPixelBytes()),
    // matching what the interleaved waveform's sample index now means — NOT necessarily
    // pixelBytes' own raw file-storage (possibly bottom-up/padded) order.
    juce::Image toJuceImage(juce::Range<int> highlightByteRange = {}) const
    { return toJuceImageFromBytes(pixelBytes, highlightByteRange); }

    // Same rendering as toJuceImage(), but reads bytesToRender instead of this->pixelBytes —
    // format metadata (width/height/rowStride/channels/bottomUp/format) still comes from this.
    // Lets a caller render an uncommitted candidate buffer (e.g. a live plugin preview) for
    // display without ever mutating pixelBytes itself. bytesToRender itself is still expected
    // in pixelBytes' own real (possibly bottom-up/padded) row layout — only highlightByteRange
    // is in visual-order coordinates; see toJuceImage() above.
    juce::Image toJuceImageFromBytes(const juce::MemoryBlock& bytesToRender,
                                      juce::Range<int> highlightByteRange = {}) const;

    // Distinct overload (not an optional param on the one above) so every
    // existing interleaved-mode call site is untouched: highlights a
    // plane-sample range (in getChannelPlane()'s coordinate space, not a byte
    // range in the interleaved buffer) on one channel's lane, outlined in that
    // channel's own colour instead of the fixed yellow above.
    juce::Image toJuceImage(Channel highlightChannel, juce::Range<int> highlightPlaneSampleRange) const
    { return toJuceImageFromBytes(pixelBytes, highlightChannel, highlightPlaneSampleRange); }

    juce::Image toJuceImageFromBytes(const juce::MemoryBlock& bytesToRender,
                                      Channel highlightChannel,
                                      juce::Range<int> highlightPlaneSampleRange) const;

    enum class Format { bmp, pnmBinary, pnmGray };

    // What format this image was actually loaded from (BMP vs PNM) — used
    // internally to interpret pixelBytes' layout (row order, channel order).
    Format getFormat() const { return format; }

    // Per-image, session-level choice of how pixelBytes' bytes map onto float
    // samples for plugin processing/waveform display. Defaults to bipolar
    // (today's behaviour); see SampleFormat.h and PROJECT.md for why unipolar
    // exists. Not part of undo/redo history — a view setting, not an edit.
    SampleFormat::Mode getSampleMode() const { return sampleMode; }
    void setSampleMode(SampleFormat::Mode newMode) { sampleMode = newMode; }

    juce::MemoryBlock headerBytes;
    juce::MemoryBlock pixelBytes;

private:
    Format format = Format::bmp;
    SampleFormat::Mode sampleMode = SampleFormat::Mode::bipolar;
    int width = 0;
    int height = 0;
    int rowStride = 0;   // bytes per row, including any padding (BMP only; equals width*channels for PNM)
    int channels = 0;    // 3 for BMP/P6, 1 for P5 at load time; user-editable thereafter for BMP
    bool bottomUp = true; // BMP only

    // Shared between loadBmp() and the header-editor's validator, so the two
    // can never drift apart.
    static constexpr int32_t maxDimension = 32768;
    // BITMAPFILEHEADER (14 bytes) + BITMAPINFOHEADER (40 bytes) — the editor's
    // own floor on bfOffBits, since applyBmpHeaderFields() always serializes a
    // full 54-byte struct back into headerBytes. loadBmp() itself has no such
    // floor and tolerates a smaller declared offset in a file loaded from disk.
    static constexpr uint32_t minimumBmpHeaderSize = 54;

    static std::unique_ptr<RawImage> loadBmp(const juce::File&, juce::String& errorMessage);
    static std::unique_ptr<RawImage> loadPnm(const juce::File&, juce::String& errorMessage);

    static int64_t computeBmpRowStride(int32_t width, int channels);

    // Moves the headerBytes/pixelBytes boundary to newOffBits, re-labelling
    // bytes between the two blocks without mutating their content: growing the
    // offset pulls former leading pixel bytes into the header; shrinking it
    // pushes trailing header bytes into pixel data. headerBytes.size() +
    // pixelBytes.size() is conserved, so grow-then-shrink-back round-trips
    // losslessly.
    void moveHeaderPixelBoundary(uint32_t newOffBits);

    // Re-derives width/height/rowStride/channels/bottomUp from the 5
    // structural fields currently stored in headerBytes — shared by
    // applyBmpHeaderFields() and restoreSnapshot().
    void deriveBmpGeometryFromHeaderBytes();

    // Per-channel plane cache: lazily (re)computed by ensurePlanesUpToDate()
    // the next time getChannelPlane() is called after pixelBytes changed.
    // Deliberately NOT recomputed eagerly on every mutation — deinterleaving
    // is a cheap linear pass but a wasted one if split-channel view is never
    // opened, so a dirty flag defers the cost to the first actual need.
    mutable juce::MemoryBlock channelPlanes[3];
    mutable bool planesDirty = true;

    void ensurePlanesUpToDate() const;

    // Byte offset of a channel within one interleaved pixel: BMP stores BGR
    // (blue=0, green=1, red=2), PNM P6 stores RGB (red=0, green=1, blue=2).
    static int channelByteOffset(Format format, Channel channel);

    // Shared splice-back loop used by both applyChannelBytes() (mutating) and
    // previewWithChannelBytes() (pure) — writes newPlaneBytes into
    // interleavedBytes at this channel's stride positions, leaving every
    // other channel's bytes in interleavedBytes untouched.
    void spliceChannelIntoInterleaved(uint8_t* interleavedBytes, size_t interleavedSize,
                                       const juce::MemoryBlock& newPlaneBytes, int channelOffset) const;

    // Whole-buffer visual-order cache backing getVisualOrderedPixelBytes() —
    // same lazy-recompute idiom as channelPlanes/planesDirty above, just for
    // the still-interleaved (not deinterleaved) whole buffer.
    mutable juce::MemoryBlock visualOrderedPixelBytes;
    mutable bool visualOrderDirty = true;

    void ensureVisualOrderUpToDate() const;

    // Shared per-row remap loop behind getVisualOrderedPixelBytes() (via
    // ensureVisualOrderUpToDate()) and previewWithVisualOrderedBytes()/
    // applyVisualOrderedBytes()'s inverse splice: copies width*channels bytes
    // per row between rawBytes (pixelBytes' own possibly-bottom-up/padded row
    // layout) and canonicalBytes (always exactly width*height*channels bytes,
    // top-down, unpadded). toCanonical selects the direction — true copies
    // raw->canonical (the "get" direction), false copies canonical->raw (the
    // "apply"/splice-back direction).
    void remapPixelRowOrder(uint8_t* rawBytes, size_t rawSize,
                             uint8_t* canonicalBytes, bool toCanonical) const;

    // The shared per-pixel border-drawing loop behind both toJuceImageFromBytes
    // overloads: rowSelection[y] is the selected-column range for screen row y
    // (empty vector == no highlight at all), in whichever coordinate space the
    // caller already reduced its highlight range to (byte-range-in-interleaved
    // for the plain overload, plane-sample-range for the channel overload).
    juce::Image renderWithRowSelection(const juce::MemoryBlock& bytesToRender,
                                        const std::vector<juce::Range<int>>& rowSelection,
                                        juce::Colour borderColour) const;
};
