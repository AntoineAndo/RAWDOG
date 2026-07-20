#include "WaveformView.h"
#include "PixelBenderLookAndFeel.h"

void WaveformView::setBuffer(juce::AudioBuffer<float> newBuffer, bool resetView,
                             std::optional<WaveformPeaks::Partial> precomputedPeaks)
{
    waveformData = std::move(newBuffer);
    invalidateCachedTrace();

    {
        const int numSamples = waveformData.getNumSamples();
        const float* samples = numSamples > 0 ? waveformData.getReadPointer(0) : nullptr;

        if (precomputedPeaks.has_value())
        {
            // The supplied run covers every full bucket; applyPartial()
            // recomputes whatever it doesn't cover (at most the buffer's
            // partial tail bucket -- see computePartial()'s doc comment).
            peakMins.assign((size_t) WaveformPeaks::numBucketsFor(numSamples), 0.0f);
            peakMaxs.assign((size_t) WaveformPeaks::numBucketsFor(numSamples), 0.0f);
            WaveformPeaks::applyPartial(peakMins, peakMaxs, *precomputedPeaks, samples, numSamples, 0, numSamples);
        }
        else
        {
            WaveformPeaks::buildPeaks(samples, numSamples, peakMins, peakMaxs);
        }
    }

    bool selectionChanged = false;

    if (resetView)
    {
        viewStartSample = 0;
        viewLengthSamples = waveformData.getNumSamples();
        hasSelection = false;
        selectionChanged = true;
    }
    else
    {
        viewLengthSamples = juce::jlimit(0, waveformData.getNumSamples(), viewLengthSamples);
        viewStartSample = juce::jlimit(0, juce::jmax(0, waveformData.getNumSamples() - viewLengthSamples), viewStartSample);

        // The selection range is independent of the view range above, so it
        // needs its own re-clamp against the new buffer's sample count — a
        // stale selection could otherwise point past the end of a shorter
        // buffer (e.g. after Apply changes the sample count).
        if (hasSelection)
        {
            const int numSamples = waveformData.getNumSamples();
            selectionStartSample = juce::jlimit(0, numSamples, selectionStartSample);
            selectionEndSample = juce::jlimit(0, numSamples, selectionEndSample);

            if (juce::jmin(selectionStartSample, selectionEndSample) >= juce::jmax(selectionStartSample, selectionEndSample))
            {
                hasSelection = false;
                selectionChanged = true;
            }
        }
    }

    if (onViewChanged != nullptr)
        onViewChanged();

    if (selectionChanged && onSelectionChanged != nullptr)
        onSelectionChanged();

    repaint();
}

void WaveformView::updateSampleRange(int startSample, const juce::AudioBuffer<float>& newSamples,
                                     std::optional<WaveformPeaks::Partial> precomputedPeaks)
{
    const int length = newSamples.getNumSamples();
    if (length <= 0)
        return;

    waveformData.copyFrom(0, startSample, newSamples, 0, 0, length);

    // Defensive: a stale-sized peak cache (shouldn't happen -- setBuffer()
    // always resizes it) would make applyPartial() write out of bounds, so
    // fall back to a full rebuild rather than trust it. Otherwise splice the
    // precomputed run (or recompute every touched bucket from the just-spliced
    // data, when none was supplied -- an empty Partial covers nothing, so
    // applyPartial() degrades to exactly that).
    if ((int) peakMins.size() != WaveformPeaks::numBucketsFor(waveformData.getNumSamples()))
        WaveformPeaks::buildPeaks(waveformData.getReadPointer(0), waveformData.getNumSamples(), peakMins, peakMaxs);
    else
        WaveformPeaks::applyPartial(peakMins, peakMaxs,
                                    precomputedPeaks.has_value() ? *precomputedPeaks : WaveformPeaks::Partial{},
                                    waveformData.getReadPointer(0), waveformData.getNumSamples(),
                                    startSample, startSample + length);

    invalidateCachedTrace();

    if (onViewChanged != nullptr)
        onViewChanged();

    repaint();
}

void WaveformView::clearSelection()
{
    if (onBeforeSelectionChange != nullptr)
        onBeforeSelectionChange();

    hasSelection = false;

    if (onSelectionChanged != nullptr)
        onSelectionChanged();

    repaint();
}

