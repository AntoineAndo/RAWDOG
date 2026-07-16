#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "WaveformSectionPanel.h"

// Parents the image preview, a WaveformSectionPanel, and the status label.
// Carves the fixed 24px status strip off the bottom first (unchanged from
// today), then splits the remainder into preview/waveform via its own
// user-draggable divider.
class RightColumnPanel : public juce::Component
{
public:
    RightColumnPanel(juce::Component& imagePreviewIn, juce::Label& statusLabelIn,
                     juce::Component& waveformViewIn, juce::Slider& waveformZoomSliderIn,
                     juce::Label& waveformZoomLabelIn, juce::Slider& horizontalZoomSliderIn,
                     juce::Component& horizontalScrollBarIn,
                     juce::Component& redWaveformIn, juce::Component& greenWaveformIn,
                     juce::Component& blueWaveformIn, juce::Button& splitToggleIn,
                     juce::Label& sampleModeLabelIn, juce::ComboBox& sampleModeComboIn)
        : imagePreviewRef(imagePreviewIn), statusLabelRef(statusLabelIn),
          sampleModeLabelRef(sampleModeLabelIn), sampleModeComboRef(sampleModeComboIn),
          waveformSection(waveformViewIn, waveformZoomSliderIn, waveformZoomLabelIn,
                          horizontalZoomSliderIn, horizontalScrollBarIn,
                          redWaveformIn, greenWaveformIn, blueWaveformIn, splitToggleIn)
    {
        addAndMakeVisible(imagePreviewRef);
        addAndMakeVisible(waveformSection);
        addAndMakeVisible(resizerBar);
        addAndMakeVisible(statusLabelRef);
        addAndMakeVisible(sampleModeLabelRef);
        addAndMakeVisible(sampleModeComboRef);

        layout.setItemLayout(0, 100, -1.0, -1.0); // preview: min 100px, fills remainder
        layout.setItemLayout(1, 8, 8, 8);          // resizer bar: fixed 8px
        layout.setItemLayout(2, 80, -0.6, 140);    // waveform section: min 80px, max 60%, preferred 140px
    }

    // Forwarded down to WaveformSectionPanel — see its own doc comment.
    void updateSplitVisibility() { waveformSection.updateSplitVisibility(); }

    void resized() override
    {
        auto area = getLocalBounds();

        auto statusArea = area.removeFromBottom(24);
        area.removeFromBottom(8);
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
    WaveformSectionPanel waveformSection;
    juce::StretchableLayoutManager layout;
    juce::StretchableLayoutResizerBar resizerBar { &layout, 1, false /*horizontal bar, dragged up/down*/ };
};
