#include "PluginsSettingsTab.h"
#include "RawdogLookAndFeel.h"
#include <algorithm>

// Plain path-text rows sourced from directoriesStore.getDirectories() --
// juce::ListBox already tracks the selected row itself, so removeDirectoryClicked()
// just reads directoryListBox.getSelectedRow() directly rather than this model
// threading selection state anywhere.
class PluginsSettingsTab::DirectoryListModel : public juce::ListBoxModel
{
public:
    explicit DirectoryListModel(PluginDirectoriesStore& storeIn) : store(storeIn) {}

    int getNumRows() override { return store.getDirectories().size(); }

    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override
    {
        const auto& palette = RawdogLookAndFeel::Palette::get();

        g.setColour(rowIsSelected ? palette.selectedBg : palette.surface);
        g.fillAll();

        const auto directories = store.getDirectories();
        if (! juce::isPositiveAndBelow(rowNumber, directories.size()))
            return;

        g.setColour(rowIsSelected ? palette.selectedFg : palette.ink);
        g.drawText(directories[rowNumber], 8, 0, width - 12, height, juce::Justification::centredLeft);
    }

    void selectedRowsChanged(int) override { if (onSelectionChanged) onSelectionChanged(); }

    std::function<void()> onSelectionChanged;

private:
    PluginDirectoriesStore& store;
};

// Every plugin the last scan found, both formats, completely unfiltered --
// unlike the main editor's PluginListModel, a disabled plugin still shows up
// here (just unchecked), since this is precisely where the user manages that.
class PluginsSettingsTab::PluginChecklistModel : public juce::ListBoxModel
{
public:
    PluginChecklistModel(PluginScanner& scannerIn, PluginEnablementStore& enablementIn, std::function<void()> onChangedIn)
        : scanner(scannerIn), enablementStore(enablementIn), onChanged(std::move(onChangedIn))
    {
        refresh();
    }

    void refresh()
    {
        allTypes = scanner.getKnownPluginList().getTypes();
        applyFilter();
    }

    void setSearchQuery(const juce::String& query)
    {
        searchQuery = query;
        applyFilter();
    }

    int getNumRows() override { return filteredTypes.size(); }

    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override
    {
        const auto& palette = RawdogLookAndFeel::Palette::get();

        g.setColour(rowIsSelected ? palette.selectedBg : palette.surface);
        g.fillAll();

        if (! juce::isPositiveAndBelow(rowNumber, filteredTypes.size()))
            return;

        const auto& desc = filteredTypes.getReference(rowNumber);
        const bool isEnabled = enablementStore.isEnabled(desc.createIdentifierString());

        auto textColour = rowIsSelected ? palette.selectedFg : palette.ink;

        // Drawn as a rectangle, not a font glyph -- guarantees rendering
        // regardless of font glyph coverage, unlike the main list's star icon.
        const auto box = juce::Rectangle<int>(checkboxColumnX, height / 2 - checkboxSize / 2, checkboxSize, checkboxSize);
        g.setColour(textColour);
        g.drawRect(box, 1);
        if (isEnabled)
            g.fillRect(box.reduced(3));

        // Format is shown here (unlike the main list) since a VST3/AU
        // duplicate pair must be visually distinguishable when both rows
        // are on screen at once.
        static const juce::String emDash(juce::CharPointer_UTF8("\xE2\x80\x94"));
        juce::String text;
        text << desc.name << "  " << emDash << "  " << desc.manufacturerName << "  (" << desc.pluginFormatName << ")";

        g.setColour(textColour);
        g.drawText(text, checkboxColumnX + checkboxSize + 8, 0, width - checkboxColumnX - checkboxSize - 12, height,
                    juce::Justification::centredLeft);
    }

