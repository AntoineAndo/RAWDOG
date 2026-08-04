#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "FileByteMixer.h"

// Lets the user pick an arbitrary file and mix its raw bytes into the loaded
// image's pixel data (XOR/wrapped-add/replace), scoped to the current
// waveform selection exactly like the plugin chain -- see
// MainComponentFileModifier.cpp. Mirrors HeaderEditorPanel's embedded-panel-
// with-Apply/Cancel shape: MainComponent drives a live preview by calling
// FileByteMixer::mixBytes() every time onSettingsChanged fires, and only
// commits to the real image on Apply.
class FileModifierPanel : public juce::Component
{
public:
    FileModifierPanel(std::function<void()> onChooseFileIn,
                       std::function<void()> onSettingsChangedIn,
                       std::function<void()> onApply,
                       std::function<void()> onCancel)
        : onChooseFile(std::move(onChooseFileIn)), onSettingsChanged(std::move(onSettingsChangedIn))
    {
        titleLabel.setText("File Modifier", juce::dontSendNotification);
        titleLabel.setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));
        addAndMakeVisible(titleLabel);

        addAndMakeVisible(chooseFileButton);
        chooseFileButton.onClick = [this] { if (onChooseFile) onChooseFile(); };

        fileInfoLabel.setText("No file chosen", juce::dontSendNotification);
        fileInfoLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
        fileInfoLabel.setFont(juce::Font(juce::FontOptions(12.0f)));
        addAndMakeVisible(fileInfoLabel);

        operationLabel.setText("Operation", juce::dontSendNotification);
        addAndMakeVisible(operationLabel);

        operationCombo.addItem("XOR", (int) FileByteMixer::Operation::xorOp + 1);
        operationCombo.addItem("Add (wrapped)", (int) FileByteMixer::Operation::addWrap + 1);
        operationCombo.addItem("Replace", (int) FileByteMixer::Operation::replace + 1);
        operationCombo.setSelectedId((int) FileByteMixer::Operation::xorOp + 1, juce::dontSendNotification);
        operationCombo.onChange = [this] { if (onSettingsChanged) onSettingsChanged(); };
        addAndMakeVisible(operationCombo);

        blendLabel.setText("Blend", juce::dontSendNotification);
        addAndMakeVisible(blendLabel);

        blendSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        blendSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
        blendSlider.setTextValueSuffix("%");
        blendSlider.setRange(0.0, 100.0, 1.0);
        blendSlider.setValue(100.0, juce::dontSendNotification);
        blendSlider.setTooltip("How strongly the operation's result replaces the original bytes -- "
                                "0% leaves the image untouched, 100% is the operation at full strength.");
        blendSlider.onValueChange = [this] { if (onSettingsChanged) onSettingsChanged(); };
        addAndMakeVisible(blendSlider);

        scaleLabel.setText("Scale", juce::dontSendNotification);
        addAndMakeVisible(scaleLabel);

        scaleSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        scaleSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
        scaleSlider.setTextValueSuffix("x");
        scaleSlider.setRange(0.1, 10.0, 0.01);
        scaleSlider.setSkewFactorFromMidPoint(1.0);
        scaleSlider.setValue(1.0, juce::dontSendNotification);
        scaleSlider.setTooltip("Resamples the file before tiling it in -- above 1x stretches its pattern out "
                                "(coarser), below 1x compresses it (denser/higher-frequency).");
        scaleSlider.onValueChange = [this] { if (onSettingsChanged) onSettingsChanged(); };
        addAndMakeVisible(scaleSlider);

        scopeLabel.setText("Applies to the current waveform selection, or the whole image if nothing is selected.",
                            juce::dontSendNotification);
        scopeLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
        scopeLabel.setFont(juce::Font(juce::FontOptions(12.0f)));
        scopeLabel.setJustificationType(juce::Justification::topLeft);
        addAndMakeVisible(scopeLabel);

        addAndMakeVisible(cancelButton);
        addAndMakeVisible(applyButton);
        applyButton.setEnabled(false); // no modifier file chosen yet
        cancelButton.onClick = std::move(onCancel);
        applyButton.onClick = std::move(onApply);
    }

    int getPreferredWidth() const { return 340; }

    void setModifierFileInfo(const juce::String& fileName, juce::int64 sizeInBytes)
    {
        fileInfoLabel.setText(fileName + "  (" + juce::File::descriptionOfSizeInBytes(sizeInBytes) + ")",
                               juce::dontSendNotification);
        fileInfoLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        applyButton.setEnabled(true);
    }

    FileByteMixer::Operation getSelectedOperation() const
    {
        return (FileByteMixer::Operation) (operationCombo.getSelectedId() - 1);
    }

    float getBlend() const { return (float) (blendSlider.getValue() / 100.0); }
    float getScale() const { return (float) scaleSlider.getValue(); }

    void resized() override
    {
        auto area = getLocalBounds().reduced(8);

        auto buttonStrip = area.removeFromBottom(40);
        applyButton.setBounds(buttonStrip.removeFromRight(buttonStrip.getWidth() / 2).reduced(4, 0));
        cancelButton.setBounds(buttonStrip.reduced(4, 0));

        titleLabel.setBounds(area.removeFromTop(24));
        area.removeFromTop(8);

        chooseFileButton.setBounds(area.removeFromTop(28));
        area.removeFromTop(4);
        fileInfoLabel.setBounds(area.removeFromTop(20));
        area.removeFromTop(12);

        operationLabel.setBounds(area.removeFromTop(20));
        operationCombo.setBounds(area.removeFromTop(24));
        area.removeFromTop(12);

        blendLabel.setBounds(area.removeFromTop(20));
        blendSlider.setBounds(area.removeFromTop(24));
        area.removeFromTop(12);

        scaleLabel.setBounds(area.removeFromTop(20));
        scaleSlider.setBounds(area.removeFromTop(24));
        area.removeFromTop(12);

        scopeLabel.setBounds(area.removeFromTop(48));
    }

private:
    std::function<void()> onChooseFile;
    std::function<void()> onSettingsChanged;

    juce::Label titleLabel;
    juce::TextButton chooseFileButton { "Choose File..." };
    juce::Label fileInfoLabel;
    juce::Label operationLabel;
    juce::ComboBox operationCombo;
    juce::Label blendLabel;
    juce::Slider blendSlider;
    juce::Label scaleLabel;
    juce::Slider scaleSlider;
    juce::Label scopeLabel;
    juce::TextButton cancelButton { "Cancel" }, applyButton { "Apply" };
};
