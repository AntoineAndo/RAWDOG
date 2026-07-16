#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "RawImage.h"

// Lets the user view all 16 documented BMP header fields and edit the 5 that
// drive this app's own decode/render logic (bfOffBits/biWidth/biHeight/
// biBitCount/biCompression). The other 11 fields are shown read-only, for
// reference -- nothing ever writes them back into headerBytes. Mirrors
// PluginEditorPanel's embedded-panel-with-Apply/Cancel shape: MainComponent
// drives live preview by calling RawImage::applyBmpHeaderFields() on a scratch
// copy every time onFieldsChanged fires, and only commits to the real image
// on Apply.
class HeaderEditorPanel : public juce::Component
{
public:
    HeaderEditorPanel(const RawImage::BmpHeaderFields& initial,
                       std::function<void(const RawImage::BmpEditableHeaderFields&)> onFieldsChangedIn,
                       std::function<void()> onApply,
                       std::function<void()> onCancel)
        : onFieldsChanged(std::move(onFieldsChangedIn))
    {
        content.setUp(initial, [this] { notifyChanged(); });

        viewport.setViewedComponent(&content, false);
        viewport.setScrollBarsShown(true, false); // vertical only -- content matches our own width
        addAndMakeVisible(viewport);

        warningLabel.setColour(juce::Label::textColourId, juce::Colours::orange);
        warningLabel.setJustificationType(juce::Justification::topLeft);
        warningLabel.setFont(juce::Font(juce::FontOptions(12.0f)));
        addAndMakeVisible(warningLabel);

        errorLabel.setColour(juce::Label::textColourId, juce::Colours::red);
        errorLabel.setJustificationType(juce::Justification::topLeft);
        errorLabel.setFont(juce::Font(juce::FontOptions(12.0f)));
        addAndMakeVisible(errorLabel);

        addAndMakeVisible(cancelButton);
        addAndMakeVisible(applyButton);
        cancelButton.onClick = std::move(onCancel);
        applyButton.onClick = std::move(onApply);
    }

    int getPreferredWidth() const { return 340; }

    void showValidation(const RawImage::HeaderEditResult& result)
    {
        warningLabel.setText(result.warnings.joinIntoString("\n"), juce::dontSendNotification);
        errorLabel.setText(result.blockingErrors.joinIntoString("\n"), juce::dontSendNotification);
        applyButton.setEnabled(result.ok);
        resized();
    }

    void resized() override
    {
        auto area = getLocalBounds();

        auto buttonStrip = area.removeFromBottom(40).reduced(8);
        applyButton.setBounds(buttonStrip.removeFromRight(buttonStrip.getWidth() / 2).reduced(4, 0));
        cancelButton.setBounds(buttonStrip.reduced(4, 0));

        if (errorLabel.getText().isNotEmpty())
            errorLabel.setBounds(area.removeFromBottom(36).reduced(8, 0));
        else
            errorLabel.setBounds({});

        if (warningLabel.getText().isNotEmpty())
            warningLabel.setBounds(area.removeFromBottom(36).reduced(8, 0));
        else
            warningLabel.setBounds({});

        viewport.setBounds(area);
        content.setSize(viewport.getMaximumVisibleWidth(), content.getPreferredHeight());
    }

private:
    // The scrollable body: one row per header field. TextEditor/ComboBox for
    // the 5 editable fields, plain read-only Labels for the other 11.
    class Content : public juce::Component
    {
    public:
        void setUp(const RawImage::BmpHeaderFields& initial, std::function<void()> onAnyChange)
        {
            changed = std::move(onAnyChange);

            addAndMakeVisible(editableSectionLabel);
            editableSectionLabel.setText("Editable (affects rendering)", juce::dontSendNotification);
            editableSectionLabel.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));

            setUpEditableRow(offBitsRowLabel, offBitsEditor, "bfOffBits", juce::String((juce::int64) initial.bfOffBits), false);
            setUpEditableRow(widthRowLabel, widthEditor, "biWidth", juce::String(initial.biWidth), true);
            setUpEditableRow(heightRowLabel, heightEditor, "biHeight", juce::String(initial.biHeight), true);
            setUpEditableRow(bitCountRowLabel, bitCountEditor, "biBitCount", juce::String(initial.biBitCount), false);

            addAndMakeVisible(compressionRowLabel);
            compressionRowLabel.setText("biCompression", juce::dontSendNotification);
            addAndMakeVisible(compressionCombo);
            compressionCombo.addItem("BI_RGB (0, uncompressed)", 1);
            compressionCombo.addItem("BI_RLE8 (1)", 2);
            compressionCombo.addItem("BI_RLE4 (2)", 3);
            compressionCombo.addItem("BI_BITFIELDS (3)", 4);
            compressionCombo.addItem("BI_JPEG (4)", 5);
            compressionCombo.addItem("BI_PNG (5)", 6);
            compressionCombo.addItem("Other...", 7);
            selectCompressionValue(initial.biCompression);
            compressionCombo.onChange = [this] { compressionComboChanged(); };

