#pragma once

#include <map>
#include <optional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "FavouritePluginsStore.h"
#include "PluginPresetsStore.h"

class PluginListModel : public juce::ListBoxModel
{
public:
    PluginListModel(juce::KnownPluginList& list, FavouritePluginsStore& favouritesIn, PluginPresetsStore& presetsIn)
        : knownPluginList(list), favourites(favouritesIn), presetsStore(presetsIn) {}

    // Every row -- vendor-group accordion header, plugin leaf, or preset leaf
    // nested under a plugin -- lives in one flat displayRows array regardless
    // of groupByVendor, so getNumRows()/getLoadTarget() never need a
    // flat-vs-grouped special case; only rebuildDisplayRows() (below)
    // branches on it.
    struct DisplayRow
    {
        enum class Kind { vendorHeader, plugin, preset };
        Kind kind = Kind::plugin;
        juce::String vendorName; // valid for vendorHeader
        int pluginIndex = -1;    // index into cachedTypes, valid for plugin & preset
        int presetIndex = -1;    // index into that plugin's getPresetNames(), valid for preset
    };

    void refresh()
    {
        allTypes = knownPluginList.getTypes();

        // Additively seed newly-discovered vendors as expanded, without
        // clearing/resetting collapse choices the user already made for
        // vendors seen earlier in the session. Deliberately NOT done for
        // expandedPlugins -- a plugin's preset list starts collapsed, only
        // ever expanded by an explicit click on its disclosure triangle.
        for (const auto& desc : allTypes)
            if (! expandedVendors.contains(desc.manufacturerName))
                expandedVendors.add(desc.manufacturerName);

        applyFilter();
    }

    // What double-clicking a row should load: the plugin to instantiate, and
    // (only for a preset row) the parameter state to apply right after.
    // Nullopt for a vendor header (nothing to load) or an out-of-range row.
    struct RowTarget
    {
        const juce::PluginDescription* description = nullptr;
        std::optional<juce::MemoryBlock> presetState;
    };

    std::optional<RowTarget> getLoadTarget(int index) const
    {
        if (! juce::isPositiveAndBelow(index, displayRows.size()))
            return std::nullopt;

        const auto& row = displayRows.getReference(index);
        if (row.kind == DisplayRow::Kind::vendorHeader)
            return std::nullopt;

        const auto& desc = cachedTypes.getReference(row.pluginIndex);

        if (row.kind == DisplayRow::Kind::preset)
        {
            const auto presetNames = presetsStore.getPresetNames(desc.createIdentifierString());
            if (! juce::isPositiveAndBelow(row.presetIndex, presetNames.size()))
                return std::nullopt;

            return RowTarget { &desc, presetsStore.getPresetState(desc.createIdentifierString(), presetNames[row.presetIndex]) };
        }

        return RowTarget { &desc, std::nullopt };
    }

    int getNumRows() override { return displayRows.size(); }

    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;

    static constexpr int groupedLeafIndent = 16;

    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override
    {
        if (enabled && onDoubleClick != nullptr)
            onDoubleClick(row);
    }

    void listBoxItemClicked(int row, const juce::MouseEvent& e) override;

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

    // Called by MainComponent after saving/deleting a preset (from the
    // plugin editor panel, outside this list) -- re-derives displayRows from
    // already-current state, no full refresh()/re-scan needed.
    void notifyPresetsChanged() { rebuildDisplayRows(); }

    static constexpr int starColumnWidth = 24;

    std::function<void(int)> onDoubleClick;
    std::function<void()> onFavouritesChanged;
    std::function<void()> onGroupExpansionChanged;

private:
    // Shared by both the ungrouped path and the grouped-leaf-row path in
    // paintListBoxItem (defined in PluginListModel.cpp). hasPresets/
    // presetsExpanded control the disclosure-triangle column, reserved at
    // leftIndent regardless of hasPresets so the star's x position never
    // shifts depending on whether this particular plugin has any presets.
    void paintPluginRow(juce::Graphics& g, const juce::PluginDescription& desc, bool isFavourite,
                        bool hasPresets, bool presetsExpanded, bool rowIsSelected, int leftIndent, int width, int height);

