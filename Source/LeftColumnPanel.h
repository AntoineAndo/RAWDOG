#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "EffectChainPanel.h"
#include "GrippedResizerBar.h"
#include "RawdogLookAndFeel.h"
#include "PluginEditorPanel.h"

// Top-level content of the left column: an "Effect Chain"/"Plugins" tab
// switch. "Plugins" holds the search box, All/Favourites/By Vendor filter
// tabs, the plugin list, and -- while a session is open -- the
// currently-selected slot's PluginEditorPanel, split from the rack by a
// user-draggable divider.
//
// Opens on "Effect Chain" by default (see the SessionRail ctor) -- that's
// what a fresh session starts on, showing just the "+ Add effect"
// placeholder until the user switches to "Plugins" to pick one.
//
// MainComponent owns pluginEditorPanel via unique_ptr -- this class only
// holds a non-owning pointer for layout/parenting purposes, set via
// setEditorPanel().
class LeftColumnPanel : public juce::Component
{
public:
    // Not the same as PluginFilterTabs below (All/Favourites/By Vendor),
    // which only filters the browser once you're already on the Plugins tab.
    //
    // Not a juce::TabbedButtonBar (which only knows how to lay tabs out
    // horizontally) -- a narrow 26px rail with two stacked, hand-painted tab
    // regions, each drawing its label rotated 90 degrees so "EFFECT CHAIN"/
    // "PLUGINS" reads bottom-to-top in the vertical strip, matching the
    // design mockup's `writing-mode: vertical-rl; transform: rotate(180deg)`.
    // Spans the panel's full content height (not just a top strip) -- see
    // resized() below, which carves this off the left edge before anything
    // else.
    class SessionRail : public juce::Component
    {
    public:
        SessionRail()
        {
            chainTab.label = "EFFECT CHAIN";
            chainTab.active = true;
            chainTab.onClick = [this] { setCurrentTabIndex(0); };
            addAndMakeVisible(chainTab);

            pluginsTab.label = "PLUGINS";
            pluginsTab.onClick = [this] { setCurrentTabIndex(1); };
            addAndMakeVisible(pluginsTab);
        }

        int getCurrentTabIndex() const { return currentIndex; }

        void setCurrentTabIndex(int index)
        {
            if (index == currentIndex)
                return;

            currentIndex = index;
            chainTab.active = (index == 0);
            pluginsTab.active = (index == 1);
            chainTab.repaint();
            pluginsTab.repaint();

            if (onTabChanged)
                onTabChanged(index);
        }

        std::function<void(int)> onTabChanged;

        void paint(juce::Graphics& g) override
        {
            // The tabs are sized to their own content (see resized()), not
            // stretched to fill the whole rail, so this fill covers the
            // remainder below them, matching the mockup's rail background
            // showing through past the tab boxes.
            g.fillAll(RawdogLookAndFeel::Palette::get().windowBg);
        }

        void resized() override
        {
            auto area = getLocalBounds();
            chainTab.setBounds(area.removeFromTop(chainTab.getPreferredExtent()));
            area.removeFromTop(4); // gap, matching the mockup's `gap:4px` between rail tabs
            pluginsTab.setBounds(area.removeFromTop(pluginsTab.getPreferredExtent()));
        }

    private:
        // One stacked region of the rail -- painted, not a juce::Button,
        // since a button's LookAndFeel draws horizontal chrome we'd have to
        // fight to get the rotated-text look below.
        struct RailTab : public juce::Component
        {
            RailTab() { setMouseCursor(juce::MouseCursor::PointingHandCursor); }

            // The tab's extent along the rail's vertical axis -- since the
            // label is rotated 90 degrees, that's the text's own (horizontal,
            // pre-rotation) width plus padding, not a fixed/stretched share of
            // the rail's height.
            int getPreferredExtent() const
            {
                const auto font = RawdogLookAndFeel::chromeFont(9.0f);
                return (int) juce::GlyphArrangement::getStringWidth(font, label) + 28;
            }

            void mouseUp(const juce::MouseEvent& e) override
            {
                if (contains(e.getPosition()) && onClick)
                    onClick();
            }