            addChildComponent(compressionOtherEditor); // only made visible when "Other..." is picked
            compressionOtherEditor.setInputRestrictions(10, "0123456789");
            compressionOtherEditor.onTextChange = [this] { if (changed) changed(); };

            setUpReadOnlyRow(bfTypeRowLabel, bfTypeValue, "bfType", formatFourCcLike(initial.bfType));
            setUpReadOnlyRow(bfSizeRowLabel, bfSizeValue, "bfSize", juce::String(initial.bfSize));
            setUpReadOnlyRow(bfReserved1RowLabel, bfReserved1Value, "bfReserved1", juce::String(initial.bfReserved1));
            setUpReadOnlyRow(bfReserved2RowLabel, bfReserved2Value, "bfReserved2", juce::String(initial.bfReserved2));
            setUpReadOnlyRow(biSizeRowLabel, biSizeValue, "biSize", juce::String(initial.biSize));
            setUpReadOnlyRow(biPlanesRowLabel, biPlanesValue, "biPlanes", juce::String(initial.biPlanes));
            setUpReadOnlyRow(biSizeImageRowLabel, biSizeImageValue, "biSizeImage", juce::String(initial.biSizeImage));
            setUpReadOnlyRow(biXPelsRowLabel, biXPelsValue, "biXPelsPerMeter", juce::String(initial.biXPelsPerMeter));
            setUpReadOnlyRow(biYPelsRowLabel, biYPelsValue, "biYPelsPerMeter", juce::String(initial.biYPelsPerMeter));
            setUpReadOnlyRow(biClrUsedRowLabel, biClrUsedValue, "biClrUsed", juce::String(initial.biClrUsed));
            setUpReadOnlyRow(biClrImportantRowLabel, biClrImportantValue, "biClrImportant", juce::String(initial.biClrImportant));

