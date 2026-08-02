#include "PluginListModel.h"
#include "RawdogLookAndFeel.h"
#include <cmath>

// Shared by both the ungrouped path and the grouped-leaf-row path: draws the
// disclosure triangle (only if this plugin has presets), the favourite star,
// and the plugin's name/manufacturer/format text, starting at a
// caller-supplied left indent (0 for the flat list, a small extra indent for
// leaf rows nested under a vendor header).
void PluginListModel::paintPluginRow(juce::Graphics& g, const juce::PluginDescription& desc, bool isFavourite,
                                       bool hasPresets, bool presetsExpanded, bool rowIsSelected, int leftIndent, int width, int height)
{
    const auto& palette = RawdogLookAndFeel::Palette::get();
    auto textColour = rowIsSelected ? palette.selectedFg : palette.ink;
    auto mutedColour = rowIsSelected ? palette.selectedFg.withAlpha(0.75f) : palette.inkMuted;

    // Dims every glyph on the row so a disabled list -- no image loaded --
    // reads as a single greyed-out block rather than a row with one dim word
    // floating in otherwise full-contrast chrome.
    if (! enabled)
    {
        textColour = textColour.withMultipliedAlpha(0.4f);
        mutedColour = mutedColour.withMultipliedAlpha(0.4f);
    }

    if (hasPresets)
    {
        g.setColour(mutedColour);
        g.drawText(presetsExpanded ? juce::CharPointer_UTF8("\xE2\x96\xBE") : juce::CharPointer_UTF8("\xE2\x96\xB8"),
                    leftIndent, 0, disclosureColumnWidth, height, juce::Justification::centred);
    }

    // The disclosure column is always reserved (even when blank, for a
    // plugin with no presets) so the star's x position never shifts
    // depending on whether this particular plugin happens to have any.
    const int starX = leftIndent + disclosureColumnWidth;

    g.setColour(isFavourite ? textColour : mutedColour);
    g.drawText(isFavourite ? juce::CharPointer_UTF8("\xE2\x98\x85") : juce::CharPointer_UTF8("\xE2\x98\x86"),
                starX, 0, starColumnWidth, height, juce::Justification::centred);

    // Name and vendor/format are drawn as two separate passes, not one string,
    // so the vendor/format suffix can be visually de-emphasized (dimmer colour)
    // relative to the name -- with dozens of plugins sharing the same vendor,
    // a uniform-weight "Name  —  Vendor  (Format)" on every row reads as a wall
    // of near-identical text; making the name the one bright element per row
    // is what actually needs scanning.
    const auto textArea = juce::Rectangle<int>(starX + starColumnWidth + 4, 0,
                                                width - starX - starColumnWidth - 8, height);
    const auto& font = g.getCurrentFont();
    const auto nameWidth = juce::jmin((float) textArea.getWidth(), juce::GlyphArrangement::getStringWidth(font, desc.name));

    g.setColour(textColour);
    g.drawText(desc.name, textArea.getX(), textArea.getY(), (int) std::ceil(nameWidth), height,
                juce::Justification::centredLeft);

    // Built via operator<< onto an already-constructed juce::String, not
    // operator+ starting from a raw "  —  " literal: juce::String's
    // const-char*-taking *constructor* (invoked by the free
    // operator+(const char*, const String&) that a leading raw literal would
    // trigger) treats its input as ASCII, not UTF-8 -- only operator+=/<<
    // on an existing String use the UTF-8-safe path. Starting from an empty
    // String and appending (ASCII-only literals are byte-identical either
    // way; the em dash is explicit CharPointer_UTF8, same convention as the
    // star glyphs above) sidesteps that gotcha entirely.
    static const juce::String emDash(juce::CharPointer_UTF8("\xE2\x80\x94"));

    juce::String suffix;
    suffix << "  " << emDash << "  " << desc.manufacturerName << "  (" << desc.pluginFormatName << ")";

    g.setColour(mutedColour);
    g.drawText(suffix, textArea.getX() + (int) nameWidth, 0, textArea.getWidth() - (int) nameWidth, height,
                juce::Justification::centredLeft);
}

void PluginListModel::paintPresetRow(juce::Graphics& g, const juce::String& presetName, bool rowIsSelected, int leftIndent, int width, int height)
{
    const auto& palette = RawdogLookAndFeel::Palette::get();
    auto textColour = rowIsSelected ? palette.selectedFg : palette.inkMuted;

    if (! enabled)
        textColour = textColour.withMultipliedAlpha(0.4f);

    g.setColour(textColour);
    g.drawText(presetName, leftIndent, 0, width - leftIndent - presetMenuColumnWidth, height,
                juce::Justification::centredLeft);

    g.setColour(textColour.withAlpha(0.7f));
    g.drawText(juce::CharPointer_UTF8("\xE2\x8B\xAE"), width - presetMenuColumnWidth, 0, presetMenuColumnWidth, height,
                juce::Justification::centred);
}

