#include "WaveformView.h"

void WaveformView::setBuffer(juce::AudioBuffer<float> newBuffer, bool resetView)
{
    waveformData = std::move(newBuffer);

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

void WaveformView::updateSampleRange(int startSample, const juce::AudioBuffer<float>& newSamples)
{
    const int length = newSamples.getNumSamples();
    if (length <= 0)
        return;

    waveformData.copyFrom(0, startSample, newSamples, 0, 0, length);

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

    if (onViewChanged != nullptr)
        onViewChanged();

    repaint();
}

void WaveformView::setViewStart(int newViewStartSample)
{
    const int numSamples = waveformData.getNumSamples();
    viewStartSample = juce::jlimit(0, juce::jmax(0, numSamples - viewLengthSamples), newViewStartSample);

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
    g.fillAll(juce::Colours::black);

    const int width = getWidth();
    const int height = getHeight();
    const float midY = (float) height * 0.5f;
    const int numSamples = waveformData.getNumSamples();

    if (numSamples > 0 && width > 0 && viewLengthSamples > 0)
    {
        const auto* samples = waveformData.getReadPointer(0);

        g.setColour(juce::Colours::limegreen);

        for (int x = 0; x < width; ++x)
        {
            const int startSample = viewStartSample + (int) ((juce::int64) x * viewLengthSamples / width);
            int endSample = viewStartSample + (int) ((juce::int64) (x + 1) * viewLengthSamples / width);
            endSample = juce::jmin(juce::jmax(endSample, startSample + 1), viewStartSample + viewLengthSamples);

            float minV = 0.0f, maxV = 0.0f;
            for (int s = startSample; s < endSample && s < numSamples; ++s)
            {
                minV = juce::jmin(minV, samples[s]);
                maxV = juce::jmax(maxV, samples[s]);
            }

            minV = juce::jlimit(-1.0f, 1.0f, minV * verticalZoom);
            maxV = juce::jlimit(-1.0f, 1.0f, maxV * verticalZoom);

            const float y1 = midY - maxV * midY;
            const float y2 = juce::jmax(midY - minV * midY, y1 + 1.0f);
            g.drawLine((float) x, y1, (float) x, y2);
        }
    }
    else
    {
        g.setColour(juce::Colours::grey);
        g.drawText("Load an image to see its waveform", getLocalBounds(), juce::Justification::centred);
    }

    if (hasSelection)
    {
        const float x1 = (float) sampleToX(juce::jmin(selectionStartSample, selectionEndSample));
        const float x2 = (float) sampleToX(juce::jmax(selectionStartSample, selectionEndSample));
        auto selectionRect = juce::Rectangle<float>(x1, 0.0f, x2 - x1, (float) height);

        g.setColour(juce::Colours::yellow.withAlpha(0.25f));
        g.fillRect(selectionRect);
        g.setColour(juce::Colours::yellow);
        g.drawRect(selectionRect, 1.0f);

        // Small grip marks at each edge so the resize handles are visually
        // discoverable, not just an invisible hit zone found by hovering.
        constexpr float gripWidth = 3.0f;
        g.setColour(juce::Colours::white);
        g.fillRect(juce::Rectangle<float>(x1 - gripWidth * 0.5f, 0.0f, gripWidth, (float) height));
        g.fillRect(juce::Rectangle<float>(x2 - gripWidth * 0.5f, 0.0f, gripWidth, (float) height));
    }
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
