#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

// Implements the native macOS File/Edit menu bar for MainComponent, decoupled
// from MainComponent's own state via a small set of query/action callbacks.
// MainComponent still owns undo/redo as ApplicationCommandTarget commands (they're
// entangled with its undo stack), so this class doesn't know the command IDs —
// it just calls populateEditMenu so MainComponent can add its own command items.
class MainMenuModel : public juce::MenuBarModel
{
public:
    struct Callbacks
    {
        std::function<bool()> isScanning;
        std::function<bool()> isPanelOpen;
        std::function<bool()> hasWorkingImage;
        std::function<bool()> hasOriginalImage;
        std::function<void()> onLoadImage;
        std::function<void()> onExportImage;
        std::function<void()> onReset;
        std::function<void()> onRescan;
        std::function<void(juce::PopupMenu&)> populateEditMenu;
    };

    explicit MainMenuModel(Callbacks callbacksIn) : callbacks(std::move(callbacksIn)) {}

    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex(int topLevelMenuIndex, const juce::String& menuName) override;
    void menuItemSelected(int menuItemID, int topLevelMenuIndex) override;

private:
    enum MenuItemIDs
    {
        loadImageMenuItem = 1,
        exportImageMenuItem,
        resetMenuItem,
        rescanPluginsMenuItem
    };

    Callbacks callbacks;
};
