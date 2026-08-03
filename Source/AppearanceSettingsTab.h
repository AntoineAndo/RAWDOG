#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "AppearanceSettingsStore.h"

// SettingsWindow's "Appearance" tab: a two-button Light/Dark segmented
// choice (mutually exclusive via a shared radio group, not a checkbox --
// there are exactly two named themes, not an on/off toggle), persisted via
// AppearanceSettingsStore and applied app-wide immediately on click (not
// deferred to the window's "OK" button, matching how the Plugins tab's
// enablement checkboxes take effect immediately).
class AppearanceSettingsTab : public juce::Component
{
public:
    AppearanceSettingsTab(AppearanceSettingsStore& store, std::function<void()> onAppearanceChanged);

    void resized() override;

private:
    // Small hand-drawn sun/moon glyphs plus a label, drawn as one button --
    // avoids the LookAndFeel's plain text button reading as just a labelled
    // radio option, matching the 1-bit chrome's preference for a drawn glyph
    // over relying on font glyph coverage (see PluginsSettingsTab's checkbox
    // squares for the same reasoning).
    class ThemeButton : public juce::TextButton
    {
    public:
        enum class Icon { sun, moon };

        ThemeButton(Icon iconIn, const juce::String& label) : juce::TextButton(label), icon(iconIn) {}

        void paintButton(juce::Graphics& g, bool isMouseOverButton, bool isButtonDown) override;

    private:
        Icon icon;
    };

    void setDarkModeEnabled(bool enabled);

    AppearanceSettingsStore& store;
    std::function<void()> onAppearanceChanged;

    juce::Label appearanceLabel { {}, "Appearance" };
    ThemeButton lightButton { ThemeButton::Icon::sun, "Light" };
    ThemeButton darkButton { ThemeButton::Icon::moon, "Dark" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AppearanceSettingsTab)
};