            void paint(juce::Graphics& g) override
            {
                const auto& palette = RawdogLookAndFeel::Palette::get();
                auto bounds = getLocalBounds().toFloat();

                // Active state reads as a page switch (like the Settings
                // window's own tabs), not a list-row selection -- using
                // selectedBg/selectedFg here would light up as a jarring
                // near-white box in dark mode, since selectedBg is
                // deliberately inverted-light there for list selections.
                g.setColour(active ? palette.divider : palette.windowBg);
                g.fillRect(bounds);
                g.setColour(palette.ink);
                g.drawRect(bounds, 1.0f);

                if (! active)
                {
                    // Inset highlight bevel, drawn only for the inactive
                    // state -- brightened relative to windowBg rather than a
                    // hardcoded white, so it still reads as a highlight (not
                    // a bright streak) against the dark palette's much
                    // darker fill.
                    auto inner = bounds.reduced(1.0f);
                    g.setColour(palette.windowBg.brighter(0.6f).withAlpha(0.9f));
                    g.drawLine(inner.getX(), inner.getY(), inner.getRight(), inner.getY(), 1.0f);
                    g.drawLine(inner.getX(), inner.getY(), inner.getX(), inner.getBottom(), 1.0f);
                }

                // Rotate the drawing context -90 degrees about this region's
                // own centre, then draw the label into a width/height-swapped
                // rectangle centred on the same point -- the inverse rotation
                // needed so the rotated text still lands exactly within
                // `bounds` on screen, reading bottom-to-top.
                juce::Graphics::ScopedSaveState saveState(g);
                g.addTransform(juce::AffineTransform::rotation(-juce::MathConstants<float>::halfPi,
                                                                bounds.getCentreX(), bounds.getCentreY()));
                auto rotatedBounds = juce::Rectangle<float>(0.0f, 0.0f, bounds.getHeight(), bounds.getWidth())
                                         .withCentre(bounds.getCentre());

                // ink reads correctly against both the active (divider) and
                // inactive (windowBg) fills above, in either theme -- unlike
                // selectedFg, which only pairs correctly with selectedBg.
                g.setColour(palette.ink);
                g.setFont(RawdogLookAndFeel::chromeFont(9.0f));
                g.drawText(label, rotatedBounds.toNearestInt(), juce::Justification::centred);
            }

            juce::String label;
            bool active = false;
            std::function<void()> onClick;
        };

        RailTab chainTab, pluginsTab;
        int currentIndex = 0;
    };

    // Tiny standalone TabbedButtonBar (not the heavier TabbedComponent, which
    // manages separate content pages we don't need - we're filtering one shared
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

    // Rounded search field: a magnifying-glass glyph and a clear ("x") button
    // (shown only once text is entered) drawn directly around a borderless
    // juce::TextEditor, all inside one shared rounded pill background painted
    // here, so the icon, text entry, and clear button read as a single
    // control.
    class SearchField : public juce::Component
    {
    public:
        SearchField()
        {
            addAndMakeVisible(editor);
            addAndMakeVisible(clearButton);
            clearButton.setVisible(false);

            editor.setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
            editor.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
            editor.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
            setTextToShowWhenEmpty("Search plugins...");
            editor.onFocusChanged = [this] { repaint(); };

            editor.onTextChange = [this]
            {
                const bool hasText = editor.getText().isNotEmpty();
                if (clearButton.isVisible() != hasText)
                {
                    clearButton.setVisible(hasText);
                    resized();
                }

                if (onTextChange)
                    onTextChange(editor.getText());
            };

            clearButton.onClick = [this]
            {
                // Not editor.clear() -- unlike setText(), clear() never fires
                // textChanged()/onTextChange, so the filtered list this field
                // drives would keep showing stale results from the last
                // query until some other keystroke happened to trigger a real
                // textChanged() call.
                editor.setText({}, true);
                editor.grabKeyboardFocus();
            };
        }

        // Always styled in the palette's inkMuted colour -- stores text so
        // lookAndFeelChanged() below can reapply it in whichever colour the
        // active palette now uses.
        void setTextToShowWhenEmpty(const juce::String& text)
        {
            placeholderText = text;
            editor.setTextToShowWhenEmpty(placeholderText, RawdogLookAndFeel::Palette::get().inkMuted);
        }

        void lookAndFeelChanged() override
        {
            if (placeholderText.isNotEmpty())
                editor.setTextToShowWhenEmpty(placeholderText, RawdogLookAndFeel::Palette::get().inkMuted);
        }

        juce::String getText() const { return editor.getText(); }

        std::function<void(const juce::String&)> onTextChange;

        void resized() override
        {
            auto area = getLocalBounds();
            iconArea = area.removeFromLeft(24);

            if (clearButton.isVisible())
                clearButton.setBounds(area.removeFromRight(22));

            editor.setBounds(area);
        }

