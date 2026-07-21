#include "PluginListModel.h"
#include "PixelBenderLookAndFeel.h"
#include <cmath>

// Shared by both the ungrouped path and the grouped-leaf-row path: draws the
// disclosure triangle (only if this plugin has presets), the favourite star,
// and the plugin's name/manufacturer/format text, starting at a
// caller-supplied left indent (0 for the flat list, a small extra indent for
// leaf rows nested under a vendor header).
void PluginListModel::paintPluginRow(juce::Graphics& g, const juce::PluginDescription& desc, bool isFavourite,
                                       bool hasPresets, bool presetsExpanded, int leftIndent, int width, int height)
{
    const auto& palette = PixelBenderLookAndFeel::Palette::get();

    if (hasPresets)
    {
        g.setColour(palette.textSecondary);
        g.drawText(presetsExpanded ? juce::CharPointer_UTF8("\xE2\x96\xBE") : juce::CharPointer_UTF8("\xE2\x96\xB8"),
                    leftIndent, 0, disclosureColumnWidth, height, juce::Justification::centred);
    }

    // The disclosure column is always reserved (even when blank, for a
    // plugin with no presets) so the star's x position never shifts
    // depending on whether this particular plugin happens to have any.
    const int starX = leftIndent + disclosureColumnWidth;

    g.setColour(isFavourite ? palette.gold : palette.textSecondary.withAlpha(0.6f));
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

    g.setColour(enabled ? palette.textPrimary : palette.textSecondary);
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

    g.setColour(palette.textSecondary.withAlpha(enabled ? 0.85f : 0.6f));
    g.drawText(suffix, textArea.getX() + (int) nameWidth, 0, textArea.getWidth() - (int) nameWidth, height,
                juce::Justification::centredLeft);
}

void PluginListModel::paintPresetRow(juce::Graphics& g, const juce::String& presetName, int leftIndent, int width, int height)
{
    const auto& palette = PixelBenderLookAndFeel::Palette::get();

    g.setColour(palette.textSecondary);
    g.drawText(presetName, leftIndent, 0, width - leftIndent - presetDeleteColumnWidth, height,
                juce::Justification::centredLeft);

    g.setColour(palette.textSecondary.withAlpha(0.7f));
    g.drawText(juce::CharPointer_UTF8("\xC3\x97"), width - presetDeleteColumnWidth, 0, presetDeleteColumnWidth, height,
                juce::Justification::centred);
}

void PluginListModel::paintListBoxItem(int rowNumber, juce::Graphics& g,
                                         int width, int height, bool rowIsSelected)
{
    const auto& palette = PixelBenderLookAndFeel::Palette::get();

    if (rowIsSelected)
        g.fillAll(palette.accent.withAlpha(0.22f));

    if (! juce::isPositiveAndBelow(rowNumber, displayRows.size()))
        return;

    const auto& displayRow = displayRows.getReference(rowNumber);

    if (displayRow.kind == DisplayRow::Kind::vendorHeader)
    {
        g.fillAll(palette.surfaceRaised);

        const bool isExpanded = expandedVendors.contains(displayRow.vendorName);

        int matchingCount = 0;
        for (const auto& desc : cachedTypes)
            if (desc.manufacturerName == displayRow.vendorName)
                ++matchingCount;

        g.setColour(palette.textPrimary);
        g.drawText(isExpanded ? juce::CharPointer_UTF8("\xE2\x96\xBE") : juce::CharPointer_UTF8("\xE2\x96\xB8"),
                    4, 0, starColumnWidth, height, juce::Justification::centred);
        g.setFont(juce::Font(juce::FontOptions(13.0f).withStyle("Bold")));
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
                      expandedPlugins.contains(identifier), leftIndent, width, height);
        return;
    }

    const auto presetNames = presetsStore.getPresetNames(identifier);
    if (juce::isPositiveAndBelow(displayRow.presetIndex, presetNames.size()))
        paintPresetRow(g, presetNames[displayRow.presetIndex], leftIndent + presetExtraIndent, width, height);
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

    // Preset row: only the right-edge delete "x" column is clickable.
    const auto& desc = cachedTypes.getReference(displayRow.pluginIndex);
    const auto identifier = desc.createIdentifierString();
    const auto presetNames = presetsStore.getPresetNames(identifier);

    if (! juce::isPositiveAndBelow(displayRow.presetIndex, presetNames.size()))
        return;

    const int rowWidth = e.eventComponent != nullptr ? e.eventComponent->getWidth() : 0;
    if (rowWidth > 0 && e.x >= rowWidth - presetDeleteColumnWidth)
        confirmAndDeletePreset(identifier, presetNames[displayRow.presetIndex]);
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
        // ("Cancel") -- same button-index mapping
        // MainComponent::confirmDiscardChangesIfNeeded relies on, not the
        // 1/0 convention documented on the older showOkCancelBox().
        if (result != 1)
            return;

        presetsStore.deletePreset(pluginIdentifier, presetName);
        rebuildDisplayRows();

        if (onGroupExpansionChanged)
            onGroupExpansionChanged();
    });
}