    void listBoxItemClicked(int row, const juce::MouseEvent&) override
    {
        if (! juce::isPositiveAndBelow(row, filteredTypes.size()))
            return;

        const auto identifier = filteredTypes.getReference(row).createIdentifierString();
        enablementStore.setEnabled(identifier, ! enablementStore.isEnabled(identifier));

        // An explicit user toggle -- of any plugin, not just a duplicate --
        // always counts as "a default has been assigned", so the
        // duplicate-seeding logic in MainComponent::refreshPluginList()
        // never later overwrites it.
        enablementStore.markDefaultAssigned(identifier);

        if (onChanged)
            onChanged();
    }

    static constexpr int checkboxColumnX = 8;
    static constexpr int checkboxSize = 14;

private:
    // Mirrors PluginListModel::applyFilter()'s name/manufacturer/format
    // search-matching logic.
    void applyFilter()
    {
        filteredTypes.clear();

        for (const auto& desc : allTypes)
        {
            if (searchQuery.isNotEmpty()
                && ! desc.name.containsIgnoreCase(searchQuery)
                && ! desc.manufacturerName.containsIgnoreCase(searchQuery)
                && ! desc.pluginFormatName.containsIgnoreCase(searchQuery))
                continue;

            filteredTypes.add(desc);
        }

        // By name, not scan/vendor order -- this list has no vendor grouping
        // (unlike the main editor's PluginListModel), so name is what the
        // user actually scans for a specific plugin.
        std::sort(filteredTypes.begin(), filteredTypes.end(),
                  [](const juce::PluginDescription& a, const juce::PluginDescription& b)
                  { return a.name.compareIgnoreCase(b.name) < 0; });
    }

    PluginScanner& scanner;
    PluginEnablementStore& enablementStore;
    std::function<void()> onChanged;
    juce::Array<juce::PluginDescription> allTypes;
    juce::Array<juce::PluginDescription> filteredTypes;
    juce::String searchQuery;
};

PluginsSettingsTab::PluginsSettingsTab(PluginDirectoriesStore& directoriesStoreIn,
                                        PluginEnablementStore& enablementStoreIn,
                                        PluginScanner& scannerIn,
                                        std::function<void()> onRescanRequestedIn,
                                        std::function<void()> onEnablementChangedIn)
    : directoriesStore(directoriesStoreIn), enablementStore(enablementStoreIn), scanner(scannerIn),
      onRescanRequested(std::move(onRescanRequestedIn)), onEnablementChanged(std::move(onEnablementChangedIn))
{
    directoriesLabel.setFont(RawdogLookAndFeel::chromeFont(11.0f));
    addAndMakeVisible(directoriesLabel);

    directoryListModel = std::make_unique<DirectoryListModel>(directoriesStore);
    directoryListModel->onSelectionChanged = [this] { updateRemoveButtonEnablement(); };
    directoryListBox.setModel(directoryListModel.get());
    addAndMakeVisible(directoryListBox);

    addDirectoryButton.onClick = [this] { addDirectoryClicked(); };
    addAndMakeVisible(addDirectoryButton);

    removeDirectoryButton.onClick = [this] { removeDirectoryClicked(); };
    removeDirectoryButton.setEnabled(false);
    addAndMakeVisible(removeDirectoryButton);

    revertDirectoriesButton.onClick = [this] { revertDirectoriesClicked(); };
    addAndMakeVisible(revertDirectoriesButton);

    pluginsLabel.setFont(RawdogLookAndFeel::chromeFont(11.0f));
    addAndMakeVisible(pluginsLabel);

    pluginSearchField.setTextToShowWhenEmpty("Search plugins...", RawdogLookAndFeel::Palette::get().inkMuted);
    pluginSearchField.onTextChange = [this](const juce::String& query)
    {
        pluginChecklistModel->setSearchQuery(query);
        pluginChecklistBox.updateContent();
        pluginChecklistBox.repaint();
    };
    addAndMakeVisible(pluginSearchField);

    pluginChecklistModel = std::make_unique<PluginChecklistModel>(scanner, enablementStore,
        [this] { pluginChecklistBox.repaint(); if (onEnablementChanged) onEnablementChanged(); });
    pluginChecklistBox.setModel(pluginChecklistModel.get());
    addAndMakeVisible(pluginChecklistBox);

    RawdogLookAndFeel::setEmphasized(rescanButton);
    rescanButton.onClick = [this]
    {
        if (onRescanRequested)
            onRescanRequested();
    };
    addAndMakeVisible(rescanButton);
}

