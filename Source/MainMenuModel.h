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
        std::function<bool()> isPanelOpen;
        std::function<void(juce::PopupMenu&)> populateEditMenu;

        // Appended at the end, not inserted -- Callbacks is initialised
        // positionally at MainComponent.h's menuModel member, so inserting a
        // field in the middle would silently scramble every later one.
        std::function<bool()> canEditHeader;
        std::function<void()> onEditHeader;

        // Adds the "Load Image..." item as an ApplicationCommand item (via
        // commandManager.addCommandItem), not a plain menu.addItem() --
        // MainComponent owns loadImageCommand (entangled with its
        // ApplicationCommandTarget machinery, same reason populateEditMenu
        // above works this way for undo/redo), so this class doesn't know the
        // command ID; it just gives MainComponent the menu to add it to. That's
        // what makes the Cmd+O shortcut appear next to the item.
        std::function<void(juce::PopupMenu&)> populateFileMenuLoadImageItem;

        // Adds "Reset to Original" (resetCommand, Cmd+Shift+R) as an
        // ApplicationCommand item, not a plain menu.addItem() -- see
        // populateFileMenuLoadImageItem's comment above for why.
        std::function<void(juce::PopupMenu&)> populateFileMenuResetItem;

        // Adds "Export Image..." (exportImageCommand, Cmd+S) as an
        // ApplicationCommand item, not a plain menu.addItem() -- see
        // populateFileMenuLoadImageItem's comment above for why.
        std::function<void(juce::PopupMenu&)> populateFileMenuExportItem;
    };

    explicit MainMenuModel(Callbacks callbacksIn) : callbacks(std::move(callbacksIn)) {}

    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex(int topLevelMenuIndex, const juce::String& menuName) override;
    void menuItemSelected(int menuItemID, int topLevelMenuIndex) override;

private:
    enum MenuItemIDs
    {
        editHeaderMenuItem = 1
    };

    Callbacks callbacks;
};
