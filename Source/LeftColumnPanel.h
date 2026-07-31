#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "GrippedResizerBar.h"
#include "RawdogLookAndFeel.h"
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

    // Rounded search field: a magnifying-glass glyph and a clear ("x") button
    // (shown only once text is entered) drawn directly around a borderless
    // juce::TextEditor, all inside one shared rounded pill background painted
    // here -- rather than the editor's own LookAndFeel-drawn box sitting next
    // to a separately-boxed icon, which read as two disconnected controls.
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
            editor.setTextToShowWhenEmpty("Search plugins...", RawdogLookAndFeel::Palette::get().inkMuted);
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
                editor.clear();
                editor.grabKeyboardFocus();
            };
        }

        void setTextToShowWhenEmpty(const juce::String& text, juce::Colour colour)
        {
            editor.setTextToShowWhenEmpty(text, colour);
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
            // Double-weight border on focus -- same emphasis motif as
            // RawdogLookAndFeel's "emphasized" buttons -- is this field's only
            // focus affordance, since the wrapped editor's own outline colours
            // are set transparent (this outer box owns the border instead).
            g.drawRect(bounds, borderWeight);

            // Plain "Q" glyph in place of a hand-drawn lens icon, per the
            // Platinum mockup's search field treatment.
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
    };

    explicit LeftColumnPanel(juce::ListBox& listBoxIn) : listBox(listBoxIn)
    {
        addAndMakeVisible(listBox);
        addAndMakeVisible(resizerBar);
        addAndMakeVisible(filterTabs);
        addAndMakeVisible(searchBox);

        searchBox.onTextChange = [this](const juce::String& text) { if (onSearchChanged) onSearchChanged(text); };
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
            resizerBar.toFront(false);

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
        // Padding lives on the panel as a whole -- inset once here so every
        // child (tabs, search field, list, and the editor panel when open)
        // shares the same margin from the window frame, rather than each one
        // carving out its own.
        constexpr int margin = 8;
        auto area = getLocalBounds().reduced(margin, margin);

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

        // Stretch the bar's tint out to the panel's true right edge, closing
        // the notch where it would otherwise stop `margin` short of the
        // outer vertical resizer bar it meets there.
        resizerBar.setBounds(resizerBar.getBounds().withRight(getWidth()));
    }

private:
    juce::ListBox& listBox;
    PluginFilterTabs filterTabs;
    SearchField searchBox;
    juce::Component* currentPanel = nullptr;
    juce::StretchableLayoutManager layout;
    GrippedResizerBar resizerBar { &layout, 1, false /*horizontal bar, dragged up/down*/ };
};
