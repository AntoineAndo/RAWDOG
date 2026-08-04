#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginEditorPanel.h"
#include "RawdogLookAndFeel.h"

// Floating alternative to embedding a chain slot's PluginEditorPanel below
// the effect chain rack (see LeftColumnPanel::setEditorPanel()), used when
// GeneralSettingsStore::isPluginWindowModeEnabled() is true. Non-owning: the
// panel is (and stays) owned by MainComponent's pluginEditorPanel unique_ptr
// regardless of which of the two presentations is showing it, exactly like
// SettingsWindow's Content is a member, not heap-allocated -- setContentOwned()
// would have this window's destructor try to delete the panel, double-freeing
// it once MainComponent's own unique_ptr runs.
class PluginEditorWindow : public juce::DocumentWindow
{
public:
    PluginEditorWindow(const juce::String& pluginName, PluginEditorPanel& panelToShow)
        : DocumentWindow(pluginName, RawdogLookAndFeel::Palette::get().windowBg, DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar(true);
        setResizable(true, true);
        setResizeLimits(300, 200, 10000, 10000);
        setContentNonOwned(&panelToShow, true);

        // Fits the editor's own natural size (plus PluginEditorPanel's top
        // strip) rather than a fixed guess -- jmax'd against a sane minimum
        // so a tiny/degenerate editor doesn't produce an unusably small
        // window.
        centreWithSize(juce::jmax(panelToShow.getPreferredWidth(), 300),
                        juce::jmax(panelToShow.getPreferredHeight(), 200));
    }

    // Deferred via callAsync -- onCloseButtonPressed (MainComponent's
    // deselectChainSlot()) resets the unique_ptr that owns this very window,
    // which would destroy `this` mid-call if invoked synchronously from
    // within its own closeButtonPressed().
    void closeButtonPressed() override
    {
        juce::MessageManager::callAsync([callback = onCloseButtonPressed]
        {
            if (callback)
                callback();
        });
    }

    void lookAndFeelChanged() override
    {
        setBackgroundColour(RawdogLookAndFeel::Palette::get().windowBg);
    }

    std::function<void()> onCloseButtonPressed;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditorWindow)
};