void PluginListModel::paintListBoxItem(int rowNumber, juce::Graphics& g,
                                         int width, int height, bool rowIsSelected)
{
    const auto& palette = RawdogLookAndFeel::Palette::get();

    g.setColour(palette.surface);
    g.fillAll();

    if (rowIsSelected)
    {
        g.setColour(palette.selectedBg);
        g.fillAll();
    }

    if (! juce::isPositiveAndBelow(rowNumber, displayRows.size()))
        return;

    const auto& displayRow = displayRows.getReference(rowNumber);

    if (displayRow.kind == DisplayRow::Kind::vendorHeader)
    {
        g.setColour(palette.windowBg);
        g.fillAll();

        const bool isExpanded = expandedVendors.contains(displayRow.vendorName);

        int matchingCount = 0;
        for (const auto& desc : cachedTypes)
            if (desc.manufacturerName == displayRow.vendorName)
                ++matchingCount;

        g.setColour(enabled ? palette.ink : palette.ink.withMultipliedAlpha(0.4f));
        g.drawText(isExpanded ? juce::CharPointer_UTF8("\xE2\x96\xBE") : juce::CharPointer_UTF8("\xE2\x96\xB8"),
                    4, 0, starColumnWidth, height, juce::Justification::centred);
        g.setFont(RawdogLookAndFeel::chromeFont(11.0f));
        g.drawText(displayRow.vendorName + " (" + juce::String(matchingCount) + ")",
                    starColumnWidth + 8, 0, width - starColumnWidth - 12, height, juce::Justification::centredLeft);
        return;
    }

    const auto& desc = cachedTypes.getReference(displayRow.pluginIndex);
    const auto identifier = desc.createIdentifierString();
    const int leftIndent = groupByVendor ? groupedLeafIndent : 0;

    if (displayRow.kind == DisplayRow::Kind::plugin)
    {
        const auto presetNames = presetsStore.getPresetNames(identifier);
        paintPluginRow(g, desc, favourites.isFavourite(identifier), ! presetNames.isEmpty(),
                      expandedPlugins.contains(identifier), rowIsSelected, leftIndent, width, height);
        return;
    }

    const auto presetNames = presetsStore.getPresetNames(identifier);
    if (juce::isPositiveAndBelow(displayRow.presetIndex, presetNames.size()))
        paintPresetRow(g, presetNames[displayRow.presetIndex], rowIsSelected, leftIndent + presetExtraIndent, width, height);
}

void PluginListModel::listBoxItemClicked(int row, const juce::MouseEvent& e)
{
    if (! enabled || ! juce::isPositiveAndBelow(row, displayRows.size()))
        return;

    const auto& displayRow = displayRows.getReference(row);

    if (displayRow.kind == DisplayRow::Kind::vendorHeader)
    {
        if (expandedVendors.contains(displayRow.vendorName))
            expandedVendors.removeString(displayRow.vendorName);
        else
            expandedVendors.add(displayRow.vendorName);

        rebuildDisplayRows();

        if (onGroupExpansionChanged)
            onGroupExpansionChanged();

        return;
    }

    // Leaf rows (plugin or preset) in grouped mode are drawn with an extra
    // left indent before their content (see paintPluginRow/paintPresetRow) --
    // the hit test must match that same offset, or clicks on the visible
    // triangle/star/delete-"x" miss and clicks on the blank indent strip
    // wrongly register.
    const int leftIndent = groupByVendor ? groupedLeafIndent : 0;

    if (displayRow.kind == DisplayRow::Kind::plugin)
    {
        const auto& desc = cachedTypes.getReference(displayRow.pluginIndex);
        const auto identifier = desc.createIdentifierString();

        // Not confined to one small glyph, same convention the preset row's
        // own right-click handling below already uses.
        if (e.mods.isPopupMenu())
        {
            showPluginContextMenu(identifier);
            return;
        }

        const bool hasPresets = ! presetsStore.getPresetNames(identifier).isEmpty();

        if (hasPresets && e.x >= leftIndent && e.x < leftIndent + disclosureColumnWidth)
        {
            if (expandedPlugins.contains(identifier))
                expandedPlugins.removeString(identifier);
            else
                expandedPlugins.add(identifier);

            rebuildDisplayRows();

            if (onGroupExpansionChanged)
                onGroupExpansionChanged();

            return;
        }

        const int starX = leftIndent + disclosureColumnWidth;
        if (e.x >= starX && e.x < starX + starColumnWidth)
        {
            favourites.setFavourite(identifier, ! favourites.isFavourite(identifier));
            applyFilter();

            if (onFavouritesChanged)
                onFavouritesChanged();
        }

        return;
    }

    // Preset row: the right-edge menu-trigger column opens the context menu
    // on a left click anywhere else on the row; a right-click (isPopupMenu())
    // opens it regardless of x position, matching the platform convention
    // that a context menu isn't confined to one small glyph.
    const auto& desc = cachedTypes.getReference(displayRow.pluginIndex);
    const auto identifier = desc.createIdentifierString();
    const auto presetNames = presetsStore.getPresetNames(identifier);

    if (! juce::isPositiveAndBelow(displayRow.presetIndex, presetNames.size()))
        return;

    if (e.mods.isPopupMenu())
    {
        showPresetContextMenu(identifier, presetNames[displayRow.presetIndex]);
        return;
    }

    const int rowWidth = e.eventComponent != nullptr ? e.eventComponent->getWidth() : 0;
    if (rowWidth > 0 && e.x >= rowWidth - presetMenuColumnWidth)
        showPresetContextMenu(identifier, presetNames[displayRow.presetIndex]);
}

