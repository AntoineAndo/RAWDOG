#include "WaveformView.h"

void WaveformView::setBuffer(juce::AudioBuffer<float> newBuffer, bool resetView)
{
    waveformData = std::move(newBuffer);

    if (resetView)
    {
        viewStartSample = 0;
        viewLengthSamples = waveformData.getNumSamples();
        hasSelection = false;
    }
    else
    {
        viewLengthSamples = juce::jlimit(0, waveformData.getNumSamples(), viewLengthSamples);
        viewStartSample = juce::jlimit(0, juce::jmax(0, waveformData.getNumSamples() - viewLengthSamples), viewStartSample);
    }

    if (onViewChanged != nullptr)
        onViewChanged();

    repaint();
}

void WaveformView::clearSelection()
{
    hasSelection = false;
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
    }
}

void WaveformView::mouseDown(const juce::MouseEvent& e)
{
    selectionStartSample = selectionEndSample = xToSample(e.x);
    hasSelection = true;
    repaint();
}

void WaveformView::mouseDrag(const juce::MouseEvent& e)
{
    selectionEndSample = xToSample(juce::jlimit(0, getWidth(), e.x));
    repaint();
}
