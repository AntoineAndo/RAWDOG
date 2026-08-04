#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "GeneralSettingsStore.h"
#include "LeftColumnPanel.h"
#include "PluginDirectoriesStore.h"
#include "PluginEnablementStore.h"
#include "PluginScanner.h"

// SettingsWindow's "Plugins" tab: an editable list of directories to scan, a
// checklist of every plugin the last scan found (both formats, unfiltered --
// unlike the main editor's PluginListModel, which hides disabled plugins
// entirely), a Rescan button driving the same scan pipeline the File menu's
// "Rescan Plugins" item uses, and a checkbox for whether a selected chain
// slot's editor opens embedded below the effect chain rack or in its own
// floating window (see MainComponent::selectChainSlot()).
class PluginsSettingsTab : public juce::Component,
                           private juce::Timer
{
public:
    PluginsSettingsTab(PluginDirectoriesStore& directoriesStore,
                        PluginEnablementStore& enablementStore,
                        GeneralSettingsStore& generalStore,
                        PluginScanner& scanner,
                        std::function<void()> onRescanRequested,
                        std::function<void()> onEnablementChanged);
    ~PluginsSettingsTab() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

    // Refreshes immediately (in case a scan finished while this tab was
    // hidden -- e.g. the File menu's "Rescan Plugins" item, which has no
    // direct path into this class) and starts the polling timer, which then
    // keeps the checklist in sync with any rescan that happens while visible
    // (this tab's own Rescan button, or the File menu one) -- see
    // timerCallback()'s doc comment.
    void visibilityChanged() override;

private:
    class DirectoryListModel;
    class PluginChecklistModel;

    // Runs continuously while this tab is visible (started/stopped by
    // visibilityChanged() above), not just after this tab's own Rescan button
    // is clicked -- refreshPluginList()'s single completion callback only
    // refreshes the main editor's list, and a rescan can also be triggered
    // from the File menu while this window stays open (it's non-modal), so
    // polling isScanning() is how the checklist notices either kind of scan
    // has finished and repopulates with whatever's new (including freshly-
    // seeded duplicate defaults).
    void timerCallback() override;

    PluginDirectoriesStore& directoriesStore;
    PluginEnablementStore& enablementStore;
    GeneralSettingsStore& generalStore;
    PluginScanner& scanner;
    std::function<void()> onRescanRequested;
    std::function<void()> onEnablementChanged;

    juce::Label directoriesLabel { {}, "PLUGIN DIRECTORIES" };
    juce::ListBox directoryListBox;
    std::unique_ptr<DirectoryListModel> directoryListModel;
    juce::TextButton addDirectoryButton { "Add Directory..." };
    juce::TextButton removeDirectoryButton { "Remove" };
    juce::TextButton revertDirectoriesButton { "Revert to Default" };

    juce::Label pluginsLabel { {}, "INSTALLED PLUGINS" };
    LeftColumnPanel::SearchField pluginSearchField;
    juce::ListBox pluginChecklistBox;
    std::unique_ptr<PluginChecklistModel> pluginChecklistModel;

    juce::TextButton rescanButton { "Rescan Plugins" };

    juce::ToggleButton pluginWindowModeToggle { "Open plugins in a separate window" };

    std::unique_ptr<juce::FileChooser> directoryChooser;

    // Section-header divider lines (drawn in paint()) and the rules above the
    // rescan row and the window-mode row below it -- cached in resized()
    // alongside the layout math that determines them, same pattern
    // RightColumnPanel/WaveformSectionPanel use for their own fixed strip
    // dividers.
    int directoriesHeaderDividerY = 0;
    int pluginsHeaderDividerY = 0;
    int rescanDividerY = 0;
    int windowModeDividerY = 0;

    void addDirectoryClicked();
    void removeDirectoryClicked();
    void revertDirectoriesClicked();
    void updateRemoveButtonEnablement();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginsSettingsTab)
};