void WaveformView::setSelectionSampleRange(juce::Range<int> newSelection)
{
    if (newSelection.isEmpty())
    {
        hasSelection = false;
    }
    else
    {
        hasSelection = true;
        selectionStartSample = newSelection.getStart();
        selectionEndSample = newSelection.getEnd();
    }

    if (onSelectionChanged != nullptr)
        onSelectionChanged();

    repaint();
}

void WaveformView::setHorizontalZoom(float newZoom)
{
    const int numSamples = waveformData.getNumSamples();
    if (numSamples == 0)
        return;

    const float clampedZoom = juce::jmax(1.0f, newZoom);
    const int minVisible = juce::jmin(numSamples, 16);
    viewLengthSamples = juce::jlimit(minVisible, numSamples, (int) (numSamples / clampedZoom));
    viewStartSample = juce::jlimit(0, numSamples - viewLengthSamples, viewStartSample);
    invalidateCachedTrace();

    if (onViewChanged != nullptr)
        onViewChanged();

    repaint();
}

void WaveformView::setViewStart(int newViewStartSample)
{
    const int numSamples = waveformData.getNumSamples();
    viewStartSample = juce::jlimit(0, juce::jmax(0, numSamples - viewLengthSamples), newViewStartSample);
    invalidateCachedTrace();

    if (onViewChanged != nullptr)
        onViewChanged();

    repaint();
}

int WaveformView::xToSample(int x) const
{
    const int width = juce::jmax(1, getWidth());
    const double frac = juce::jlimit(0.0, 1.0, (double) x / (double) width);
    return viewStartSample + (int) (frac * viewLengthSamples);
}

int WaveformView::sampleToX(int sample) const
{
    if (viewLengthSamples <= 0)
        return 0;

    const double frac = (double) (sample - viewStartSample) / (double) viewLengthSamples;
    return (int) juce::jlimit(0.0, (double) getWidth(), frac * getWidth());
}

juce::Range<int> WaveformView::getSelectionSampleRange() const
{
    if (! hasSelection)
        return {};

    return { juce::jmin(selectionStartSample, selectionEndSample), juce::jmax(selectionStartSample, selectionEndSample) };
}

void WaveformView::paint(juce::Graphics& g)
{
    ensureCachedTraceUpToDate();
    g.drawImageAt(cachedTrace, 0, 0);

    if (hasSelection)
    {
        const auto& palette = PixelBenderLookAndFeel::Palette::get();
        const float height = (float) getHeight();
        const float x1 = (float) sampleToX(juce::jmin(selectionStartSample, selectionEndSample));
        const float x2 = (float) sampleToX(juce::jmax(selectionStartSample, selectionEndSample));
        auto selectionRect = juce::Rectangle<float>(x1, 0.0f, x2 - x1, height);

        g.setColour(palette.gold.withAlpha(0.22f));
        g.fillRect(selectionRect);
        g.setColour(palette.gold);
        g.drawRect(selectionRect, 1.0f);

        // Small grip marks at each edge so the resize handles are visually
        // discoverable, not just an invisible hit zone found by hovering.
        constexpr float gripWidth = 3.0f;
        g.setColour(juce::Colours::white);
        g.fillRect(juce::Rectangle<float>(x1 - gripWidth * 0.5f, 0.0f, gripWidth, height));
        g.fillRect(juce::Rectangle<float>(x2 - gripWidth * 0.5f, 0.0f, gripWidth, height));
    }
}

