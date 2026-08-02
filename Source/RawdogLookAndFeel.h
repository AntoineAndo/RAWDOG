#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

// Centralises RAWDOG's visual identity: a 1-bit "Platinum Bend" look (System 7
// / Mac OS Platinum chrome -- flat grey panels, white sunken fields, hard
// black 1px borders, no colour accents) so every widget shares one coherent
// look instead of inheriting juce::LookAndFeel_V4's stock flat-grey defaults
// piecemeal. Installed once via juce::Desktop::getInstance().setDefaultLookAndFeel()
// in Main.cpp; other paint() overrides in the codebase (PluginListModel,
// WaveformView, WaveformSectionPanel, ZoomableImageView) reference
// Palette::get() directly so hand-drawn components stay in sync with this
// same palette.
class RawdogLookAndFeel : public juce::LookAndFeel_V4
{
public:
    struct Palette
    {
        juce::Colour windowBg   { 0xffdcdcdc }; // Platinum grey -- panels, chrome, toolbars
        juce::Colour surface    { 0xffffffff }; // list/editor/waveform white fields
        juce::Colour ink        { 0xff000000 }; // borders, primary text, glyphs
        juce::Colour inkMuted   { 0xff777777 }; // secondary text (vendor/format suffix, hints)
        juce::Colour divider    { 0xff999999 }; // dotted separators, inset-shadow lines
        juce::Colour selectedBg { 0xff000000 }; // selected list row fill
        juce::Colour selectedFg { 0xffffffff }; // selected list row text

        static const Palette& get()
        {
            static const Palette instance;
            return instance;
        }
    };

    static constexpr const char* emphasizedPropertyKey = "rawdogEmphasized";

    RawdogLookAndFeel()
    {
        const auto& p = Palette::get();

        setColour(juce::ResizableWindow::backgroundColourId, p.windowBg);

        setColour(juce::TextButton::buttonColourId, p.windowBg);
        // Same solid black/white "selected" convention used throughout this
        // app (rack rows, rail tabs, etc.), distinct from the off colour so a
        // real toggle button (splitModeToggle, currently the only one) shows
        // a visible difference between on/off.
        setColour(juce::TextButton::buttonOnColourId, p.selectedBg);
        setColour(juce::TextButton::textColourOffId, p.ink);
        setColour(juce::TextButton::textColourOnId, p.selectedFg);

        setColour(juce::TextEditor::backgroundColourId, p.surface);
        setColour(juce::TextEditor::textColourId, p.ink);
        setColour(juce::TextEditor::outlineColourId, p.ink);
        setColour(juce::TextEditor::focusedOutlineColourId, p.ink);
        setColour(juce::TextEditor::shadowColourId, juce::Colours::transparentBlack);

        setColour(juce::ComboBox::backgroundColourId, p.surface);
        setColour(juce::ComboBox::outlineColourId, p.ink);
        setColour(juce::ComboBox::textColourId, p.ink);
        setColour(juce::ComboBox::arrowColourId, p.ink);

        setColour(juce::ListBox::backgroundColourId, p.surface);
        setColour(juce::ScrollBar::thumbColourId, p.divider);
        setColour(juce::ScrollBar::backgroundColourId, p.surface);

        setColour(juce::TabbedButtonBar::tabOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::TabbedComponent::outlineColourId, juce::Colours::transparentBlack);

        setColour(juce::Slider::trackColourId, p.divider);
        setColour(juce::Slider::thumbColourId, p.ink);
        setColour(juce::Slider::backgroundColourId, p.surface);

        setColour(juce::Label::textColourId, p.ink);
    }

    juce::Font getLabelFont(juce::Label& label) override
    {
        return juce::Font(juce::FontOptions(juce::jmax(12.0f, label.getFont().getHeight())));
    }

    // Small bold system-font stand-in for the mockup's bitmap "chrome" font
    // (Silkscreen/Chicago) -- tab bars, toolbar labels, status strips. Not a
    // bundled bitmap font: approximated with the platform's default sans at
    // a small, bold, tightly-spaced size, which reads as terse chrome text
    // without adding font assets or build changes.
    static juce::Font chromeFont(float height = 10.0f)
    {
        return juce::Font(juce::FontOptions(height).withStyle("Bold"));
    }

