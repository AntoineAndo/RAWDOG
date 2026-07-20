#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "GrippedResizerBar.h"
#include "PixelBenderLookAndFeel.h"
#include "WaveformSectionPanel.h"

// Parents the image preview, a WaveformSectionPanel, and the status label.
// Carves the fixed 24px status strip off the bottom first (unchanged from
// today), then splits the remainder into preview/waveform via its own
// user-draggable divider.
class RightColumnPanel : public juce::Component
{
public:
    RightColumnPanel(juce::Component& imagePreviewIn, juce::Label& statusLabelIn,
                     juce::Component& waveformViewIn,
                     juce::Component& horizontalScrollBarIn,
                     juce::Component& redWaveformIn, juce::Component& greenWaveformIn,
                     juce::Component& blueWaveformIn, juce::Component& alphaWaveformIn,
                     juce::Button& splitToggleIn,
                     juce::Label& sampleModeLabelIn, juce::ComboBox& sampleModeComboIn,
                     juce::Component& busySpinnerIn)
        : imagePreviewRef(imagePreviewIn), statusLabelRef(statusLabelIn),
          sampleModeLabelRef(sampleModeLabelIn), sampleModeComboRef(sampleModeComboIn),
          busySpinnerRef(busySpinnerIn),
          waveformSection(waveformViewIn, horizontalScrollBarIn,
                          redWaveformIn, greenWaveformIn, blueWaveformIn, alphaWaveformIn, splitToggleIn)
    {
        addAndMakeVisible(imagePreviewRef);
        addAndMakeVisible(waveformSection);
        addAndMakeVisible(resizerBar);
        addAndMakeVisible(statusLabelRef);
        addAndMakeVisible(sampleModeLabelRef);
        addAndMakeVisible(sampleModeComboRef);

        // The spinner manages its own visibility (shown only while a
        // live-preview pass is computing) -- addChildComponent, not
        // addAndMakeVisible, so it starts hidden.
        addChildComponent(busySpinnerRef);

        layout.setItemLayout(0, 100, -1.0, -1.0); // preview: min 100px, fills remainder
        layout.setItemLayout(1, 8, 8, 8);          // resizer bar: fixed 8px

        // Waveform section: min 80px, preferred 140px, capped at a fixed 220px
        // (not a proportional -0.6 max) -- the toolbar/waveform-lane rows above
        // it are fixed-height, so dragging past a sane cap only inflated the
        // horizontal-scrollbar strip at the bottom instead of the waveform
        // itself, which read as a layout bug rather than "more room."
        layout.setItemLayout(2, 80, 220, 140);
    }

    // Forwarded down to WaveformSectionPanel — see its own doc comment.
    void updateSplitVisibility() { waveformSection.updateSplitVisibility(); }

    void paint(juce::Graphics& g) override
    {
        // Frames the image viewport so it reads as a bounded surface rather
        // than bleeding straight into the panel background.
        g.setColour(PixelBenderLookAndFeel::Palette::get().border);
        g.drawRect(imagePreviewRef.getBounds().expanded(1), 1);
    }

    void resized() override
    {
        auto area = getLocalBounds();

        auto statusArea = area.removeFromBottom(24);
        area.removeFromBottom(8);

        // Spinner square on the left of the status strip, text after it --
        // the text keeps a constant small indent so it doesn't reflow when
        // the spinner appears/disappears.
        busySpinnerRef.setBounds(statusArea.removeFromLeft(24).reduced(3));
        statusArea.removeFromLeft(2);
        statusLabelRef.setBounds(statusArea);

        // Fixed strip for the bipolar/unipolar controls, above the preview.
        // MainComponent shows/hides this pair based on whether the plugin
        // editor panel is open; when hidden, this strip is simply blank.
        auto sampleModeArea = area.removeFromTop(28);
        area.removeFromTop(4);
        sampleModeLabelRef.setBounds(sampleModeArea.removeFromLeft(90));
        sampleModeArea.removeFromLeft(8);
        sampleModeComboRef.setBounds(sampleModeArea.removeFromLeft(110));

        juce::Component* items[] = { &imagePreviewRef, &resizerBar, &waveformSection };
        layout.layOutComponents(items, 3, area.getX(), area.getY(),
                                 area.getWidth(), area.getHeight(),
                                 true /*stacked vertically*/, true /*resizeOtherDimension*/);
    }

private:
    juce::Component& imagePreviewRef;
    juce::Label& statusLabelRef;
    juce::Label& sampleModeLabelRef;
    juce::ComboBox& sampleModeComboRef;
    juce::Component& busySpinnerRef;
    WaveformSectionPanel waveformSection;
    juce::StretchableLayoutManager layout;
    GrippedResizerBar resizerBar { &layout, 1, false /*horizontal bar, dragged up/down*/ };
};
