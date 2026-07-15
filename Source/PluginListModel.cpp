#include "PluginListModel.h"

// Shared by both the ungrouped path and the grouped-leaf-row path: draws the
// favourite star plus the plugin's name/manufacturer/format text, starting at
// a caller-supplied left indent (0 for the flat list, a small extra indent for
// leaf rows nested under a vendor header).
void PluginListModel::paintPluginRow(juce::Graphics& g, const juce::PluginDescription& desc,
                                       bool isFavourite, int leftIndent, int width, int height)
{
    g.setColour(isFavourite ? juce::Colours::yellow : juce::Colours::grey);
    g.drawText(isFavourite ? juce::CharPointer_UTF8("\xE2\x98\x85") : juce::CharPointer_UTF8("\xE2\x98\x86"),
                leftIndent, 0, starColumnWidth, height, juce::Justification::centred);

    g.setColour(enabled ? juce::Colours::white : juce::Colours::grey);
    g.drawText(desc.name + "  —  " + desc.manufacturerName + "  (" + desc.pluginFormatName + ")",
                leftIndent + starColumnWidth + 4, 0, width - leftIndent - starColumnWidth - 8, height,
                juce::Justification::centredLeft);
}

void PluginListModel::paintListBoxItem(int rowNumber, juce::Graphics& g,
                                         int width, int height, bool rowIsSelected)
{
    if (rowIsSelected)
        g.fillAll(juce::Colours::lightblue);

    if (groupByVendor)
    {
        if (! juce::isPositiveAndBelow(rowNumber, displayRows.size()))
            return;

        const auto& displayRow = displayRows.getReference(rowNumber);

        if (displayRow.isHeader)
        {
            g.fillAll(juce::Colours::darkgrey.darker());

            const bool isExpanded = expandedVendors.contains(displayRow.vendorName);

            int matchingCount = 0;
            for (const auto& desc : cachedTypes)
                if (desc.manufacturerName == displayRow.vendorName)
                    ++matchingCount;

            g.setColour(juce::Colours::white);
            g.drawText(isExpanded ? juce::CharPointer_UTF8("\xE2\x96\xBE") : juce::CharPointer_UTF8("\xE2\x96\xB8"),
                        4, 0, starColumnWidth, height, juce::Justification::centred);
            g.drawText(displayRow.vendorName + " (" + juce::String(matchingCount) + ")",
                        starColumnWidth + 8, 0, width - starColumnWidth - 12, height, juce::Justification::centredLeft);
            return;
        }

        if (auto* desc = getType(rowNumber))
            paintPluginRow(g, *desc, favourites.isFavourite(desc->createIdentifierString()), groupedLeafIndent, width, height);

        return;
    }

    if (auto* desc = getType(rowNumber))
        paintPluginRow(g, *desc, favourites.isFavourite(desc->createIdentifierString()), 0, width, height);
}