    // Halftone "workspace mat" texture -- used behind the image canvas
    // (letterboxing and the no-image placeholder) instead of a flat grey
    // fill, matching the mockup's dithered/dotted backdrop. Cheap enough to
    // call from a cached-image rebuild or an idle placeholder repaint; not
    // meant for a per-frame hot path. backgroundColour defaults to the same
    // Platinum grey the image canvas uses; other callers (e.g. EffectChainPanel)
    // pass Palette::get().surface for a white mat with the same grey dots.
    static void drawDotMat(juce::Graphics& g, juce::Rectangle<int> area,
                           juce::Colour backgroundColour = Palette::get().windowBg)
    {
        const auto& p = Palette::get();
        g.setColour(backgroundColour);
        g.fillRect(area);

        g.setColour(p.divider.withAlpha(0.6f));
        constexpr float spacing = 6.0f;
        constexpr float dotSize = 1.4f;

        for (float y = (float) area.getY() + spacing * 0.5f; y < (float) area.getBottom(); y += spacing)
            for (float x = (float) area.getX() + spacing * 0.5f; x < (float) area.getRight(); x += spacing)
                g.fillEllipse(x - dotSize * 0.5f, y - dotSize * 0.5f, dotSize, dotSize);
    }

    // Marks a button as the emphasized/default action (e.g. "Apply") --
    // drawButtonBackground() below gives an emphasized button a double-weight
    // border instead of a colour change, matching the mockup's Apply button
    // (2px border) vs its plain siblings (1px).
    static void setEmphasized(juce::Button& button, bool emphasized = true)
    {
        button.getProperties().set(emphasizedPropertyKey, emphasized);
        button.repaint();
    }

    // Flat System-7 folder-tab look: the active tab reads as an extension of
    // the content below it (white fill, no bottom border so it visually
    // joins), inactive tabs recede into the bar's own grey background with a
    // full outline -- replaces TabbedButtonBar's default thin-underline
    // treatment.
    void drawTabButton(juce::TabBarButton& button, juce::Graphics& g, bool /*isMouseOver*/, bool /*isMouseDown*/) override
    {
        const auto& p = Palette::get();
        auto area = button.getLocalBounds().toFloat();
        const bool active = button.getToggleState();

        // Every tab draws the identical full-height border box, so the seam
        // between adjacent tabs always lines up. Only the *fill* shrinks for
        // inactive tabs (a darker chip recessed from the border's top edge),
        // which is what reads as "smaller," matching the mockup's shorter top
        // padding on FAVOURITES/BY VENDOR. Neither state draws a bottom
        // border -- tabs read as flaps attached directly to the content
        // below.
        const auto fillArea = active ? area : area.withTrimmedTop(3.0f);

        g.setColour(active ? p.windowBg : p.windowBg.darker(0.08f));
        g.fillRect(fillArea);

        // Same 1px border weight on every edge, for every tab -- no extra
        // bevel-highlight line under the active tab's top edge, so every
        // tab's top border reads at the same weight.
        g.setColour(p.ink);
        g.drawLine(area.getX(), area.getY(), area.getRight(), area.getY(), 1.0f);
        g.drawLine(area.getX(), area.getY(), area.getX(), area.getBottom(), 1.0f);
        g.drawLine(area.getRight() - 1.0f, area.getY(), area.getRight() - 1.0f, area.getBottom(), 1.0f);

        g.setColour(active ? p.ink : p.inkMuted);
        g.setFont(chromeFont(active ? 11.0f : 9.5f));
        g.drawText(button.getButtonText().toUpperCase(), area.toNearestInt(), juce::Justification::centred);
    }

    int getTabButtonBestWidth(juce::TabBarButton& button, int) override
    {
        // Sized off the larger (active-state) font -- an inactive tab's
        // smaller text still needs to fit the same width so a tab doesn't
        // visibly resize itself when selection changes.
        const auto font = chromeFont(11.0f);
        const auto textWidth = juce::GlyphArrangement::getStringWidth(font, button.getButtonText().toUpperCase());
        return juce::jmax(64, (int) textWidth + 32);
    }

