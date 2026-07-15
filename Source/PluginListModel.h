#pragma once

#include <map>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "FavouritePluginsStore.h"

class PluginListModel : public juce::ListBoxModel
{
public:
    PluginListModel(juce::KnownPluginList& list, FavouritePluginsStore& favouritesIn)
        : knownPluginList(list), favourites(favouritesIn) {}

    // Row layer used only when groupByVendor is active: either an
    // accordion header for a vendor, or a leaf pointing back into
    // cachedTypes.
    struct DisplayRow
    {
        bool isHeader = false;
        juce::String vendorName; // valid when isHeader
        int pluginIndex = -1;          // index into cachedTypes, valid when !isHeader
    };

    void refresh()
    {
        allTypes = knownPluginList.getTypes();

        // Additively seed newly-discovered vendors as expanded, without
        // clearing/resetting collapse choices the user already made for
        // vendors seen earlier in the session.
        for (const auto& desc : allTypes)
            if (! expandedVendors.contains(desc.manufacturerName))
                expandedVendors.add(desc.manufacturerName);

        applyFilter();
    }

    // Row index meaning depends on groupByVendor: when ungrouped, index
    // straight into cachedTypes; when grouped, index into displayRows — returns
    // nullptr for a header row (or an out-of-range row either way).
    // MainComponent::loadAndOpenPlugin() already guards on nullptr, so
    // double-clicking a header is a safe no-op with no extra guard needed there.
    const juce::PluginDescription* getType(int index) const
    {
        if (groupByVendor)
        {
            if (! juce::isPositiveAndBelow(index, displayRows.size()))
                return nullptr;

            const auto& displayRow = displayRows.getReference(index);
            if (displayRow.isHeader)
                return nullptr;

            return &cachedTypes.getReference(displayRow.pluginIndex);
        }

        return juce::isPositiveAndBelow(index, cachedTypes.size()) ? &cachedTypes.getReference(index) : nullptr;
    }

    int getNumRows() override { return groupByVendor ? displayRows.size() : cachedTypes.size(); }

    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;

    static constexpr int groupedLeafIndent = 16;

    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override
    {
        if (enabled && onDoubleClick != nullptr)
            onDoubleClick(row);
    }

    void listBoxItemClicked(int row, const juce::MouseEvent& e) override
    {
        if (! enabled)
            return;

        if (groupByVendor && juce::isPositiveAndBelow(row, displayRows.size())
            && displayRows.getReference(row).isHeader)
        {
            const auto& vendorName = displayRows.getReference(row).vendorName;

            if (expandedVendors.contains(vendorName))
                expandedVendors.removeString(vendorName);
            else
                expandedVendors.add(vendorName);

            rebuildDisplayRows();

            if (onGroupExpansionChanged)
                onGroupExpansionChanged();

            return;
        }

        // Leaf rows in grouped mode are drawn with an extra left indent before
        // the star column (see paintPluginRow/groupedLeafIndent) — the hit test
        // must match that same offset, or clicks on the visible star miss and
        // clicks on the blank indent strip wrongly register.
        const int leftIndent = groupByVendor ? groupedLeafIndent : 0;
        if (e.x < leftIndent || e.x >= leftIndent + starColumnWidth)
            return;

        if (auto* desc = getType(row))
        {
            const auto identifier = desc->createIdentifierString();
            favourites.setFavourite(identifier, ! favourites.isFavourite(identifier));
            applyFilter();

            if (onFavouritesChanged)
                onFavouritesChanged();
        }
    }

    void setEnabled(bool shouldBeEnabled) { enabled = shouldBeEnabled; }

    void setShowFavouritesOnly(bool shouldShowFavouritesOnly)
    {
        showFavouritesOnly = shouldShowFavouritesOnly;
        applyFilter();
    }

    void setSearchQuery(const juce::String& query)
    {
        searchQuery = query;
        applyFilter();
    }

    void setGroupByVendor(bool shouldGroupByVendor)
    {
        groupByVendor = shouldGroupByVendor;
        applyFilter();
    }

    static constexpr int starColumnWidth = 24;

    std::function<void(int)> onDoubleClick;
    std::function<void()> onFavouritesChanged;
    std::function<void()> onGroupExpansionChanged;

private:
    // Shared by both the ungrouped path and the grouped-leaf-row path in
    // paintListBoxItem (defined in PluginListModel.cpp).
    void paintPluginRow(juce::Graphics& g, const juce::PluginDescription& desc, bool isFavourite,
                        int leftIndent, int width, int height);

    void applyFilter()
    {
        cachedTypes.clear();

        for (const auto& desc : allTypes)
        {
            if (showFavouritesOnly && ! favourites.isFavourite(desc.createIdentifierString()))
                continue;

            if (searchQuery.isNotEmpty()
                && ! desc.name.containsIgnoreCase(searchQuery)
                && ! desc.manufacturerName.containsIgnoreCase(searchQuery)
                && ! desc.pluginFormatName.containsIgnoreCase(searchQuery))
                continue;

            cachedTypes.add(desc);
        }

        rebuildDisplayRows();
    }

    // Rebuilds displayRows strictly from cachedTypes (never from a separately
    // cached "all known vendors" list) — this is what makes a
    // vendor with zero matches under an active search query correctly
    // produce no header row, with no extra filtering logic needed: if none of
    // its plugins survived applyFilter() into cachedTypes, it never appears in
    // the map built below.
    void rebuildDisplayRows()
    {
        displayRows.clear();

        if (! groupByVendor)
            return;

        std::map<juce::String, juce::Array<int>> vendorToIndices;

        for (int i = 0; i < cachedTypes.size(); ++i)
            vendorToIndices[cachedTypes.getReference(i).manufacturerName].add(i);

        std::vector<juce::String> vendorNames;
        vendorNames.reserve(vendorToIndices.size());
        for (auto& entry : vendorToIndices)
            vendorNames.push_back(entry.first);

        std::sort(vendorNames.begin(), vendorNames.end(),
                   [](const juce::String& a, const juce::String& b)
                   { return a.compareIgnoreCase(b) < 0; });

        for (const auto& vendorName : vendorNames)
        {
            DisplayRow header;
            header.isHeader = true;
            header.vendorName = vendorName;
            displayRows.add(header);

            if (! expandedVendors.contains(vendorName))
                continue;

            auto& indices = vendorToIndices[vendorName];
            std::sort(indices.begin(), indices.end(),
                      [this](int a, int b)
                      { return cachedTypes.getReference(a).name.compareIgnoreCase(cachedTypes.getReference(b).name) < 0; });

            for (auto index : indices)
            {
                DisplayRow leaf;
                leaf.pluginIndex = index;
                displayRows.add(leaf);
            }
        }
    }

    juce::KnownPluginList& knownPluginList;
    FavouritePluginsStore& favourites;
    juce::Array<juce::PluginDescription> allTypes;
    juce::Array<juce::PluginDescription> cachedTypes;
    bool enabled = false;
    bool showFavouritesOnly = false;
    juce::String searchQuery;

    bool groupByVendor = false;
    juce::StringArray expandedVendors;
    juce::Array<DisplayRow> displayRows;
};
