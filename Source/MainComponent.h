#pragma once

#include <map>
#include <juce_gui_extra/juce_gui_extra.h>
#include "FavouritePluginsStore.h"
#include "PluginParameterWatcher.h"
#include "PluginScanner.h"
#include "RawImage.h"
#include "WaveformView.h"
#include "ZoomableImageView.h"

class MainComponent : public juce::Component,
                      private juce::ScrollBar::Listener,
                      public juce::MenuBarModel,
                      public juce::ApplicationCommandTarget
{
public:
    MainComponent();
    ~MainComponent() override;

    void resized() override;

    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex(int topLevelMenuIndex, const juce::String& menuName) override;
    void menuItemSelected(int menuItemID, int topLevelMenuIndex) override;

    ApplicationCommandTarget* getNextCommandTarget() override;
    void getAllCommands(juce::Array<juce::CommandID>& commands) override;
    void getCommandInfo(juce::CommandID commandID, juce::ApplicationCommandInfo& result) override;
    bool perform(const InvocationInfo& info) override;

private:
    enum MenuItemIDs
    {
        loadImageMenuItem = 1,
        exportImageMenuItem,
        resetMenuItem,
        rescanPluginsMenuItem
    };

    enum CommandIDs
    {
        undoCommand = 1000,
        redoCommand
    };

    void refreshPluginList();
    void loadImageClicked();
    void exportImageClicked();
    void loadAndOpenPlugin(int row);
    void openEditorClicked();
    void applyClicked();
    void cancelEditorClicked();
    void resetClicked();
    void undoClicked();
    void redoClicked();
    void pushUndoState();
    void updatePreview(bool resetView = false);
    void updateWaveform(bool resetView = false);
    void updatePluginListEnablement();
    void syncScrollBarToView();
    void setStatus(const juce::String& text);

    juce::MemoryBlock computeProcessedPixelBytes(juce::AudioPluginInstance& plugin,
                                                  const juce::Range<int>& selection);
    void refreshLivePreview();
    void endLivePreviewSession(bool commitToWorkingImage);

    void scrollBarMoved(juce::ScrollBar* scrollBarThatHasMoved, double newRangeStart) override;

    static constexpr double sampleRate = 44100.0;
    static constexpr int blockSize = 512;

    PluginScanner scanner;

    juce::Slider waveformZoomSlider { juce::Slider::LinearVertical, juce::Slider::NoTextBox };
    juce::Label waveformZoomLabel { {}, "Zoom" };
    juce::Slider horizontalZoomSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::ScrollBar horizontalScrollBar { false };

    ZoomableImageView imagePreview;
    WaveformView waveformView;
    juce::ListBox pluginListBox;
    juce::Label statusLabel;

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
        // paintListBoxItem (defined in MainComponent.cpp).
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

    FavouritePluginsStore favouritePluginsStore;
    PluginListModel listModel { scanner.getKnownPluginList(), favouritePluginsStore };

    // Embeds a plugin's editor in-place (instead of a separate OS popup window),
    // wrapped in a Viewport so any editor size/aspect ratio scrolls cleanly rather
    // than clipping or being force-resized, with an "Apply" button docked underneath.
    class PluginEditorPanel : public juce::Component
    {
    public:
        PluginEditorPanel(std::unique_ptr<juce::AudioProcessorEditor> editorIn, std::function<void()> onApply,
                          std::function<void()> onCancel)
            : editor(std::move(editorIn))
        {
            viewport.setViewedComponent(editor.get(), false); // false: editor stays owned by our unique_ptr
            viewport.setScrollBarsShown(true, true); // explicit: show scrollbars whenever the editor's own
                                                      // (unforced) size exceeds the viewport, rather than
                                                      // relying on Viewport's implicit default policy.
            addAndMakeVisible(viewport);
            addAndMakeVisible(cancelButton);
            addAndMakeVisible(applyButton);
            cancelButton.onClick = std::move(onCancel);
            applyButton.onClick = std::move(onApply);
        }

        int getPreferredWidth() const { return editor->getWidth(); }

        void resized() override
        {
            auto area = getLocalBounds();
            auto buttonStrip = area.removeFromBottom(40).reduced(8);
            applyButton.setBounds(buttonStrip.removeFromRight(buttonStrip.getWidth() / 2).reduced(4, 0));
            cancelButton.setBounds(buttonStrip.reduced(4, 0));
            viewport.setBounds(area);
        }

    private:
        std::unique_ptr<juce::AudioProcessorEditor> editor;
        juce::Viewport viewport;
        juce::TextButton cancelButton { "Cancel" };
        juce::TextButton applyButton { "Apply" };
    };

    // Parents the plugin list and (optionally) the currently-open PluginEditorPanel,
    // split by a user-draggable horizontal divider. MainComponent still owns
    // pluginEditorPanel via unique_ptr — this class only holds a non-owning pointer
    // for layout/parenting purposes, set via setEditorPanel().
    class LeftColumnPanel : public juce::Component
    {
    public:
        // Tiny standalone TabbedButtonBar (not the heavier TabbedComponent, which
        // manages separate content pages we don't need — we're filtering one shared
        // ListBox, not swapping pages) offering "All"/"Favourites" tabs.
        class PluginFilterTabs : public juce::TabbedButtonBar
        {
        public:
            PluginFilterTabs() : TabbedButtonBar(TabsAtTop)
            {
                addTab("All", juce::Colours::transparentBlack, 0);
                addTab("Favourites", juce::Colours::transparentBlack, 1);
                addTab("By Vendor", juce::Colours::transparentBlack, 2);
            }

            std::function<void(int)> onTabChanged;

        private:
            void currentTabChanged(int newIndex, const juce::String&) override
            {
                if (onTabChanged)
                    onTabChanged(newIndex);
            }
        };

        explicit LeftColumnPanel(juce::ListBox& listBoxIn) : listBox(listBoxIn)
        {
            addAndMakeVisible(listBox);
            addAndMakeVisible(resizerBar);
            addAndMakeVisible(filterTabs);
            addAndMakeVisible(searchBox);

            searchBox.setTextToShowWhenEmpty("Search plugins...", juce::Colours::grey);
            searchBox.onTextChange = [this] { if (onSearchChanged) onSearchChanged(searchBox.getText()); };
            filterTabs.onTabChanged = [this](int newIndex) { if (onTabChanged) onTabChanged(newIndex); };

            // Seeded once here; only re-seeded (item 2, the panel) when a genuinely
            // new/different editor panel is set — see setEditorPanel() — never on
            // every resized()/layOutComponents() call, so user drag adjustments
            // survive ordinary window resizes.
            layout.setItemLayout(0, 80, -0.7, 220);   // list: min 80px, max 70%, preferred 220px (today's value)
            layout.setItemLayout(1, 8, 8, 8);          // resizer bar: fixed 8px
            layout.setItemLayout(2, 120, -1.0, -1.0);  // panel: min 120px, fills remainder
        }

        void setListControlsEnabled(bool shouldBeEnabled)
        {
            filterTabs.setEnabled(shouldBeEnabled);
            searchBox.setEnabled(shouldBeEnabled);
        }

        std::function<void(int)> onTabChanged;
        std::function<void(const juce::String&)> onSearchChanged;

        // nullptr clears the panel (list fills the whole column, matching today's
        // no-panel behavior). A genuinely new/different panel re-seeds the panel's
        // preferred size from its natural editor width; re-setting the same panel
        // pointer (e.g. a redundant call) does not re-seed, so a user's drag
        // adjustment isn't reset.
        void setEditorPanel(juce::Component* panel)
        {
            if (panel == currentPanel)
                return;

            if (currentPanel != nullptr)
                removeChildComponent(currentPanel);

            currentPanel = panel;

            if (currentPanel != nullptr)
            {
                addAndMakeVisible(*currentPanel);
                resizerBar.setVisible(true);

                // Newly-opened (genuinely different) panel: re-seed just this item's
                // preferred size from the plugin's natural editor width, so each new
                // plugin gets a sensible starting split. Deliberately NOT done on every
                // resized()/layOutComponents() call — that would reset user drag
                // adjustments on every window resize.
                if (auto* editorPanel = dynamic_cast<PluginEditorPanel*>(currentPanel))
                    layout.setItemLayout(2, 120, -1.0, (double) editorPanel->getPreferredWidth());
            }
            else
            {
                resizerBar.setVisible(false);
            }

            resized();
        }

        void resized() override
        {
            auto area = getLocalBounds();

            auto tabsArea = area.removeFromTop(28);
            area.removeFromTop(4);
            filterTabs.setBounds(tabsArea);

            auto searchArea = area.removeFromTop(28);
            area.removeFromTop(4);
            searchBox.setBounds(searchArea);

            if (currentPanel == nullptr)
            {
                listBox.setBounds(area);
                return;
            }

            juce::Component* items[] = { &listBox, &resizerBar, currentPanel };
            layout.layOutComponents(items, 3, area.getX(), area.getY(),
                                     area.getWidth(), area.getHeight(),
                                     true /*stacked vertically*/, true /*resizeOtherDimension*/);
        }

    private:
        juce::ListBox& listBox;
        PluginFilterTabs filterTabs;
        juce::TextEditor searchBox;
        juce::Component* currentPanel = nullptr;
        juce::StretchableLayoutManager layout;
        juce::StretchableLayoutResizerBar resizerBar { &layout, 1, false /*horizontal bar, dragged up/down*/ };
    };

    // Ports today's fixed-pixel waveform sub-layout (waveform view + vertical zoom
    // column + horizontal zoom/scrollbar row) into its own container, unchanged.
    // Not a user-resizable split, just scoped to this panel's local coordinates.
    class WaveformSectionPanel : public juce::Component
    {
    public:
        WaveformSectionPanel(juce::Component& waveformViewIn, juce::Slider& waveformZoomSliderIn,
                             juce::Label& waveformZoomLabelIn, juce::Slider& horizontalZoomSliderIn,
                             juce::Component& horizontalScrollBarIn)
            : waveformViewRef(waveformViewIn), waveformZoomSliderRef(waveformZoomSliderIn),
              waveformZoomLabelRef(waveformZoomLabelIn), horizontalZoomSliderRef(horizontalZoomSliderIn),
              horizontalScrollBarRef(horizontalScrollBarIn)
        {
            addAndMakeVisible(waveformViewRef);
            addAndMakeVisible(waveformZoomSliderRef);
            addAndMakeVisible(waveformZoomLabelRef);
            addAndMakeVisible(horizontalZoomSliderRef);
            addAndMakeVisible(horizontalScrollBarRef);
        }

        void resized() override
        {
            auto area = getLocalBounds();

            auto waveformTop = area.removeFromTop(100);
            area.removeFromTop(4);

            auto zoomArea = waveformTop.removeFromRight(40);
            waveformTop.removeFromRight(8);
            waveformZoomLabelRef.setBounds(zoomArea.removeFromTop(16));
            waveformZoomSliderRef.setBounds(zoomArea);
            waveformViewRef.setBounds(waveformTop);

            auto horizontalZoomArea = area.removeFromLeft(120);
            area.removeFromLeft(8);
            horizontalZoomSliderRef.setBounds(horizontalZoomArea);
            horizontalScrollBarRef.setBounds(area);
        }

    private:
        juce::Component& waveformViewRef;
        juce::Slider& waveformZoomSliderRef;
        juce::Label& waveformZoomLabelRef;
        juce::Slider& horizontalZoomSliderRef;
        juce::Component& horizontalScrollBarRef;
    };

    // Parents the image preview, a WaveformSectionPanel, and the status label.
    // Carves the fixed 24px status strip off the bottom first (unchanged from
    // today), then splits the remainder into preview/waveform via its own
    // user-draggable divider.
    class RightColumnPanel : public juce::Component
    {
    public:
        RightColumnPanel(juce::Component& imagePreviewIn, juce::Label& statusLabelIn,
                         juce::Component& waveformViewIn, juce::Slider& waveformZoomSliderIn,
                         juce::Label& waveformZoomLabelIn, juce::Slider& horizontalZoomSliderIn,
                         juce::Component& horizontalScrollBarIn)
            : imagePreviewRef(imagePreviewIn), statusLabelRef(statusLabelIn),
              waveformSection(waveformViewIn, waveformZoomSliderIn, waveformZoomLabelIn,
                              horizontalZoomSliderIn, horizontalScrollBarIn)
        {
            addAndMakeVisible(imagePreviewRef);
            addAndMakeVisible(waveformSection);
            addAndMakeVisible(resizerBar);
            addAndMakeVisible(statusLabelRef);

            layout.setItemLayout(0, 100, -1.0, -1.0); // preview: min 100px, fills remainder
            layout.setItemLayout(1, 8, 8, 8);          // resizer bar: fixed 8px
            layout.setItemLayout(2, 80, -0.6, 140);    // waveform section: min 80px, max 60%, preferred 140px
        }

        void resized() override
        {
            auto area = getLocalBounds();

            auto statusArea = area.removeFromBottom(24);
            area.removeFromBottom(8);
            statusLabelRef.setBounds(statusArea);

            juce::Component* items[] = { &imagePreviewRef, &resizerBar, &waveformSection };
            layout.layOutComponents(items, 3, area.getX(), area.getY(),
                                     area.getWidth(), area.getHeight(),
                                     true /*stacked vertically*/, true /*resizeOtherDimension*/);
        }

    private:
        juce::Component& imagePreviewRef;
        juce::Label& statusLabelRef;
        WaveformSectionPanel waveformSection;
        juce::StretchableLayoutManager layout;
        juce::StretchableLayoutResizerBar resizerBar { &layout, 1, false /*horizontal bar, dragged up/down*/ };
    };

    // Top-level user-resizable columns. Constructed after the raw controls above
    // (declaration order == construction order) so the references/pointers they
    // parent are already valid. MainComponent adds these two (not the raw controls
    // directly) as its own children; the outer split between them is handled by
    // outerLayout/outerResizerBar below.
    LeftColumnPanel leftColumn { pluginListBox };
    RightColumnPanel rightColumn { imagePreview, statusLabel, waveformView, waveformZoomSlider,
                                    waveformZoomLabel, horizontalZoomSlider, horizontalScrollBar };

    juce::StretchableLayoutManager outerLayout;
    juce::StretchableLayoutResizerBar outerResizerBar { &outerLayout, 1, true /*vertical bar, dragged left/right*/ };

    // Declared before currentPlugin so it destructs (and detaches) first —
    // member destruction order is the reverse of declaration order.
    PluginParameterWatcher pluginParamWatcher;

    std::unique_ptr<RawImage> originalImage;
    std::unique_ptr<RawImage> workingImage;
    std::unique_ptr<juce::AudioPluginInstance> currentPlugin;
    std::unique_ptr<PluginEditorPanel> pluginEditorPanel;
    std::unique_ptr<juce::FileChooser> fileChooser;

    // Caches the last live-preview result while pluginEditorPanel is open; Apply
    // reuses it directly instead of recomputing, so the committed bytes are
    // guaranteed identical to what was just previewed.
    juce::MemoryBlock livePreviewBytes;

    struct EditorSnapshot
    {
        juce::MemoryBlock pixelBytes;
        juce::Range<int> selection;
    };

    std::vector<EditorSnapshot> undoStack;
    std::vector<EditorSnapshot> redoStack;
    juce::ApplicationCommandManager commandManager;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