void WaveformView::ensureCachedTraceUpToDate()
{
    const int w = juce::jmax(1, getWidth());
    const int h = juce::jmax(1, getHeight());

    if (cachedTraceValid && cachedTrace.getWidth() == w && cachedTrace.getHeight() == h)
        return;

    // Only reallocate when the size actually changed -- reuse the same
    // juce::Image object across same-size regenerations, same idiom as
    // ZoomableImageView::ensureCachedRenderUpToDate().
    if (cachedTrace.getWidth() != w || cachedTrace.getHeight() != h)
        cachedTrace = juce::Image(juce::Image::RGB, w, h, false);

    const auto& palette = PixelBenderLookAndFeel::Palette::get();

    juce::Graphics cg(cachedTrace);
    cg.fillAll(palette.background);

    const int width = w;
    const int height = h;
    const float midY = (float) height * 0.5f;
    const int numSamples = waveformData.getNumSamples();

    // bipolar: centred on midY, samples range [-1,1]. unipolar: anchored at
    // the bottom, samples range [0,1] — using the full lane height instead of
    // only the top half, since byte 0 (silence) is a genuine floor, not one
    // of two symmetric extremes.
    const bool bipolar = sampleMode == SampleFormat::Mode::bipolar;
    const float baseline = bipolar ? midY : (float) height;
    const float scale = bipolar ? midY : (float) height;
    const float loClamp = bipolar ? -1.0f : 0.0f;
    const float hiClamp = 1.0f;

    if (numSamples > 0 && width > 0 && viewLengthSamples > 0)
    {
        const auto* samples = waveformData.getReadPointer(0);

        cg.setColour(palette.waveform);

        for (int x = 0; x < width; ++x)
        {
            const int startSample = viewStartSample + (int) ((juce::int64) x * viewLengthSamples / width);
            int endSample = viewStartSample + (int) ((juce::int64) (x + 1) * viewLengthSamples / width);
            endSample = juce::jmin(juce::jmax(endSample, startSample + 1), viewStartSample + viewLengthSamples);

            // Exact min/max of the column's clamped sample range, via the
            // bucket peak cache instead of a raw O(samples-per-column) rescan
            // (which, totalled across columns, was an O(viewLengthSamples)
            // message-thread pass on every rebuild -- measured at ~406ms on a
            // 77.9M-sample buffer; see PROJECT.md's live-preview performance
            // note). columnMinMax() preserves this loop's previous semantics
            // exactly, including the true one-sided min/max for columns whose
            // samples never cross zero (e.g. a colour channel's plane wherever
            // that channel is absent) and {0,0} for a column beyond the data.
            const auto mm = WaveformPeaks::columnMinMax(samples, numSamples, startSample, endSample, peakMins, peakMaxs);

            const float minV = juce::jlimit(loClamp, hiClamp, mm.minV);
            const float maxV = juce::jlimit(loClamp, hiClamp, mm.maxV);

            const float y1 = baseline - maxV * scale;
            const float y2 = juce::jmax(baseline - minV * scale, y1 + 1.0f);
            cg.drawLine((float) x, y1, (float) x, y2);
        }
    }
    else
    {
        cg.setColour(palette.textSecondary);
        cg.drawText("Load an image to see its waveform", cachedTrace.getBounds(), juce::Justification::centred);
    }

    cachedTraceValid = true;
}

void WaveformView::mouseDown(const juce::MouseEvent& e)
{
    if (hasSelection)
    {
        const int leftEdgeSample = juce::jmin(selectionStartSample, selectionEndSample);
        const int rightEdgeSample = juce::jmax(selectionStartSample, selectionEndSample);
        const int leftX = sampleToX(leftEdgeSample);
        const int rightX = sampleToX(rightEdgeSample);

        if (std::abs(e.x - leftX) <= handleGrabPixels)
        {
            if (onBeforeSelectionChange != nullptr)
                onBeforeSelectionChange();

            dragMode = DragMode::resizingLeft;
            dragAnchorSample = rightEdgeSample;
            repaint();
            return;
        }

        if (std::abs(e.x - rightX) <= handleGrabPixels)
        {
            if (onBeforeSelectionChange != nullptr)
                onBeforeSelectionChange();

            dragMode = DragMode::resizingRight;
            dragAnchorSample = leftEdgeSample;
            repaint();
            return;
        }

        if (e.x > leftX && e.x < rightX)
        {
            if (onBeforeSelectionChange != nullptr)
                onBeforeSelectionChange();

            dragMode = DragMode::movingSelection;
            dragMoveLengthSamples = rightEdgeSample - leftEdgeSample;
            dragMoveOffsetSamples = xToSample(e.x) - leftEdgeSample;
            repaint();
            return;
        }
    }

    if (onBeforeSelectionChange != nullptr)
        onBeforeSelectionChange();

    dragMode = DragMode::creatingSelection;
    selectionStartSample = selectionEndSample = xToSample(e.x);
    hasSelection = true;

    if (onSelectionChanged != nullptr)
        onSelectionChanged();

    repaint();
}

