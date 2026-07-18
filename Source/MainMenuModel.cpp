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
        menu.addItem(loadImageMenuItem, "Load Image...", ! panelOpen);
        menu.addItem(editHeaderMenuItem, "Edit Header...", callbacks.canEditHeader() && ! panelOpen);
        // Also gated on ! panelOpen: while a plugin's live-preview session is
        // active, RawImage's render caches (toJuceImage()'s cachedPlainImage)
        // are being touched by the live-preview worker thread -- see
        // PROJECT.md's live-preview performance note. Exporting is also more
        // sensible gated this way anyway: it would otherwise export the
        // last-committed image, not the current unapplied preview.
        menu.addItem(exportImageMenuItem, "Export Image...", callbacks.hasWorkingImage() && ! panelOpen);
        menu.addItem(resetMenuItem, "Reset to Original", callbacks.hasOriginalImage() && ! panelOpen);
        menu.addSeparator();
        menu.addItem(rescanPluginsMenuItem, "Rescan Plugins", ! callbacks.isScanning() && ! panelOpen);
    }
    else if (topLevelMenuIndex == 1)
    {
        callbacks.populateEditMenu(menu);
    }

    return menu;
}

void MainMenuModel::menuItemSelected(int menuItemID, int /*topLevelMenuIndex*/)
{
    switch (menuItemID)
    {
        case loadImageMenuItem:      callbacks.onLoadImage(); break;
        case exportImageMenuItem:    callbacks.onExportImage(); break;
        case resetMenuItem:          callbacks.onReset(); break;
        case rescanPluginsMenuItem:  callbacks.onRescan(); break;
        case editHeaderMenuItem:     callbacks.onEditHeader(); break;
        default: break;
    }
}
