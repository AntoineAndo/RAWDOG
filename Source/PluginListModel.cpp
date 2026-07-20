#include "PluginListModel.h"
#include "PixelBenderLookAndFeel.h"
#include <cmath>

// Shared by both the ungrouped path and the grouped-leaf-row path: draws the
// favourite star plus the plugin's name/manufacturer/format text, starting at
// a caller-supplied left indent (0 for the flat list, a small extra indent for
// leaf rows nested under a vendor header).
void PluginListModel::paintPluginRow(juce::Graphics& g, const juce::PluginDescription& desc,
                                       bool isFavourite, int leftIndent, int width, int height)
{
    const auto& palette = PixelBenderLookAndFeel::Palette::get();

    g.setColour(isFavourite ? palette.gold : palette.textSecondary.withAlpha(0.6f));
    g.drawText(isFavourite ? juce::CharPointer_UTF8("\xE2\x98\x85") : juce::CharPointer_UTF8("\xE2\x98\x86"),
                leftIndent, 0, starColumnWidth, height, juce::Justification::centred);

    // Name and vendor/format are drawn as two separate passes, not one string,
    // so the vendor/format suffix can be visually de-emphasized (dimmer colour)
    // relative to the name -- with dozens of plugins sharing the same vendor,
    // a uniform-weight "Name  —  Vendor  (Format)" on every row reads as a wall
    // of near-identical text; making the name the one bright element per row
    // is what actually needs scanning.
    const auto textArea = juce::Rectangle<int>(leftIndent + starColumnWidth + 4, 0,
                                                width - leftIndent - starColumnWidth - 8, height);
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

void PluginListModel::paintListBoxItem(int rowNumber, juce::Graphics& g,
                                         int width, int height, bool rowIsSelected)
{
    const auto& palette = PixelBenderLookAndFeel::Palette::get();

    if (rowIsSelected)
        g.fillAll(palette.accent.withAlpha(0.22f));

    if (groupByVendor)
    {
        if (! juce::isPositiveAndBelow(rowNumber, displayRows.size()))
            return;

        const auto& displayRow = displayRows.getReference(rowNumber);

        if (displayRow.isHeader)
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

        if (auto* desc = getType(rowNumber))
            paintPluginRow(g, *desc, favourites.isFavourite(desc->createIdentifierString()), groupedLeafIndent, width, height);

        return;
    }

    if (auto* desc = getType(rowNumber))
        paintPluginRow(g, *desc, favourites.isFavourite(desc->createIdentifierString()), 0, width, height);
}
