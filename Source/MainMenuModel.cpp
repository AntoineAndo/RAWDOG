#include "MainMenuModel.h"

juce::StringArray MainMenuModel::getMenuBarNames()
{
    return { "File", "Edit" };
}

juce::PopupMenu MainMenuModel::getMenuForIndex(int topLevelMenuIndex, const juce::String& /*menuName*/)
{
    juce::PopupMenu menu;

    const bool panelOpen = callbacks.isPanelOpen();

    if (topLevelMenuIndex == 0)
    {
        callbacks.populateFileMenuLoadImageItem(menu);
        menu.addItem(editHeaderMenuItem, "Edit Header...", callbacks.canEditHeader() && ! panelOpen);
        // Gating on ! panelOpen (also covered by exportImageCommand's own
        // setActive()) matters because while a plugin's live-preview session
        // is active, RawImage's render caches (toJuceImage()'s
        // cachedPlainImage) are being touched by the live-preview worker
        // thread -- see PROJECT.md's live-preview performance note. Exporting
        // is also more sensible gated this way anyway: it would otherwise
        // export the last-committed image, not the current unapplied preview.
        callbacks.populateFileMenuExportItem(menu);
        callbacks.populateFileMenuResetItem(menu);
    }
    else if (topLevelMenuIndex == 1)
    {
        callbacks.populateEditMenu(menu);
        menu.addItem(fileModifierMenuItem, "Add File Modifier...", callbacks.canOpenFileModifier() && ! panelOpen);
    }

    return menu;
}

void MainMenuModel::menuItemSelected(int menuItemID, int /*topLevelMenuIndex*/)
{
    switch (menuItemID)
    {
        case editHeaderMenuItem: callbacks.onEditHeader(); break;
        case fileModifierMenuItem: callbacks.onOpenFileModifier(); break;
        default: break;
    }
}