    // Flat Platinum raised-bevel button: hard black outline plus a single
    // inset highlight line along the top+left edges to fake the mockup's
    // `box-shadow: inset 1px 1px 0 #fff` bevel -- no rounding, no colour
    // fills, matching the 1-bit chrome everywhere else.
    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                               bool isMouseOverButton, bool isButtonDown) override
    {
        const auto& p = Palette::get();
        const bool emphasized = button.getProperties()[emphasizedPropertyKey];
        // 1x/2x ratio matching the mockup's own literal CSS border weights
        // (1px plain / 2px Apply).
        const float borderWeight = emphasized ? 2.0f : 1.0f;

        auto fullBounds = button.getLocalBounds().toFloat();
        // Inset by half the border's own weight (not a fixed 0.5px) so a
        // thicker (emphasized) border stays centred on the component edge
        // instead of overhanging it unevenly on one side.
        auto bounds = fullBounds.reduced(borderWeight * 0.5f);

        auto fill = backgroundColour;
        if (isButtonDown)
            fill = fill.darker(0.15f);
        else if (isMouseOverButton)
            fill = fill.darker(0.04f);

        g.setColour(fill);
        g.fillRect(fullBounds);

        g.setColour(button.isEnabled() ? p.ink : p.inkMuted);
        g.drawRect(bounds, borderWeight);

        // Inset highlight bevel, inside the border.
        auto inner = bounds.reduced(borderWeight);
        g.setColour(juce::Colours::white.withAlpha(0.9f));
        g.drawLine(inner.getX(), inner.getY(), inner.getRight(), inner.getY(), 1.0f);
        g.drawLine(inner.getX(), inner.getY(), inner.getX(), inner.getBottom(), 1.0f);
    }

    // LookAndFeel_V4's default drawScrollbar() only paints the thumb (as a
    // rounded rectangle) and leaves the track untouched, so
    // ScrollBar::backgroundColourId (set to the white "surface" colour
    // above) was never actually painted -- the track just showed whatever
    // was behind the scrollbar. Fill the track explicitly, and draw the
    // thumb as a hard-edged rectangle rather than the default's rounded
    // one, matching this theme's flat, hard-cornered widgets elsewhere.
    void drawScrollbar(juce::Graphics& g, juce::ScrollBar& scrollbar, int x, int y, int width, int height,
                        bool isScrollbarVertical, int thumbStartPosition, int thumbSize,
                        bool isMouseOver, bool /*isMouseDown*/) override
    {
        g.setColour(scrollbar.findColour(juce::ScrollBar::backgroundColourId));
        g.fillRect(x, y, width, height);

        juce::Rectangle<int> thumbBounds = isScrollbarVertical
                                                ? juce::Rectangle<int>(x, thumbStartPosition, width, thumbSize)
                                                : juce::Rectangle<int>(thumbStartPosition, y, thumbSize, height);

        auto thumbColour = scrollbar.findColour(juce::ScrollBar::thumbColourId);
        g.setColour(isMouseOver ? thumbColour.brighter(0.25f) : thumbColour);
        g.fillRect(thumbBounds.reduced(1));
    }

    // Draggable panel-split bars (GrippedResizerBar) always show the same
    // faint tint used for the hover/drag state elsewhere, rather than only
    // appearing on interaction -- gives a permanent but subtle affordance
    // without a hard black seam across the panels.
    void drawStretchableLayoutResizerBar(juce::Graphics& g, int w, int h, bool /*isVerticalBar*/,
                                          bool /*isMouseOver*/, bool isMouseDragging) override
    {
        const auto& p = Palette::get();

        g.setColour(p.ink.withAlpha(isMouseDragging ? 0.25f : 0.12f));
        g.fillRect(juce::Rectangle<float>(0.0f, 0.0f, (float) w, (float) h));
    }

    // Flat white sunken field: hard black outline plus a single inset
    // *shadow* line along the top+left edges (the inverse of the button's
    // highlight bevel) so recessed controls read as pressed-in rather than
    // raised, matching the mockup's `box-shadow: inset 1px 1px 0 #999`.
    void fillTextEditorBackground(juce::Graphics& g, int width, int height, juce::TextEditor& editor) override
    {
        auto bounds = juce::Rectangle<int>(0, 0, width, height).toFloat();
        g.setColour(editor.findColour(juce::TextEditor::backgroundColourId));
        g.fillRect(bounds);

        auto inner = bounds.reduced(1.0f);
        g.setColour(Palette::get().divider);
        g.drawLine(inner.getX(), inner.getY(), inner.getRight(), inner.getY(), 1.0f);
        g.drawLine(inner.getX(), inner.getY(), inner.getX(), inner.getBottom(), 1.0f);
    }

    void drawTextEditorOutline(juce::Graphics& g, int width, int height, juce::TextEditor& editor) override
    {
        if (! editor.isEnabled())
            return;

        auto bounds = juce::Rectangle<int>(0, 0, width, height).toFloat().reduced(0.5f);
        g.setColour(editor.hasKeyboardFocus(true)
                        ? editor.findColour(juce::TextEditor::focusedOutlineColourId)
                        : editor.findColour(juce::TextEditor::outlineColourId));
        g.drawRect(bounds, 1.0f);
    }
};