        void paint(juce::Graphics& g) override
        {
            const auto& palette = RawdogLookAndFeel::Palette::get();
            const float borderWeight = editor.hasKeyboardFocus(true) ? 2.0f : 1.0f;

            auto fullBounds = getLocalBounds().toFloat();
            // Inset by half the border's own weight (not a fixed 0.5px) so
            // the focused (2px) border stays centred on the component edge
            // instead of overhanging it unevenly on one side.
            auto bounds = fullBounds.reduced(borderWeight * 0.5f);

            g.setColour(palette.surface);
            g.fillRect(fullBounds);
            g.setColour(palette.ink);
            // Double-weight border on focus is this field's only focus
            // affordance, since the wrapped editor's own outline colours are
            // set transparent (this outer box owns the border instead).
            g.drawRect(bounds, borderWeight);

            // The letter "Q" stands in as the search icon, per the Platinum
            // mockup's search field treatment.
            g.setColour(palette.inkMuted);
            g.setFont(RawdogLookAndFeel::chromeFont(11.0f));
            g.drawText("Q", iconArea, juce::Justification::centred);
        }

    private:
        // Only override needed to notice focus gained -- juce::TextEditor
        // itself exposes onFocusLost but no focus-gained callback.
        class FocusNotifyingEditor : public juce::TextEditor
        {
        public:
            std::function<void()> onFocusChanged;

        private:
            void focusGained(juce::Component::FocusChangeType) override { if (onFocusChanged) onFocusChanged(); }
            void focusLost(juce::Component::FocusChangeType type) override
            {
                juce::TextEditor::focusLost(type);
                if (onFocusChanged) onFocusChanged();
            }
        };

        // Small hand-drawn "x", not a juce::TextButton -- avoids the
        // LookAndFeel's rounded button box competing with this field's own
        // single shared pill background.
        class ClearButton : public juce::Component
        {
        public:
            std::function<void()> onClick;

            void paint(juce::Graphics& g) override
            {
                const auto& palette = RawdogLookAndFeel::Palette::get();
                auto bounds = getLocalBounds().toFloat().reduced(6.0f);
                g.setColour(isMouseOver() ? palette.ink : palette.inkMuted);
                g.drawLine({ bounds.getTopLeft(), bounds.getBottomRight() }, 1.5f);
                g.drawLine({ bounds.getTopRight(), bounds.getBottomLeft() }, 1.5f);
            }

            void mouseEnter(const juce::MouseEvent&) override { repaint(); }
            void mouseExit(const juce::MouseEvent&) override { repaint(); }

            void mouseUp(const juce::MouseEvent& e) override
            {
                if (contains(e.getPosition()) && onClick)
                    onClick();
            }
        };

        juce::Rectangle<int> iconArea;
        FocusNotifyingEditor editor;
        ClearButton clearButton;
        juce::String placeholderText;
    };

    LeftColumnPanel(juce::ListBox& listBoxIn, EffectChainPanel& effectChainPanelIn)
        : listBox(listBoxIn), effectChainPanelRef(effectChainPanelIn)
    {
        addAndMakeVisible(sessionRail);
        sessionRail.onTabChanged = [this](int) { updateSessionTabVisibility(); };

        addAndMakeVisible(effectChainPanelRef);
        addAndMakeVisible(listBox);
        addAndMakeVisible(resizerBar);
        addAndMakeVisible(filterTabs);
        addAndMakeVisible(searchBox);

        searchBox.onTextChange = [this](const juce::String& text) { if (onSearchChanged) onSearchChanged(text); };
        filterTabs.onTabChanged = [this](int newIndex) { if (onTabChanged) onTabChanged(newIndex); };

        // Seeded once here; only re-seeded (item 2, the panel) when a genuinely
        // new/different editor panel is set - see setEditorPanel() - never on
        // every resized()/layOutComponents() call, so user drag adjustments
        // survive ordinary window resizes.
        layout.setItemLayout(0, 80, -0.7, 220);   // rack: min 80px, max 70%, preferred 220px
        layout.setItemLayout(1, 8, 8, 8);          // resizer bar: fixed 8px
        layout.setItemLayout(2, 120, -1.0, -1.0);  // panel: min 120px, fills remainder

        updateSessionTabVisibility();
    }

    void setListControlsEnabled(bool shouldBeEnabled)
    {
        filterTabs.setEnabled(shouldBeEnabled);
        searchBox.setEnabled(shouldBeEnabled);
    }

