#pragma once

#include <juce_graphics/juce_graphics.h>
#include <cstdint>
#include <optional>
#include "SampleFormat.h"

// Loads/saves BMP (24-bit uncompressed) and PNM (raw P5/P6) files, and loads
// PNG (any depth/colour type JUCE's own decoder handles, decoded to 8-bit
// RGB or RGBA) and JPEG (always 3-channel RGB — JPEG has no alpha channel).
// pixelBytes is always available for databending. For BMP specifically, the 5
// header fields that drive this app's own decoding (bfOffBits/biWidth/
// biHeight/biBitCount/biCompression) can be user-edited via
// applyBmpHeaderFields() — see HeaderEditorPanel. The other 11 documented BMP
// header fields are readable via getBmpHeaderFields() but never written back.
// PNM's headerBytes remains frozen verbatim source text, with no rebuild
// path. PNG and JPEG have no fixed-offset "protected region" the way BMP/PNM
// do (chunk/segment-based, compressed), so a loaded PNG or JPEG's headerBytes
// is always empty — there is nothing to preserve or edit.
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

    // Setter for pixelBytes — invalidates the per-channel plane cache below,
    // just as restoreSnapshot()/applyBmpHeaderFields() do internally, so the
    // cache never goes stale relative to whatever last touched pixelBytes.
    void setPixelBytes(juce::MemoryBlock newPixelBytes);

    enum class Channel { red = 0, green = 1, blue = 2, alpha = 3 };

    // Only meaningful for a 3-channel chunky image (BMP-24bit, PNM-P6colour)
    // or a PNG with a real alpha channel. False for grayscale PNM, and —
    // after a header edit that changes biBitCount away from 24 — false for
    // BMP too, since deriveBmpGeometryFromHeaderBytes() re-derives channels
    // from biBitCount (including to 4, for a user-edited biBitCount == 32 --
    // hasAlphaChannel()'s own format check below is what keeps *that* case
    // from being mistaken for real PNG alpha).
    bool hasChannelPlanes() const { return channels == 3 || hasAlphaChannel(); }

    // True only for a PNG loaded with a real alpha channel — every other
    // format/case (BMP, PNM, alpha-less PNG) is always false. Checking
    // format, not just channels == 4, matters: a BMP header-edited to
    // biBitCount == 32 (validateBmpHeaderFields() only warns on this, never
    // blocks it) also re-derives channels to 4, but that's a nonsensical
    // reinterpretation of a still-3-byte-per-pixel buffer, not real alpha —
    // this is the single source of truth the UI gates the 4th (alpha)
    // waveform lane on, so it must stay false for that case.
    bool hasAlphaChannel() const { return format == Format::png && channels == 4; }

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
    // top-down, unpadded row-major order. A bottom-up BMP's byte 0 is the
    // image's *bottom* row, so this keeps the (non-split) waveform's sample
    // index in the same top-down orientation the split-channel waveform uses
    // via getChannelPlane() — both must agree on what a given sample index
    // means, or the selection highlight and the waveform disagree on which
    // end of the image is which. Lazily cached (visualOrderDirty), invalidated
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

    // Renders the current pixelBytes (post- or pre-processing) as a plain juce::Image
    // for preview. The selection highlight is not baked into the pixels — it's drawn
    // as a cheap line overlay by the viewing component itself; see computeHighlightOverlay()
    // below. Backed by a lazy cache (cachedPlainImage/plainImageDirty, same idiom as
    // channelPlanes/visualOrderedPixelBytes below) — repeated calls with pixelBytes
    // unchanged since the last call are free.
    juce::Image toJuceImage() const;

    // Same rendering as toJuceImage(), but reads bytesToRender instead of this->pixelBytes —
    // format metadata (width/height/rowStride/channels/bottomUp/format) still comes from this.
    // Lets a caller render an uncommitted candidate buffer (e.g. a live plugin preview) for
    // display without ever mutating pixelBytes itself.
    juce::Image toJuceImageFromBytes(const juce::MemoryBlock& bytesToRender) const;

    // Like toJuceImageFromBytes(), but only rows [firstRow, lastRow] (inclusive,
    // in the same visual/screen row space as HighlightOverlay's topRow/bottomRow)
    // are actually repainted from bytesToRender — every other row is a cheap
    // duplicated-buffer copy of the cached plain render (getCachedPlainImage()),
    // not a per-pixel re-render. bytesToRender must be byte-identical to pixelBytes
    // everywhere OUTSIDE that row range — true for a selection-scoped live preview,
    // see LivePreviewWorker's Apply-scoping (processRequest(), in LivePreviewWorker.cpp).
    // For a live-preview session with no active selection (whole-buffer apply), the
    // caller should use toJuceImageFromBytes() instead — there's no "unchanged
    // outside a sub-range" guarantee to exploit in that case.
    juce::Image toJuceImageFromBytesScoped(const juce::MemoryBlock& bytesToRender, int firstRow, int lastRow) const;

    // Geometry for drawing the selection highlight as a two-line overlay (top edge,
    // bottom edge of the selection) rather than baking coloured pixels into the
    // rendered image — the actual image content never needs to change just because
    // the selection moved. Pure O(1) arithmetic: topRow/bottomRow are computed
    // directly (start/rowBytes, (end-1)/rowBytes — literally which row a given
    // percentage through the buffer falls on). The column span always spans the
    // full image width, even for a partial boundary row where only part of that
    // row is actually selected — a deliberate simplification, the lines read as
    // "vertical extent of the selection" rather than precisely which columns of
    // the boundary row are selected.
    struct HighlightOverlay
    {
        int topRow = 0, topStartColumn = 0, topEndColumn = 0;
        int bottomRow = 0, bottomStartColumn = 0, bottomEndColumn = 0;
    };

    // highlightByteRange is in *visual top-down, unpadded* byte order (see
    // getVisualOrderedPixelBytes()), matching what the interleaved waveform's
    // sample index means. Returns nullopt for an empty range (no highlight).
    std::optional<HighlightOverlay> computeHighlightOverlay(juce::Range<int> highlightByteRange) const;

    // Same, but highlightPlaneSampleRange is in getChannelPlane()'s coordinate
    // space (a plane-sample index, one byte per pixel) instead of a byte range
    // in the interleaved buffer — for a channel-scoped waveform selection.
    std::optional<HighlightOverlay> computeChannelHighlightOverlay(juce::Range<int> highlightPlaneSampleRange) const;

    // Exact inverses of the two functions above -- lets a caller that only has
    // an image-row range (e.g. the image preview's own draggable highlight
    // rectangle, dragged directly in row space) convert back into whichever
    // sample-range representation the corresponding waveform selection uses.
    // rowRange follows the same half-open juce::Range convention as
    // getSelectionSampleRange() -- i.e. [firstRow, lastRow + 1).
    juce::Range<int> rowRangeToHighlightByteRange(juce::Range<int> rowRange) const;
    juce::Range<int> rowRangeToChannelHighlightSampleRange(juce::Range<int> rowRange) const;

    enum class Format { bmp, pnmBinary, pnmGray, png, jpeg };

    // What format this image was actually loaded from (BMP vs PNM) — used
    // internally to interpret pixelBytes' layout (row order, channel order).
    Format getFormat() const { return format; }

    // Per-image, session-level choice of how pixelBytes' bytes map onto float
    // samples for plugin processing/waveform display. Defaults to bipolar;
    // see SampleFormat.h and PROJECT.md for why unipolar exists. Not part of
    // undo/redo history — a view setting, not an edit.
    SampleFormat::Mode getSampleMode() const { return sampleMode; }
    void setSampleMode(SampleFormat::Mode newMode) { sampleMode = newMode; }

    int getWidth() const { return width; }
    int getHeight() const { return height; }

    juce::MemoryBlock headerBytes;
    juce::MemoryBlock pixelBytes;

