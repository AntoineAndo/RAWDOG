#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "RawdogLookAndFeel.h"

// The app's About panel, opened from the "About" item in the native
// "RAWDOG" app menu (see MainComponent::aboutClicked()) -- no OS-provided
// standard About panel exists to fall back on, since createStandardAppMenu()
// (JUCE's native mac app-menu builder) only ever adds Services/Hide/Quit, not
// About. Same transient, hide-rather-than-destroy lifetime as SettingsWindow.
class AboutWindow : public juce::DocumentWindow
{
public:
    AboutWindow();

    void closeButtonPressed() override { setVisible(false); }

    // The background colour passed to DocumentWindow's constructor is cached
    // as an explicit colour rather than tracked live -- reapply it from
    // Palette::get() whenever RawdogLookAndFeel::refreshAllWindows() fires a
    // theme switch (same reasoning as SettingsWindow::lookAndFeelChanged()).
    void lookAndFeelChanged() override;

private:
    class Content : public juce::Component
    {
    public:
        explicit Content(std::function<void()> onOkClicked);

        void resized() override;

        // authorLabel's textColourId (set once in the constructor) is
        // cached on the Label rather than looked up from the LookAndFeel per
        // paint, so a theme switch needs this reapplied explicitly.
        void lookAndFeelChanged() override
        {
            authorLabel.setColour(juce::Label::textColourId, RawdogLookAndFeel::Palette::get().inkMuted);
        }

    private:
        juce::ImageComponent iconComponent;
        juce::Label titleLabel { {}, "RAWDOG" };
        juce::Label versionLabel;
        juce::Label authorLabel { {}, "By Antoine Ando" };
        juce::HyperlinkButton emailLink { "antoine.ando@protonmail.com",
                                          juce::URL("mailto:antoine.ando@protonmail.com") };
        juce::HyperlinkButton githubLink { "github.com/AntoineAndo/RAWDOG",
                                            juce::URL("https://github.com/AntoineAndo/RAWDOG") };
        juce::TextButton okButton { "OK" };
    };

    Content content;
};