void WaveformView::mouseDrag(const juce::MouseEvent& e)
{
    const int numSamples = waveformData.getNumSamples();

    switch (dragMode)
    {
        case DragMode::resizingLeft:
        {
            // jmax guards against an inverted jlimit range when the anchor sits at (or
            // near) sample 0 — e.g. resizing a degenerate zero-length selection created
            // by a plain click with no drag — which would otherwise push selectionStartSample
            // negative and corrupt any later byte-range copy off the raw buffer.
            const int upperBound = juce::jmax(0, dragAnchorSample - 1);
            const int newLeft = juce::jlimit(0, upperBound, xToSample(e.x));
            selectionStartSample = newLeft;
            selectionEndSample = dragAnchorSample;
            break;
        }

        case DragMode::resizingRight:
        {
            // Symmetric guard: keep the lower bound from exceeding numSamples when the
            // anchor sits at (or near) the buffer's end.
            const int lowerBound = juce::jmin(numSamples, dragAnchorSample + 1);
            const int newRight = juce::jlimit(lowerBound, numSamples, xToSample(e.x));
            selectionStartSample = dragAnchorSample;
            selectionEndSample = newRight;
            break;
        }

        case DragMode::movingSelection:
        {
            const int newLeft = juce::jlimit(0, juce::jmax(0, numSamples - dragMoveLengthSamples),
                                              xToSample(e.x) - dragMoveOffsetSamples);
            selectionStartSample = newLeft;
            selectionEndSample = newLeft + dragMoveLengthSamples;
            break;
        }

        case DragMode::creatingSelection:
        case DragMode::none:
        default:
            selectionEndSample = xToSample(juce::jlimit(0, getWidth(), e.x));
            break;
    }

    if (onSelectionChanged != nullptr)
        onSelectionChanged();

    repaint();
}

void WaveformView::mouseUp(const juce::MouseEvent&)
{
    dragMode = DragMode::none;
}

void WaveformView::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (waveformData.getNumSamples() == 0 || viewLengthSamples <= 0)
        return;

    // wheel.deltaX is normalised to roughly [-1, 1] per gesture tick; scale by
    // the current view width (not a fixed pixel constant) so pan speed feels
    // consistent whether zoomed in or fully zoomed out.
    constexpr float panFractionPerUnitDelta = 1.0f;
    const int panSamples = (int) (wheel.deltaX * panFractionPerUnitDelta * (float) viewLengthSamples);
    setViewStart(viewStartSample - panSamples);
}

void WaveformView::mouseMagnify(const juce::MouseEvent& e, float scaleFactor)
{
    const int numSamples = waveformData.getNumSamples();
    if (numSamples == 0 || viewLengthSamples <= 0)
        return;

    const int anchorSample = xToSample(e.x);
    const double anchorFrac = (double) (anchorSample - viewStartSample) / (double) viewLengthSamples;

    const int minVisible = juce::jmin(numSamples, 16);
    const int newLength = juce::jlimit(minVisible, numSamples,
                                        (int) (viewLengthSamples / juce::jmax(0.01f, scaleFactor)));

    viewLengthSamples = newLength;
    viewStartSample = juce::jlimit(0, juce::jmax(0, numSamples - viewLengthSamples),
                                    anchorSample - (int) (anchorFrac * viewLengthSamples));

    invalidateCachedTrace();

    if (onViewChanged != nullptr)
        onViewChanged();

    repaint();
}

void WaveformView::mouseMove(const juce::MouseEvent& e)
{
    if (! hasSelection)
    {
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }

    const int leftX = sampleToX(juce::jmin(selectionStartSample, selectionEndSample));
    const int rightX = sampleToX(juce::jmax(selectionStartSample, selectionEndSample));

    if (std::abs(e.x - leftX) <= handleGrabPixels || std::abs(e.x - rightX) <= handleGrabPixels)
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    else if (e.x > leftX && e.x < rightX)
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    else
        setMouseCursor(juce::MouseCursor::NormalCursor);
}
