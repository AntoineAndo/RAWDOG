#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginEditorPanel.h"

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