    void paintPresetRow(juce::Graphics& g, const juce::String& presetName, bool rowIsSelected, int leftIndent, int width, int height);

    // Shows a confirmation dialog (same AlertWindow+MessageBoxOptions
    // convention as MainComponent::confirmDiscardChangesIfNeeded) before
    // actually deleting -- deletion can't be undone, unlike a favourite toggle.
    void confirmAndDeletePreset(const juce::String& pluginIdentifier, const juce::String& presetName);

    // Popup shown by clicking a preset row's menu-trigger column: Open mirrors
    // double-click (via onDoubleClick), Delete defers to confirmAndDeletePreset
    // above, Rename opens promptAndRenamePreset below.
    void showPresetContextMenu(const juce::String& pluginIdentifier, const juce::String& presetName);

    // Heap-allocated juce::AlertWindow + addTextEditor, since there's no
    // existing text-entry dialog pattern anywhere else in this codebase.
    void promptAndRenamePreset(const juce::String& pluginIdentifier, const juce::String& presetName);

    // Re-resolves a preset's current row index by stable identity
    // (identifier + name) rather than trusting a row index captured earlier --
    // showMenuAsync is non-blocking, so displayRows can shift (rescan, another
    // preset add/rename/delete) while the context menu is still open.
    std::optional<int> findPresetRow(const juce::String& pluginIdentifier, const juce::String& presetName) const;

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
    // the map built below. Unconditional now (no longer skipped in flat mode)
    // since presets need to nest under a plugin row either way.
    void rebuildDisplayRows()
    {
        displayRows.clear();

        if (! groupByVendor)
        {
            for (int i = 0; i < cachedTypes.size(); ++i)
                addPluginAndPresetRows(i);

            return;
        }

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
            header.kind = DisplayRow::Kind::vendorHeader;
            header.vendorName = vendorName;
            displayRows.add(header);

            if (! expandedVendors.contains(vendorName))
                continue;

            auto& indices = vendorToIndices[vendorName];
            std::sort(indices.begin(), indices.end(),
                      [this](int a, int b)
                      { return cachedTypes.getReference(a).name.compareIgnoreCase(cachedTypes.getReference(b).name) < 0; });

            for (auto index : indices)
                addPluginAndPresetRows(index);
        }
    }

    // Appends the plugin leaf row for cachedTypes[pluginIndex], plus one
    // preset row per saved preset if that plugin is currently expanded --
    // shared by both the flat and by-vendor branches of rebuildDisplayRows().
    void addPluginAndPresetRows(int pluginIndex)
    {
        DisplayRow pluginRow;
        pluginRow.kind = DisplayRow::Kind::plugin;
        pluginRow.pluginIndex = pluginIndex;
        displayRows.add(pluginRow);

        const auto identifier = cachedTypes.getReference(pluginIndex).createIdentifierString();
        if (! expandedPlugins.contains(identifier))
            return;

        const auto presetNames = presetsStore.getPresetNames(identifier);
        for (int p = 0; p < presetNames.size(); ++p)
        {
            DisplayRow presetRow;
            presetRow.kind = DisplayRow::Kind::preset;
            presetRow.pluginIndex = pluginIndex;
            presetRow.presetIndex = p;
            displayRows.add(presetRow);
        }
    }

    juce::KnownPluginList& knownPluginList;
    FavouritePluginsStore& favourites;
    PluginPresetsStore& presetsStore;
    juce::Array<juce::PluginDescription> allTypes;
    juce::Array<juce::PluginDescription> cachedTypes;
    bool enabled = false;
    bool showFavouritesOnly = false;
    juce::String searchQuery;

    bool groupByVendor = false;
    juce::StringArray expandedVendors;

    // Which plugins (by identifier string) currently have their preset list
    // expanded -- starts empty; unlike expandedVendors, never auto-seeded,
    // since a plugin only becomes expandable once it has a saved preset.
    juce::StringArray expandedPlugins;

    juce::Array<DisplayRow> displayRows;

    static constexpr int disclosureColumnWidth = 16;
    static constexpr int presetExtraIndent = 24;   // beyond the parent plugin row's own leftIndent
    static constexpr int presetMenuColumnWidth = 20;
};