    std::function<void(int)> onTabChanged;
    std::function<void(const juce::String&)> onSearchChanged;

    // Programmatic tab switches, both one-way trips triggered by a specific
    // user action rather than a direct tab click: showPluginsTab() is wired
    // to EffectChainPanel::onAddEffectClicked (the "+ Add effect" placeholder),
    // showEffectChainTab() is called by MainComponent::addPluginToChain()
    // right after a plugin is actually added, so the user lands back on the
    // rack and immediately sees it there instead of staying on the browser.
    void showEffectChainTab() { sessionRail.setCurrentTabIndex(0); }
    void showPluginsTab() { sessionRail.setCurrentTabIndex(1); }

    // nullptr clears the panel (the rack then fills the whole Effect Chain
    // page). A genuinely new/different panel re-seeds the panel's preferred
    // size from its natural editor width;
    // re-setting the same panel pointer (e.g. a redundant call) does not
    // re-seed, so a user's drag adjustment isn't reset.
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
            resizerBar.toFront(false);

            // Newly-opened (genuinely different) panel: re-seed just this item's
            // preferred size from the plugin's natural editor width, so each new
            // plugin gets a sensible starting split. Deliberately NOT done on every
            // resized()/layOutComponents() call - that would reset user drag
            // adjustments on every window resize.
            if (auto* editorPanel = dynamic_cast<PluginEditorPanel*>(currentPanel))
                layout.setItemLayout(2, 120, -1.0, (double) editorPanel->getPreferredWidth());
        }

        // Reconciles resizerBar/currentPanel visibility against whichever
        // session tab is actually active, rather than forcing them visible
        // here regardless -- setEditorPanel() can run while the Plugins tab
        // is showing (addPluginToChain() switches back to Effect Chain right
        // after, but hasn't yet at this point in the call).
        updateSessionTabVisibility();
    }

    void resized() override
    {
        // Padding lives on the panel as a whole -- inset once here so every
        // child shares the same margin from the window frame, rather than
        // each one carving out its own.
        constexpr int margin = 8;
        auto area = getLocalBounds().reduced(margin, margin);

        // The rail spans the FULL remaining height (chain/plugins content
        // plus, when open, the editor panel below it) -- carved off the left
        // edge first, before anything else in this layout.
        sessionRail.setBounds(area.removeFromLeft(26));
        area.removeFromLeft(4);

        if (sessionRail.getCurrentTabIndex() == 0)
        {
            if (currentPanel == nullptr)
            {
                effectChainPanelRef.setBounds(area);
                return;
            }

            juce::Component* items[] = { &effectChainPanelRef, &resizerBar, currentPanel };
            layout.layOutComponents(items, 3, area.getX(), area.getY(),
                                     area.getWidth(), area.getHeight(),
                                     true /*stacked vertically*/, true /*resizeOtherDimension*/);

            // Stretch the bar's tint out to the panel's true right edge, closing
            // the notch where it would otherwise stop `margin` short of the
            // outer vertical resizer bar it meets there.
            resizerBar.setBounds(resizerBar.getBounds().withRight(getWidth()));
        }
        else
        {
            auto tabsArea = area.removeFromTop(28);
            area.removeFromTop(4);
            filterTabs.setBounds(tabsArea);

            auto searchArea = area.removeFromTop(28);
            area.removeFromTop(4);
            searchBox.setBounds(searchArea);

            listBox.setBounds(area);
        }
    }

private:
    // Single place reconciling which page's children are visible against
    // sessionRail's current selection -- called on every tab switch and from
    // setEditorPanel(), so the two can never disagree about what should be
    // on screen.
    void updateSessionTabVisibility()
    {
        const bool showEffectChain = sessionRail.getCurrentTabIndex() == 0;

        effectChainPanelRef.setVisible(showEffectChain);
        resizerBar.setVisible(showEffectChain && currentPanel != nullptr);
        if (currentPanel != nullptr)
            currentPanel->setVisible(showEffectChain);

        filterTabs.setVisible(! showEffectChain);
        searchBox.setVisible(! showEffectChain);
        listBox.setVisible(! showEffectChain);

        resized();
    }

    juce::ListBox& listBox;
    EffectChainPanel& effectChainPanelRef;
    SessionRail sessionRail;
    PluginFilterTabs filterTabs;
    SearchField searchBox;
    juce::Component* currentPanel = nullptr;
    juce::StretchableLayoutManager layout;
    GrippedResizerBar resizerBar { &layout, 1, false /*horizontal bar, dragged up/down*/ };
};
