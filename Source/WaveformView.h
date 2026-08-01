#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <optional>
#include <vector>
#include "SampleFormat.h"
#include "WaveformPeaks.h"

// Renders a mono buffer as a min/max waveform and lets the user drag out a
// time-range selection. The selection is expressed in sample indices, which
// map 1:1 onto byte offsets in the current fixed 8-bit-PCM sample format's
// buffer — for the whole-buffer (non-split) waveform, that buffer is
// RawImage::getVisualOrderedPixelBytes() (visual top-down, unpadded order),
// NOT necessarily pixelBytes' own raw file-storage order, which can be
// bottom-up/padded for BMP. See RawImage.h for why.
//
// Supports a horizontal zoom/scroll window (viewStartSample/viewLengthSamples)
// so fine structure can be inspected and selected precisely, since vertical
// zoom alone can't reveal detail in regions that are already near full scale.
class WaveformView : public juce::Component
{
public:
    // resetView: true when loading a genuinely new image (resets zoom/scroll/selection);
    // false when refreshing after an in-place edit like Apply (same sample count, so the
    // current zoom/scroll/selection stay put — the user is likely inspecting that exact spot).
    //
    // precomputedPeaks: bucket peaks already computed off-thread for the new
    // buffer (see WaveformPeaks::Partial and LivePreviewWorker::Result) — pass
    // nullopt to have the full peak cache rebuilt here instead (a one-off
    // O(numSamples) SIMD pass; fine for cold paths like image load, which
    // already pay a scalar bytesToBuffer over the same data, but NOT for the
    // recurring live-preview delivery path — see PROJECT.md's live-preview
    // performance note for the measured numbers behind that rule).
    void setBuffer(juce::AudioBuffer<float> newBuffer, bool resetView = true,
                   std::optional<WaveformPeaks::Partial> precomputedPeaks = std::nullopt);

    // Overwrites just a sub-range of the existing buffer's samples in place —
    // for when only a scoped sub-range of the underlying data actually changed
    // (e.g. a selection-scoped live preview), so the caller doesn't have to pay
    // for reconverting/reconstructing the whole buffer just to refresh the part
    // that changed. Sample count is unchanged, so unlike setBuffer() there's
    // nothing to re-clamp view/selection against; still fires onViewChanged and
    // repaints, matching setBuffer(..., resetView=false)'s effect on those.
    // precomputedPeaks: same contract as setBuffer()'s, scoped to the range.
    void updateSampleRange(int startSample, const juce::AudioBuffer<float>& newSamples,
                           std::optional<WaveformPeaks::Partial> precomputedPeaks = std::nullopt);

    void clearSelection();

    // Restores a selection programmatically (e.g. undo/redo) without treating it as a
    // new user gesture — does NOT fire onBeforeSelectionChange. An empty range means
    // "no selection", matching the convention documented on getSelectionSampleRange().
    void setSelectionSampleRange(juce::Range<int> newSelection);

    // Which byte<->float mapping the buffer currently passed to setBuffer()/
    // updateSampleRange() was converted with — purely a display concern here
    // (paint() draws bipolar samples centred, unipolar samples bottom-anchored
    // using the full lane height), the actual conversion happens in SampleFormat.
    void setSampleMode(SampleFormat::Mode newMode) { sampleMode = newMode; invalidateCachedTrace(); repaint(); }

    // zoom == 1 shows the whole buffer; higher values narrow the visible window.
    void setHorizontalZoom(float newZoom);
    void setViewStart(int newViewStartSample);

    int getNumSamples() const { return waveformData.getNumSamples(); }
    int getViewStartSample() const { return viewStartSample; }
    int getViewLengthSamples() const { return viewLengthSamples; }

    // Empty range means "no selection" — callers should treat that as "whole buffer".
    juce::Range<int> getSelectionSampleRange() const;

    // Fired whenever the visible window changes (buffer load, zoom, or scroll),
    // so a host can keep an external scrollbar in sync.
    std::function<void()> onViewChanged;

    // Fired whenever the selection changes (drag in progress, or cleared), so a
    // host can e.g. highlight the corresponding pixels in an image preview.
    std::function<void()> onSelectionChanged;

    // Fired at the very start of a new selection gesture (mouseDown, or clearSelection()),
    // before the old selection state is mutated — so a host can snapshot the "before"
    // state for undo. Not fired from mouseDrag (that would push one undo entry per drag
    // frame) or from setSelectionSampleRange() (a programmatic restore, not a user gesture).
    std::function<void()> onBeforeSelectionChange;

    void paint(juce::Graphics& g) override;

    // The cached trace's background fill depends on isEnabled() (see
    // ensureCachedTraceUpToDate()) -- unlike a size change, JUCE doesn't
    // repaint automatically when only the enabled state flips, so the cache
    // must be explicitly invalidated here.
    void enablementChanged() override { invalidateCachedTrace(); repaint(); }

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;

    // Trackpad gestures for horizontal zoom/pan: two-finger drag pans the view
    // (delivered as wheel deltas, same convention ZoomableImageView::
    // mouseWheelMove relies on), pinch zooms anchored on the sample under the
    // cursor so that sample stays put on screen.
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void mouseMagnify(const juce::MouseEvent& e, float scaleFactor) override;

private:
    int xToSample(int x) const;
    int sampleToX(int sample) const;

    // Pre-rendered, viewport-sized trace cache. paint()'s per-column min/max
    // scan (viewLengthSamples samples total, worst case the whole buffer)
    // would otherwise rerun on every repaint -- including the ones
    // mouseDrag() fires on every raw mouse-move event during a plain
    // selection drag, since that goes through no debounce. The trace only
    // actually depends on waveformData/viewStartSample/viewLengthSamples/
    // sampleMode, never the selection, so it's cached here and only
    // regenerated when one of those actually changes -- see
    // invalidateCachedTrace() call sites.
    void ensureCachedTraceUpToDate();
    void invalidateCachedTrace() { cachedTraceValid = false; }
    juce::Image cachedTrace;
    bool cachedTraceValid = false;

    juce::AudioBuffer<float> waveformData;

    // Exact per-bucket min/max over waveformData (see WaveformPeaks.h) — what
    // lets ensureCachedTraceUpToDate()'s per-column scan aggregate buckets
    // instead of rescanning O(viewLengthSamples) raw samples on every rebuild.
    // Depends ONLY on waveformData: kept in sync by setBuffer()/
    // updateSampleRange() and deliberately NOT touched by the display-only
    // setters (setSampleMode/setHorizontalZoom/setViewStart), which is
    // precisely what makes zoom/pan/mode trace rebuilds cheap too.
    std::vector<float> peakMins, peakMaxs;

    SampleFormat::Mode sampleMode = SampleFormat::Mode::bipolar;

    int viewStartSample = 0;
    int viewLengthSamples = 0;

    int selectionStartSample = 0;
    int selectionEndSample = 0;
    bool hasSelection = false;

    // Gesture classification for click-drag interaction with an existing
    // selection: resizing an edge, moving the whole selection, or creating a
    // brand new one. Decided once in mouseDown and applied in mouseDrag.
    enum class DragMode { none, creatingSelection, resizingLeft, resizingRight, movingSelection };
    DragMode dragMode = DragMode::none;
    int dragAnchorSample = 0;       // resize: the fixed (opposite) edge's sample
    int dragMoveOffsetSamples = 0;  // move: (sample under cursor at grab) - (selection's left edge at grab)
    int dragMoveLengthSamples = 0;  // move: selection length at grab, held constant while moving
    static constexpr int handleGrabPixels = 6; // hit-test tolerance around each handle, in pixels
};
