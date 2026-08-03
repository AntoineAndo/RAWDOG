#include "SettingsWindow.h"
#include "RawdogLookAndFeel.h"

SettingsWindow::Content::Content(PluginDirectoriesStore& directoriesStore,
                                  PluginEnablementStore& enablementStore,
                                  AppearanceSettingsStore& appearanceStore,
                                  PluginScanner& scanner,
                                  std::function<void()> onRescanRequested,
                                  std::function<void()> onEnablementChanged,
                                  std::function<void()> onAppearanceChanged,
                                  std::function<void()> onOkClicked)
    : pluginsTab(directoriesStore, enablementStore, scanner, std::move(onRescanRequested), std::move(onEnablementChanged)),
      appearanceTab(appearanceStore, std::move(onAppearanceChanged))
{
    addAndMakeVisible(tabs);
    tabs.addTab("Plugins", RawdogLookAndFeel::Palette::get().windowBg, &pluginsTab, false);
    tabs.addTab("Appearance", RawdogLookAndFeel::Palette::get().windowBg, &appearanceTab, false);

    RawdogLookAndFeel::setEmphasized(okButton);
    okButton.onClick = std::move(onOkClicked);
    addAndMakeVisible(okButton);
}

void SettingsWindow::Content::resized()
{
    auto area = getLocalBounds();

    auto footer = area.removeFromBottom(40).reduced(8);
    okButton.setBounds(footer.removeFromRight(90));

    tabs.setBounds(area);
}

void SettingsWindow::Content::lookAndFeelChanged()
{
    tabs.setTabBackgroundColour(0, RawdogLookAndFeel::Palette::get().windowBg);
    tabs.setTabBackgroundColour(1, RawdogLookAndFeel::Palette::get().windowBg);
}

SettingsWindow::SettingsWindow(PluginDirectoriesStore& directoriesStore,
                                PluginEnablementStore& enablementStore,
                                AppearanceSettingsStore& appearanceStore,
                                PluginScanner& scanner,
                                std::function<void()> onRescanRequested,
                                std::function<void()> onEnablementChanged,
                                std::function<void()> onAppearanceChanged)
    : DocumentWindow("Settings",
                      RawdogLookAndFeel::Palette::get().windowBg,
                      DocumentWindow::closeButton),
      content(directoriesStore, enablementStore, appearanceStore, scanner,
              std::move(onRescanRequested), std::move(onEnablementChanged), std::move(onAppearanceChanged),
              [this] { setVisible(false); })
{
    setUsingNativeTitleBar(true);
    setResizable(true, true);
    setResizeLimits(420, 420, 900, 900);

    // content is a member, not heap-allocated -- setContentOwned() would have
    // DocumentWindow's destructor try to delete it, double-freeing.
    setContentNonOwned(&content, true);

    centreWithSize(520, 480);
}

void SettingsWindow::lookAndFeelChanged()
{
    setBackgroundColour(RawdogLookAndFeel::Palette::get().windowBg);
}
