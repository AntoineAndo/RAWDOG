#include "SettingsWindow.h"
#include "RawdogLookAndFeel.h"

SettingsWindow::Content::Content(PluginDirectoriesStore& directoriesStore,
                                  PluginEnablementStore& enablementStore,
                                  PluginScanner& scanner,
                                  std::function<void()> onRescanRequested,
                                  std::function<void()> onEnablementChanged,
                                  std::function<void()> onOkClicked)
    : pluginsTab(directoriesStore, enablementStore, scanner, std::move(onRescanRequested), std::move(onEnablementChanged))
{
    addAndMakeVisible(tabs);
    tabs.addTab("Plugins", RawdogLookAndFeel::Palette::get().windowBg, &pluginsTab, false);

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

SettingsWindow::SettingsWindow(PluginDirectoriesStore& directoriesStore,
                                PluginEnablementStore& enablementStore,
                                PluginScanner& scanner,
                                std::function<void()> onRescanRequested,
                                std::function<void()> onEnablementChanged)
    : DocumentWindow("Settings",
                      RawdogLookAndFeel::Palette::get().windowBg,
                      DocumentWindow::closeButton),
      content(directoriesStore, enablementStore, scanner, std::move(onRescanRequested), std::move(onEnablementChanged),
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
