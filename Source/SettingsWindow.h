#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginDirectoriesStore.h"
#include "PluginEnablementStore.h"
#include "PluginScanner.h"
#include "PluginsSettingsTab.h"

// The app's Settings window, opened from the "Settings..." item in the native
// "RAWDOG" app menu (see MainComponent::openSettingsClicked()). First
// secondary/auxiliary window in this codebase -- unlike Main.cpp's MainWindow
// (the single lifetime-of-app main window), this one is transient: the user
// opens/closes it freely while MainComponent stays alive underneath, and
// closing it just hides the window rather than requesting app quit.
// Currently holds one tab, "Plugins"; more tabs go here later.
class SettingsWindow : public juce::DocumentWindow
{
public:
    SettingsWindow(PluginDirectoriesStore& directoriesStore,
                   PluginEnablementStore& enablementStore,
                   PluginScanner& scanner,
                   std::function<void()> onRescanRequested,
                   std::function<void()> onEnablementChanged);

    // Hides rather than destroys/quits -- MainComponent keeps this window
    // alive via its own unique_ptr and just re-shows it on the next
    // "Settings..." click, so its content (directory list, checklist scroll
    // position) isn't lost between opens.
    void closeButtonPressed() override { setVisible(false); }

private:
    // Wraps the tab strip + a bottom-right "OK" button (same close semantics
    // as the native close button, just a second affordance) -- belongs at
    // this window level, not inside PluginsSettingsTab, since it's meant to
    // apply across whichever tabs exist once more are added later.
    class Content : public juce::Component
    {
    public:
        Content(PluginDirectoriesStore& directoriesStore,
                PluginEnablementStore& enablementStore,
                PluginScanner& scanner,
                std::function<void()> onRescanRequested,
                std::function<void()> onEnablementChanged,
                std::function<void()> onOkClicked);

        void resized() override;

    private:
        juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
        PluginsSettingsTab pluginsTab;
        juce::TextButton okButton { "OK" };
    };

    Content content;
};
