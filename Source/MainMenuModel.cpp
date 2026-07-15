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
        menu.addItem(exportImageMenuItem, "Export Image...", callbacks.hasWorkingImage());
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
        default: break;
    }
}
