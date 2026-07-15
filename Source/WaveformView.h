#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>

// Renders a mono buffer as a min/max waveform and lets the user drag out a
// time-range selection. The selection is expressed in sample indices, which
// map 1:1 onto byte offsets in the pixel buffer for the current fixed
// 8-bit-PCM sample format.
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
    void setBuffer(juce::AudioBuffer<float> newBuffer, bool resetView = true);
    void clearSelection();

    // Purely a display multiplier on amplitude — the underlying data is unaffected.
    // Values are clipped to the component's height once scaled, like a vertical zoom.
    void setVerticalZoom(float newZoom) { verticalZoom = newZoom; repaint(); }

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

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;

private:
    int xToSample(int x) const;
    int sampleToX(int sample) const;

    juce::AudioBuffer<float> waveformData;
    float verticalZoom = 1.0f;

    int viewStartSample = 0;
    int viewLengthSamples = 0;

    int selectionStartSample = 0;
    int selectionEndSample = 0;
    bool hasSelection = false;
};