void PluginListModel::showPluginContextMenu(const juce::String& pluginIdentifier)
{
    juce::PopupMenu menu;
    menu.addItem(1, favourites.isFavourite(pluginIdentifier) ? "Remove from Favourites" : "Add to Favourites");
    menu.addItem(2, "Hide Plugin");

    menu.showMenuAsync(juce::PopupMenu::Options(),
        [this, pluginIdentifier](int result)
        {
            if (result == 1)
            {
                favourites.setFavourite(pluginIdentifier, ! favourites.isFavourite(pluginIdentifier));
                applyFilter();

                if (onFavouritesChanged)
                    onFavouritesChanged();
            }
            else if (result == 2)
            {
                enablementStore.setEnabled(pluginIdentifier, false);

                // An explicit user toggle always counts as "a default has
                // been assigned", same rule PluginsSettingsTab's own checkbox
                // toggle follows -- so a later rescan's duplicate-seeding
                // logic never re-decides this plugin's enablement.
                enablementStore.markDefaultAssigned(pluginIdentifier);

                applyFilter();

                if (onPluginHidden)
                    onPluginHidden();
            }
        });
}

void PluginListModel::showPresetContextMenu(const juce::String& pluginIdentifier, const juce::String& presetName)
{
    juce::PopupMenu menu;
    menu.addItem(1, "Open");
    menu.addItem(2, "Rename...");
    menu.addItem(3, "Delete...");

    menu.showMenuAsync(juce::PopupMenu::Options(),
        [this, pluginIdentifier, presetName](int result)
        {
            if (result == 1)
            {
                // Re-resolve by stable identity rather than trusting the row
                // index captured when the menu opened -- showMenuAsync is
                // non-blocking, so a background plugin rescan or another
                // preset add/rename/delete can shift displayRows while this
                // menu is still open.
                if (auto freshRow = findPresetRow(pluginIdentifier, presetName))
                    if (onDoubleClick)
                        onDoubleClick(*freshRow);
            }
            else if (result == 2)
                promptAndRenamePreset(pluginIdentifier, presetName);
            else if (result == 3)
                confirmAndDeletePreset(pluginIdentifier, presetName);
        });
}

std::optional<int> PluginListModel::findPresetRow(const juce::String& pluginIdentifier, const juce::String& presetName) const
{
    for (int i = 0; i < displayRows.size(); ++i)
    {
        const auto& row = displayRows.getReference(i);
        if (row.kind != DisplayRow::Kind::preset || ! juce::isPositiveAndBelow(row.pluginIndex, cachedTypes.size()))
            continue;

        if (cachedTypes.getReference(row.pluginIndex).createIdentifierString() != pluginIdentifier)
            continue;

        const auto presetNames = presetsStore.getPresetNames(pluginIdentifier);
        if (juce::isPositiveAndBelow(row.presetIndex, presetNames.size()) && presetNames[row.presetIndex] == presetName)
            return i;
    }

    return std::nullopt;
}

void PluginListModel::promptAndRenamePreset(const juce::String& pluginIdentifier, const juce::String& presetName)
{
    auto* alertWindow = new juce::AlertWindow("Rename Preset",
                                               "Enter a new name for \"" + presetName + "\":",
                                               juce::MessageBoxIconType::NoIcon);
    alertWindow->addTextEditor("name", presetName, "Name:");
    alertWindow->addButton("Rename", 1, juce::KeyPress(juce::KeyPress::returnKey));
    alertWindow->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    alertWindow->enterModalState(true, juce::ModalCallbackFunction::create(
        [this, alertWindow, pluginIdentifier, presetName](int result)
        {
            const auto newName = alertWindow->getTextEditorContents("name").trim();

            if (result == 1 && newName.isNotEmpty() && newName != presetName)
            {
                presetsStore.renamePreset(pluginIdentifier, presetName, newName);
                rebuildDisplayRows();

                if (onGroupExpansionChanged)
                    onGroupExpansionChanged();
            }
        }),
        true); // deleteWhenDismissed
}

void PluginListModel::confirmAndDeletePreset(const juce::String& pluginIdentifier, const juce::String& presetName)
{
    auto options = juce::MessageBoxOptions::makeOptionsOkCancel(
        juce::MessageBoxIconType::WarningIcon, "Delete Preset",
        "Delete \"" + presetName + "\"? This cannot be undone.",
        "Delete", "Cancel");

    juce::AlertWindow::showAsync(options, [this, pluginIdentifier, presetName](int result)
    {
        // Empirically 1 == the first button ("Delete") and 0 == the second
        // ("Cancel") -- not the 1/0 convention documented on the older
        // showOkCancelBox().
        if (result != 1)
            return;

        presetsStore.deletePreset(pluginIdentifier, presetName);
        rebuildDisplayRows();

        if (onGroupExpansionChanged)
            onGroupExpansionChanged();
    });
}