private:
    Format format = Format::bmp;
    SampleFormat::Mode sampleMode = SampleFormat::Mode::bipolar;
    int width = 0;
    int height = 0;
    int rowStride = 0;   // bytes per row, including any padding (BMP only; equals width*channels for PNM)
    int channels = 0;    // 3 for BMP/P6, 1 for P5, 3 or 4 for PNG, at load time; user-editable thereafter for BMP
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

    // Decodes via juce::ImageFileFormat (JUCE's own PNG codec, already used
    // for export), then re-packs into a fresh interleaved RGB or RGBA buffer,
    // since PNG's own on-disk layout (chunked, DEFLATE-compressed) has no simple
    // "protected header region" to preserve. JUCE's Image is always 8 bits
    // per channel, so there's no 16-bit-PNG precision loss to handle here —
    // the decoder itself normalises to that. Accepted tradeoff: an RGBA
    // source's colour channels (not alpha itself) are read back out of
    // juce::Image::ARGB's *premultiplied* 8-bit storage, which is lossy by
    // ±1-2/255 at low alpha (round(c·a/255) then round(stored·255/a)) — not
    // worth a hand-rolled non-premultiplied PNG scanline decoder for this.
    static std::unique_ptr<RawImage> loadPng(const juce::File&, juce::String& errorMessage);

    // Same decode-and-repack shape as loadPng(), minus the alpha-detection
    // step — JPEG has no alpha channel at all, so this is always 3-channel
    // RGB. Shares packImageToInterleavedBytes() with loadPng().
    static std::unique_ptr<RawImage> loadJpeg(const juce::File&, juce::String& errorMessage);

    // Shared by loadPng()/loadJpeg(): walks a decoded juce::Image pixel-by-
    // pixel via BitmapData::getPixelColour() and packs it into a fresh
    // width*height*channels interleaved RGB(A) buffer, top-down row-major.
    // channels must be 3 or 4; a 4th (alpha) byte is only written when 4.
    static juce::MemoryBlock packImageToInterleavedBytes(const juce::Image& image, int channels);

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
    mutable juce::MemoryBlock channelPlanes[4];
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

    // Plain-render cache backing toJuceImage() — same lazy dirty-flag idiom as
    // channelPlanes/visualOrderedPixelBytes above, invalidated at exactly the
    // same pixelBytes-mutation sites. toJuceImageFromBytesScoped() copies this
    // (juce::Image::duplicateIfShared() — a real memcpy, NOT free, but far
    // cheaper than running the per-pixel colour-conversion loop over rows that
    // haven't changed) and only re-renders the rows that did.
    mutable juce::Image cachedPlainImage;
    mutable bool plainImageDirty = true;

    void ensurePlainImageUpToDate() const;

    // Shared per-pixel colour-extraction logic behind writePixelRows() below
    // (BMP's BGR vs PNM/PNG/JPEG's RGB(A) byte order, or grayscale) — bounds-checked,
    // defaults to black/opaque if byteOffset falls outside available. `a` is
    // only ever a real decoded value for a 4-channel (RGBA PNG) image; it's
    // set to 255 (opaque) for every other case.
    void readRawRgbAt(const uint8_t* px, size_t available, size_t byteOffset,
                       juce::uint8& r, juce::uint8& g, juce::uint8& b, juce::uint8& a) const;

    // Fast path behind toJuceImageFromBytes()/toJuceImageFromBytesScoped()'s
    // render loops: writes raw pixel bytes directly via the bitmap's actual
    // pixel struct, bypassing BitmapData::setPixelColour()'s per-pixel
    // juce::Colour construction, premultiply-alpha branch (this image data
    // is never premultiplied, whether or not it carries real alpha), and
    // per-pixel pixelFormat switch —
    // measured via a throwaway timing harness at ~16.5ns/pixel through
    // setPixelColour(), enough on its own to make a live-preview redraw feel
    // laggy. Mirrors the pattern JUCE's own internal fast paths use (see
    // BitmapDataDetail::PixelIterator/ImageConvolutionKernel in JUCE's
    // source): the caller picks PixelType (juce::PixelRGB or
    // juce::PixelARGB) ONCE from bitmap.pixelFormat before the loop starts —
    // a per-pixel format branch is exactly what prevents the compiler from
    // vectorizing this loop, per JUCE's own comment on why it does the same
    // hoisting internally. On macOS specifically, requesting Image::RGB at
    // construction is silently upgraded to Image::ARGB by CoreGraphicsImage —
    // so the caller must always branch on the bitmap's actual runtime
    // pixelFormat, never assume it matches what toJuceImage*() requested.
    template <typename PixelType>
    void writePixelRows(const juce::Image::BitmapData& bitmap, const uint8_t* px, size_t available,
                        int startY, int endY) const;
};
