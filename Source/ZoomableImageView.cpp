#include "ZoomableImageView.h"

void ZoomableImageView::setImage(juce::Image newImage, bool resetView)
{
    image = std::move(newImage);

    if (resetView)
        fitToView();

    repaint();
}

void ZoomableImageView::resized()
{
    if (! image.isValid())
        return;

    // Keep the view sane if the component gets resized before any zoom/pan happened.
    if (scale <= 0.0f)
        fitToView();
}

void ZoomableImageView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);

    if (! image.isValid())
    {
        g.setColour(juce::Colours::grey);
        g.drawText("Load an image to see it here", getLocalBounds(), juce::Justification::centred);
        return;
    }

    g.drawImageTransformed(image, juce::AffineTransform::scale(scale).translated(offset.x, offset.y), false);
}

void ZoomableImageView::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (! image.isValid())
        return;

    // JUCE reports two-finger trackpad scroll/pan gestures through wheel deltas
    // normalised to roughly [-1, 1] per tick; scale up to pixels for a natural pan speed.
    constexpr float panPixelsPerUnitDelta = 1000.0f;
    offset += juce::Point<float>(wheel.deltaX, wheel.deltaY) * panPixelsPerUnitDelta;
    repaint();
}

void ZoomableImageView::mouseMagnify(const juce::MouseEvent& e, float scaleFactor)
{
    if (! image.isValid())
        return;

    applyZoom(scaleFactor, e.position);
}

void ZoomableImageView::mouseDown(const juce::MouseEvent&)
{
}

void ZoomableImageView::mouseDrag(const juce::MouseEvent&)
{
}

void ZoomableImageView::mouseDoubleClick(const juce::MouseEvent&)
{
    fitToView();
    repaint();
}

void ZoomableImageView::fitToView()
{
    if (! image.isValid() || getWidth() == 0 || getHeight() == 0)
    {
        scale = 1.0f;
        offset = {};
        return;
    }

    const float fitScale = juce::jmin((float) getWidth() / (float) image.getWidth(),
                                       (float) getHeight() / (float) image.getHeight());

    scale = fitScale;
    offset = { ((float) getWidth() - (float) image.getWidth() * scale) * 0.5f,
               ((float) getHeight() - (float) image.getHeight() * scale) * 0.5f };
}

void ZoomableImageView::applyZoom(float factor, juce::Point<float> anchorScreenPos)
{
    const float newScale = juce::jlimit(0.05f, 40.0f, scale * factor);
    const auto imagePointUnderCursor = (anchorScreenPos - offset) / scale;

    scale = newScale;
    offset = anchorScreenPos - imagePointUnderCursor * scale;

    repaint();
}