            addAndMakeVisible(readOnlySectionLabel);
            readOnlySectionLabel.setText("Header info (read-only)", juce::dontSendNotification);
            readOnlySectionLabel.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
        }

        RawImage::BmpEditableHeaderFields getEditableFields() const
        {
            RawImage::BmpEditableHeaderFields fields;
            fields.bfOffBits = (uint32_t) juce::jmax(0, offBitsEditor.getText().getIntValue());
            fields.biWidth = widthEditor.getText().getIntValue();
            fields.biHeight = heightEditor.getText().getIntValue();
            fields.biBitCount = (uint16_t) juce::jmax(0, bitCountEditor.getText().getIntValue());
            fields.biCompression = currentCompressionValue();
            return fields;
        }

        int getPreferredHeight() const
        {
            const int editableRows = 5 + (compressionOtherEditor.isVisible() ? 1 : 0);
            constexpr int readOnlyRows = 11;
            return margin + rowBlock * (1 + editableRows) + sectionGap + rowBlock * (1 + readOnlyRows) + margin;
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced(margin);

            auto layoutSectionHeader = [&] (juce::Component& label)
            {
                label.setBounds(area.removeFromTop(rowHeight));
                area.removeFromTop(rowGap);
            };

            auto layoutRow = [&] (juce::Component& label, juce::Component& field)
            {
                auto row = area.removeFromTop(rowHeight);
                label.setBounds(row.removeFromLeft(labelWidth));
                field.setBounds(row.reduced(4, 0));
                area.removeFromTop(rowGap);
            };

            layoutSectionHeader(editableSectionLabel);
            layoutRow(offBitsRowLabel, offBitsEditor);
            layoutRow(widthRowLabel, widthEditor);
            layoutRow(heightRowLabel, heightEditor);
            layoutRow(bitCountRowLabel, bitCountEditor);
            layoutRow(compressionRowLabel, compressionCombo);

            if (compressionOtherEditor.isVisible())
            {
                auto row = area.removeFromTop(rowHeight);
                row.removeFromLeft(labelWidth);
                compressionOtherEditor.setBounds(row.reduced(4, 0));
                area.removeFromTop(rowGap);
            }

            area.removeFromTop(sectionGap);
            layoutSectionHeader(readOnlySectionLabel);
            layoutRow(bfTypeRowLabel, bfTypeValue);
            layoutRow(bfSizeRowLabel, bfSizeValue);
            layoutRow(bfReserved1RowLabel, bfReserved1Value);
            layoutRow(bfReserved2RowLabel, bfReserved2Value);
            layoutRow(biSizeRowLabel, biSizeValue);
            layoutRow(biPlanesRowLabel, biPlanesValue);
            layoutRow(biSizeImageRowLabel, biSizeImageValue);
            layoutRow(biXPelsRowLabel, biXPelsValue);
            layoutRow(biYPelsRowLabel, biYPelsValue);
            layoutRow(biClrUsedRowLabel, biClrUsedValue);
            layoutRow(biClrImportantRowLabel, biClrImportantValue);
        }

    private:
        static constexpr int rowHeight = 22;
        static constexpr int rowGap = 3;
        static constexpr int rowBlock = rowHeight + rowGap;
        static constexpr int sectionGap = 8;
        static constexpr int margin = 8;
        static constexpr int labelWidth = 130;

        void setUpEditableRow(juce::Label& rowLabel, juce::TextEditor& editor, const char* name,
                               const juce::String& initialText, bool allowSign)
        {
            addAndMakeVisible(rowLabel);
            rowLabel.setText(name, juce::dontSendNotification);
            addAndMakeVisible(editor);
            editor.setInputRestrictions(11, allowSign ? "0123456789-" : "0123456789");
            editor.setText(initialText, juce::dontSendNotification);
            editor.onTextChange = [this] { if (changed) changed(); };
        }

        void setUpReadOnlyRow(juce::Label& rowLabel, juce::Label& valueLabel, const char* name,
                               const juce::String& value)
        {
            addAndMakeVisible(rowLabel);
            rowLabel.setText(name, juce::dontSendNotification);
            addAndMakeVisible(valueLabel);
            valueLabel.setText(value, juce::dontSendNotification);
            valueLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
        }

        void selectCompressionValue(uint32_t value)
        {
            if (value <= 5)
            {
                compressionCombo.setSelectedId((int) value + 1, juce::dontSendNotification);
                compressionOtherEditor.setVisible(false);
            }
            else
            {
                compressionCombo.setSelectedId(7, juce::dontSendNotification);
                compressionOtherEditor.setText(juce::String(value), juce::dontSendNotification);
                compressionOtherEditor.setVisible(true);
            }
        }

        void compressionComboChanged()
        {
            const bool isOther = compressionCombo.getSelectedId() == 7;
            compressionOtherEditor.setVisible(isOther);

            if (isOther && compressionOtherEditor.getText().isEmpty())
                compressionOtherEditor.setText("0", juce::dontSendNotification);

            if (changed)
                changed();
        }

        uint32_t currentCompressionValue() const
        {
            const int selectedId = compressionCombo.getSelectedId();

            if (selectedId >= 1 && selectedId <= 6)
                return (uint32_t) (selectedId - 1);

            return (uint32_t) juce::jmax(0, compressionOtherEditor.getText().getIntValue());
        }

        // bfType is two raw ASCII bytes ('B','M' normally) packed little-endian
        // into a uint16 -- unpack for display, alongside the raw hex value so a
        // deliberately-corrupted bfType is still legible.
        static juce::String formatFourCcLike(uint16_t value)
        {
            const char chars[3] = { (char) (value & 0xFF), (char) ((value >> 8) & 0xFF), 0 };
            return juce::String::createStringFromData(chars, 2) + "  (0x" + juce::String::toHexString(value).toUpperCase() + ")";
        }

        std::function<void()> changed;

        juce::Label editableSectionLabel, readOnlySectionLabel;

        juce::Label offBitsRowLabel;    juce::TextEditor offBitsEditor;
        juce::Label widthRowLabel;      juce::TextEditor widthEditor;
        juce::Label heightRowLabel;     juce::TextEditor heightEditor;
        juce::Label bitCountRowLabel;   juce::TextEditor bitCountEditor;
        juce::Label compressionRowLabel; juce::ComboBox compressionCombo; juce::TextEditor compressionOtherEditor;

        juce::Label bfTypeRowLabel, bfTypeValue;
        juce::Label bfSizeRowLabel, bfSizeValue;
        juce::Label bfReserved1RowLabel, bfReserved1Value;
        juce::Label bfReserved2RowLabel, bfReserved2Value;
        juce::Label biSizeRowLabel, biSizeValue;
        juce::Label biPlanesRowLabel, biPlanesValue;
        juce::Label biSizeImageRowLabel, biSizeImageValue;
        juce::Label biXPelsRowLabel, biXPelsValue;
        juce::Label biYPelsRowLabel, biYPelsValue;
        juce::Label biClrUsedRowLabel, biClrUsedValue;
        juce::Label biClrImportantRowLabel, biClrImportantValue;
    };

    void notifyChanged()
    {
        if (onFieldsChanged)
            onFieldsChanged(content.getEditableFields());
    }

    Content content;
    juce::Viewport viewport;
    juce::Label warningLabel, errorLabel;
    juce::TextButton cancelButton { "Cancel" }, applyButton { "Apply" };
    std::function<void(const RawImage::BmpEditableHeaderFields&)> onFieldsChanged;
};
