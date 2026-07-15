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

void ZoomableImageView::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (! image.isValid())
        return;

    const float zoomFactor = std::pow(1.1f, wheel.deltaY * 10.0f);
    applyZoom(zoomFactor, e.position);
}

void ZoomableImageView::mouseDown(const juce::MouseEvent& e)
{
    dragStart = e.position;
    offsetAtDragStart = offset;
}

void ZoomableImageView::mouseDrag(const juce::MouseEvent& e)
{
    if (! image.isValid())
        return;

    offset = offsetAtDragStart + (e.position - dragStart);
    repaint();
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
