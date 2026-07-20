#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

// Centralises Pixel Bender's visual identity (colours, tab/button/text-editor
// chrome) so every widget shares one coherent look instead of inheriting
// juce::LookAndFeel_V4's stock flat-grey defaults piecemeal. Installed once via
// juce::Desktop::getInstance().setDefaultLookAndFeel() in Main.cpp; other
// paint() overrides in the codebase (PluginListModel, WaveformView,
// WaveformSectionPanel, ZoomableImageView) reference Palette::get() directly
// so hand-drawn components stay in sync with this same palette.
class PixelBenderLookAndFeel : public juce::LookAndFeel_V4
{
public:
    struct Palette
    {
        juce::Colour background    { 0xff1b1c20 }; // window background
        juce::Colour surface       { 0xff222329 }; // panel fill (list, waveform toolbar)
        juce::Colour surfaceRaised { 0xff2a2c33 }; // controls sitting on a surface (search box, buttons)
        juce::Colour border        { 0xff35373f };
        juce::Colour textPrimary   { 0xfff2f3f5 };
        juce::Colour textSecondary { 0xff9aa0ac };
        juce::Colour accent        { 0xff5b9dff };
        juce::Colour gold          { 0xfff2c14e }; // favourite star
        juce::Colour waveform      { 0xff5fd8c8 };

        static const Palette& get()
        {
            static const Palette instance;
            return instance;
        }
    };

    PixelBenderLookAndFeel()
    {
        const auto& p = Palette::get();

        setColour(juce::ResizableWindow::backgroundColourId, p.background);

        setColour(juce::TextButton::buttonColourId, p.surfaceRaised);
        setColour(juce::TextButton::buttonOnColourId, p.accent);
        setColour(juce::TextButton::textColourOffId, p.textPrimary);
        setColour(juce::TextButton::textColourOnId, p.textPrimary);

        setColour(juce::TextEditor::backgroundColourId, p.surfaceRaised);
        setColour(juce::TextEditor::textColourId, p.textPrimary);
        setColour(juce::TextEditor::outlineColourId, p.border);
        setColour(juce::TextEditor::focusedOutlineColourId, p.accent);
        setColour(juce::TextEditor::shadowColourId, juce::Colours::transparentBlack);

        setColour(juce::ComboBox::backgroundColourId, p.surfaceRaised);
        setColour(juce::ComboBox::outlineColourId, p.border);
        setColour(juce::ComboBox::textColourId, p.textPrimary);
        setColour(juce::ComboBox::arrowColourId, p.textSecondary);

        setColour(juce::ListBox::backgroundColourId, p.surface);
        setColour(juce::ScrollBar::thumbColourId, p.border.brighter(0.25f));
        setColour(juce::ScrollBar::backgroundColourId, p.surface);

        setColour(juce::TabbedButtonBar::tabOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::TabbedComponent::outlineColourId, juce::Colours::transparentBlack);

        setColour(juce::Slider::trackColourId, p.border);
        setColour(juce::Slider::thumbColourId, p.accent);
        setColour(juce::Slider::backgroundColourId, p.surface);

        setColour(juce::Label::textColourId, p.textPrimary);
    }

    juce::Font getLabelFont(juce::Label& label) override
    {
        return juce::Font(juce::FontOptions(juce::jmax(13.0f, label.getFont().getHeight())));
    }

    // Rounded pill for the active tab, subtle hover fill otherwise -- replaces
    // TabbedButtonBar's default thin-underline treatment.
    void drawTabButton(juce::TabBarButton& button, juce::Graphics& g, bool isMouseOver, bool isMouseDown) override
    {
        const auto& p = Palette::get();
        auto area = button.getLocalBounds().toFloat().reduced(2.0f, 3.0f);
        const bool active = button.getToggleState();

        if (active)
        {
            g.setColour(p.accent.withAlpha(0.20f));
            g.fillRoundedRectangle(area, 6.0f);
        }
        else if (isMouseOver || isMouseDown)
        {
            g.setColour(p.surfaceRaised);
            g.fillRoundedRectangle(area, 6.0f);
        }

        g.setColour(active ? p.textPrimary : p.textSecondary);
        g.setFont(juce::Font(juce::FontOptions(14.0f).withStyle(active ? "Bold" : "Regular")));
        g.drawText(button.getButtonText(), button.getLocalBounds(), juce::Justification::centred);
    }

    int getTabButtonBestWidth(juce::TabBarButton& button, int) override
    {
        const juce::Font font(juce::FontOptions(14.0f).withStyle("Bold"));
        const auto textWidth = juce::GlyphArrangement::getStringWidth(font, button.getButtonText());
        return juce::jmax(64, (int) textWidth + 32);
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                               bool isMouseOverButton, bool isButtonDown) override
    {
        const auto& p = Palette::get();
        auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);

        auto baseColour = button.getToggleState() ? p.accent : backgroundColour;

        if (isButtonDown)
            baseColour = baseColour.darker(0.2f);
        else if (isMouseOverButton)
            baseColour = baseColour.brighter(0.08f);

        g.setColour(baseColour);
        g.fillRoundedRectangle(bounds, 5.0f);

        g.setColour(p.border);
        g.drawRoundedRectangle(bounds, 5.0f, 1.0f);
    }

    void fillTextEditorBackground(juce::Graphics& g, int width, int height, juce::TextEditor& editor) override
    {
        auto bounds = juce::Rectangle<int>(0, 0, width, height).toFloat().reduced(0.5f);
        g.setColour(editor.findColour(juce::TextEditor::backgroundColourId));
        g.fillRoundedRectangle(bounds, 5.0f);
    }

    void drawTextEditorOutline(juce::Graphics& g, int width, int height, juce::TextEditor& editor) override
    {
        if (! editor.isEnabled())
            return;

        auto bounds = juce::Rectangle<int>(0, 0, width, height).toFloat().reduced(0.5f);
        g.setColour(editor.hasKeyboardFocus(true)
                        ? editor.findColour(juce::TextEditor::focusedOutlineColourId)
                        : editor.findColour(juce::TextEditor::outlineColourId));
        g.drawRoundedRectangle(bounds, 5.0f, 1.0f);
    }
};