PluginsSettingsTab::~PluginsSettingsTab()
{
    directoryListBox.setModel(nullptr);
    pluginChecklistBox.setModel(nullptr);
}

void PluginsSettingsTab::visibilityChanged()
{
    if (! isVisible())
    {
        stopTimer();
        return;
    }

    pluginChecklistModel->refresh();
    pluginChecklistBox.updateContent();
    pluginChecklistBox.repaint();

    startTimer(250);
}

void PluginsSettingsTab::timerCallback()
{
    if (scanner.isScanning())
        return;

    pluginChecklistModel->refresh();
    pluginChecklistBox.updateContent();
    pluginChecklistBox.repaint();
}

void PluginsSettingsTab::addDirectoryClicked()
{
    directoryChooser = std::make_unique<juce::FileChooser>("Choose a plugin directory to scan");

    directoryChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [this](const juce::FileChooser& fc)
        {
            const auto dir = fc.getResult();
            if (! dir.isDirectory())
                return;

            directoriesStore.addDirectory(dir);
            directoryListBox.updateContent();
            directoryListBox.repaint();
            // Deliberately not triggering a rescan -- directory changes only
            // take effect on the next explicit Rescan click.
        });
}

void PluginsSettingsTab::removeDirectoryClicked()
{
    const auto row = directoryListBox.getSelectedRow();
    const auto directories = directoriesStore.getDirectories();

    if (! juce::isPositiveAndBelow(row, directories.size()))
        return;

    directoriesStore.removeDirectory(directories[row]);
    directoryListBox.updateContent();
    directoryListBox.repaint();
    updateRemoveButtonEnablement();
}

void PluginsSettingsTab::revertDirectoriesClicked()
{
    auto options = juce::MessageBoxOptions::makeOptionsOkCancel(
        juce::MessageBoxIconType::WarningIcon, "Revert to Default",
        "Discard all custom directories and restore the default VST3/AU search locations?",
        "Revert", "Cancel");

    juce::AlertWindow::showAsync(options, [this](int result)
    {
        // 1 == the first button ("Revert"), per the same empirical convention
        // PluginListModel::confirmAndDeletePreset already documents.
        if (result != 1)
            return;

        directoriesStore.resetToDefaults();
        directoryListBox.updateContent();
        directoryListBox.repaint();
        updateRemoveButtonEnablement();
        // Deliberately not triggering a rescan -- same rule as Add/Remove.
    });
}

void PluginsSettingsTab::updateRemoveButtonEnablement()
{
    removeDirectoryButton.setEnabled(directoryListBox.getSelectedRow() >= 0);
}

void PluginsSettingsTab::resized()
{
    auto area = getLocalBounds().reduced(12);

    directoriesLabel.setBounds(area.removeFromTop(20));
    directoryListBox.setBounds(area.removeFromTop(110));
    area.removeFromTop(4);

    auto directoryButtonRow = area.removeFromTop(28);
    addDirectoryButton.setBounds(directoryButtonRow.removeFromLeft(130));
    directoryButtonRow.removeFromLeft(8);
    removeDirectoryButton.setBounds(directoryButtonRow.removeFromLeft(80));
    directoryButtonRow.removeFromLeft(8);
    revertDirectoriesButton.setBounds(directoryButtonRow.removeFromLeft(140));

    area.removeFromTop(16);

    pluginsLabel.setBounds(area.removeFromTop(20));
    area.removeFromTop(4);
    pluginSearchField.setBounds(area.removeFromTop(28));
    area.removeFromTop(4);

    auto rescanRow = area.removeFromBottom(28);
    pluginChecklistBox.setBounds(area.reduced(0, 4));

    rescanButton.setBounds(rescanRow.removeFromLeft(150));
}
