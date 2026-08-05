#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <functional>
#include <memory>
#include <set>
#include <vector>
#include "ChainSlot.h"
#include "ConditionalChainSlot.h"
#include "PixelCondition.h"
#include "RawdogLookAndFeel.h"

// The effect-chain "rack": a signal-path list read top to bottom -- an IN cap
// (source label), one horizontal-strip row per top-level chain entry with a
// connecting line between each adjacent pair, a trailing dashed "+ Add
// Effect" row and "+ Add Condition" row (neither a real chain entry -- the
// former switches to the plugin browser, the latter appends an empty
// conditional slot), and an OUT cap. A fixed "CHAIN" header above the
// scrolling list shows the slot count and the whole-chain commit action
// ("Apply").
//
// A top-level entry is either a plain plugin slot (ChainSlot) or a
// conditional slot (ConditionalChainSlot) that branches into two full
// sub-chains based on a per-pixel condition -- its row expands accordion-
// style (see ConditionalRowComponent below) to reveal both branches, each a
// nested mini-rack reusing the exact same row/connector/add-effect building
// blocks. Branches can only ever hold plain slots, never another conditional
// slot -- see ChainEntry's own comment for why that's impossible at the type
// level, not just a UI restriction.
//
// Reads top to bottom, in DSP order: chain index 0 (processed first) is the
// TOPMOST row, each later slot one position further down -- the IN/OUT caps
// make the signal direction explicit, so rows don't need their own direction
// glyph.
//
// Each row is click-to-select (swaps which slot's native editor is shown
// below it, in the same LeftColumnPanel), has a checkbox that bypasses it
// without removing it from the chain, a remove button, and a drag handle for
// reordering -- dragging a row to a new position fires onReorderSlot(from,
// to) (top level) or onReorderBranchSlot(topIndex, branch, from, to) (within
// a branch).
//
// Holds no chain state of its own -- MainComponent owns the real
// std::vector<ChainEntry> and calls rebuild() after every mutation (add,
// remove, reorder, select, bypass-toggle, condition/mode edit), fully
// re-deriving the row list from scratch on any change rather than trying to
// incrementally patch existing rows. The one piece of state this panel does
// own is which conditional slots are currently expanded -- purely
// presentational, so it lives in Content (see expandedConditionalSlots
// below), the same way PluginListModel keeps its own vendor/preset
// expansion state rather than pushing it into the persisted plugin list.
class EffectChainPanel : public juce::Component
{
public:
    EffectChainPanel()
    {
        viewport.setViewedComponent(&content, false);
        viewport.setScrollBarsShown(true, false); // vertical only -- this is a single column
        addAndMakeVisible(viewport);

        chainHeaderLabel.setText("CHAIN", juce::dontSendNotification);
        chainHeaderLabel.setFont(RawdogLookAndFeel::chromeFont(9.0f));
        addAndMakeVisible(chainHeaderLabel);

        chainCountLabel.setFont(RawdogLookAndFeel::chromeFont(9.0f));
        chainCountLabel.setColour(juce::Label::textColourId, RawdogLookAndFeel::Palette::get().inkMuted);
        addAndMakeVisible(chainCountLabel);

        // Docked in the header, not below the scrolling rack -- Apply acts on
        // the whole chain regardless of which slot (if any) is selected, so
        // it belongs beside the chain's own title/count, not on
        // PluginEditorPanel (which only ever represents one slot at a time).
        // Starts disabled; rebuild() below re-derives this from whether the
        // chain is actually non-empty. Deliberately no "Revert" button --
        // out of scope for this iteration.
        addAndMakeVisible(applyButton);
        applyButton.setEnabled(false);
        applyButton.setTooltip("Commit the whole effect chain's result to the image");
        applyButton.onClick = [this] { if (onApplyClicked) onApplyClicked(); };
        RawdogLookAndFeel::setEmphasized(applyButton);
    }

    // chainCountLabel's textColourId (set once in the constructor above) is
    // cached on the Label rather than looked up from the LookAndFeel per
    // paint, so a theme switch needs this reapplied explicitly.
    void lookAndFeelChanged() override
    {
        chainCountLabel.setColour(juce::Label::textColourId, RawdogLookAndFeel::Palette::get().inkMuted);
    }

    void rebuild(const std::vector<ChainEntry>& chain, std::optional<ChainPath> selected)
    {
        content.rebuild(chain, selected, onSelectSlot, onRemoveSlot, onToggleBypass, onReorderSlot,
                         onReorderBranchSlot, onAddEffectClicked, onAddConditionClicked, onConditionChanged,
                         onModeChanged, onExpansionChanged);

        chainCountLabel.setText(juce::String((int) chain.size()) + (chain.size() == 1 ? " effect" : " effects"),
                                 juce::dontSendNotification);
        applyButton.setEnabled(! chain.empty());
        resized();
        repaint();
    }

    // The IN cap's source label ("IN -- <name>") -- changes far less often
    // than the chain itself, so it's a separate setter rather than a rebuild()
    // parameter threaded through every call site.
    void setInputLabel(const juce::String& name) { content.setInputLabel(name); }

    // Dims the whole panel (see paintOverChildren()) -- matching the design
    // mockup's thoroughly washed-out empty state rather than relying on each
    // child component's own isEnabled()-based dimming to add up to the same
    // effect.
    void setHasImage(bool hasImage)
    {
        hasLoadedImage = hasImage;
        repaint();
    }

    // Each fired with the mutated/selected entry's path. MainComponent wires
    // these once in its constructor to selectChainSlot()/removeChainSlot()/
    // toggleChainSlotBypass(). onReorderSlot fires once per completed
    // top-level drag with the entry's original and final index, wired to
    // moveChainSlot(); onReorderBranchSlot is the branch-local analogue,
    // wired to moveBranchSlot(). onAddEffectClicked fires with nullopt for
    // the top-level "+ Add Effect" row, or a path naming a specific
    // conditional slot's branch when that branch's own "+ Add Effect" row was
    // clicked -- wired to set MainComponent::pendingInsertionTarget before
    // switching to the plugin browser. onAddConditionClicked (no path -- it's
    // always top-level, conditions are never nested) is wired to
    // addConditionalSlotToChain(). onConditionChanged/onModeChanged fire with
    // a top-level conditional slot's index and its edited PixelCondition/
    // CompositingMode. onExpansionChanged fires after a pure UI expand/
    // collapse toggle (no chain mutation involved) -- wired to
    // refreshEffectChainPanel() so the toggle's already-updated presentation
    // state (see Content::expandedConditionalSlots) gets re-rendered.
    // onApplyClicked is wired to MainComponent::applyClicked() directly.
    std::function<void(ChainPath)> onSelectSlot;
    std::function<void(ChainPath)> onRemoveSlot;
    std::function<void(ChainPath)> onToggleBypass;
    std::function<void(int, int)> onReorderSlot;
    std::function<void(int, Branch, int, int)> onReorderBranchSlot;
    std::function<void(std::optional<ChainPath>)> onAddEffectClicked;
    std::function<void()> onAddConditionClicked;
    std::function<void(int, PixelCondition)> onConditionChanged;
    std::function<void(int, CompositingMode)> onModeChanged;
    std::function<void()> onExpansionChanged;
    std::function<void()> onApplyClicked;

    void paint(juce::Graphics& g) override
    {
        const auto& palette = RawdogLookAndFeel::Palette::get();

        // Same halftone "workspace mat" the image canvas uses, but white
        // (surface) rather than the canvas's Platinum grey -- the rack sits
        // on a white sunken field, not a grey letterboxed one.
        RawdogLookAndFeel::drawDotMat(g, getLocalBounds(), palette.surface);

        g.setColour(palette.ink);
        g.drawLine(0.0f, (float) headerHeight, (float) getWidth(), (float) headerHeight, 1.0f);
    }

    // Drawn after every child (header labels/button, viewport/rack contents)
    // has already painted -- a single semi-transparent wash over the whole
    // panel reads as thoroughly "unavailable" without needing every child
    // component's own paint() to separately get its isEnabled()-dimming
    // exactly right, and it also mutes things like the header's plain text
    // labels that don't otherwise participate in that per-component dimming.
    void paintOverChildren(juce::Graphics& g) override
    {
        if (hasLoadedImage)
            return;

        g.setColour(RawdogLookAndFeel::Palette::get().windowBg.withAlpha(0.6f));
        g.fillRect(getLocalBounds());
    }

    void resized() override
    {
        auto area = getLocalBounds();

        auto headerArea = area.removeFromTop(headerHeight).reduced(6, 4);
        applyButton.setBounds(headerArea.removeFromRight(60));
        headerArea.removeFromRight(6);
        chainHeaderLabel.setBounds(headerArea.removeFromLeft(46));
        headerArea.removeFromLeft(6);
        chainCountLabel.setBounds(headerArea);

        viewport.setBounds(area);
        content.setSize(viewport.getMaximumVisibleWidth(), content.getPreferredHeight());
        content.resized();
    }

private:
    static constexpr int headerHeight = 28;
    bool hasLoadedImage = false;

    // Hand-drawn 10x10 checkbox. Checked == this slot is enabled (NOT
    // bypassed), matching the design mockup's `on` flag; click toggles
    // bypass via the onToggle callback.
    struct CheckboxComponent : public juce::Component
    {
        CheckboxComponent() { setMouseCursor(juce::MouseCursor::PointingHandCursor); }

        void mouseUp(const juce::MouseEvent& e) override
        {
            if (isEnabled() && contains(e.getPosition()) && onToggle)
                onToggle();
        }

        void paint(juce::Graphics& g) override
        {
            const auto& palette = RawdogLookAndFeel::Palette::get();
            const bool enabled = isEnabled();
            auto bounds = getLocalBounds().toFloat().reduced(0.5f);

            g.setColour(palette.surface);
            g.fillRect(bounds);
            g.setColour(enabled ? palette.ink : palette.inkMuted);
            g.drawRect(bounds, 1.0f);

            if (checked)
            {
                g.setColour(enabled ? palette.ink : palette.inkMuted);
                g.setFont(RawdogLookAndFeel::chromeFont(juce::jmin(9.0f, bounds.getHeight())));
                g.drawText(juce::CharPointer_UTF8("\xE2\x9C\x93"), getLocalBounds(), juce::Justification::centred);
            }
        }

        bool checked = false;
        std::function<void()> onToggle;
    };

    // The drag handle -- a real child component (not a hit-test region inside
    // RowComponent) so JUCE's own hit-testing routes a grip-zone mouseDown
    // here automatically, meaning RowComponent's own mouseUp (select) simply
    // never fires for a grip-initiated gesture -- no threshold/heuristic
    // needed to tell a drag from a click.
    struct GripHandle : public juce::Component
    {
        GripHandle() { setMouseCursor(juce::MouseCursor::DraggingHandCursor); }

        // Guarding just mouseDown is enough to shut off the whole gesture when
        // disabled: mouseDrag/mouseUp only ever fire on this component because
        // it captured the mouse in mouseDown, so if that never starts a drag
        // (draggingIndex never gets set in Content::beginDrag), the later
        // calls are no-ops anyway (Content::updateDrag/endDrag both early-out
        // on draggingIndex < 0).
        void mouseDown(const juce::MouseEvent& e) override { if (isEnabled() && onGripDown) onGripDown(e); }
        void mouseDrag(const juce::MouseEvent& e) override { if (onGripDrag) onGripDrag(e); }
        void mouseUp(const juce::MouseEvent& e) override { if (onGripUp) onGripUp(e); }

        void paint(juce::Graphics& g) override
        {
            const auto& palette = RawdogLookAndFeel::Palette::get();
            g.setColour(isEnabled() ? palette.inkMuted : palette.divider);
            g.setFont(RawdogLookAndFeel::chromeFont(11.0f));
            g.drawText(juce::CharPointer_UTF8("\xE2\xA3\xBF"), getLocalBounds(), juce::Justification::centred);
        }

        std::function<void(const juce::MouseEvent&)> onGripDown;
        std::function<void(const juce::MouseEvent&)> onGripDrag;
        std::function<void(const juce::MouseEvent&)> onGripUp;
    };

    // A ConditionalChainSlot's expand/collapse disclosure arrow. Deliberately
    // NOT a juce::TextButton with a glyph set via setButtonText() -- at this
    // component's narrow width, TextButton's own text layout ellipsizes a
    // Unicode glyph down to "...", the same reason PluginListModel's own
    // vendor/preset disclosure triangles are hand-painted rather than built
    // from a button. Mirrors GripHandle's shape immediately above: plain
    // Component, custom paint, hit-tested via mouseUp.
    struct ExpandButton : public juce::Component
    {
        ExpandButton() { setMouseCursor(juce::MouseCursor::PointingHandCursor); }

        void mouseUp(const juce::MouseEvent& e) override
        {
            if (isEnabled() && contains(e.getPosition()) && onClick)
                onClick();
        }

        void paint(juce::Graphics& g) override
        {
            const auto& palette = RawdogLookAndFeel::Palette::get();
            g.setColour(isEnabled() ? palette.ink : palette.inkMuted);
            g.setFont(RawdogLookAndFeel::chromeFont(11.0f));
            g.drawText(expanded ? juce::CharPointer_UTF8("\xE2\x96\xBE") : juce::CharPointer_UTF8("\xE2\x96\xB8"),
                       getLocalBounds(), juce::Justification::centred);
        }

        bool expanded = false;
        std::function<void()> onClick;
    };

    // A small "..." menu-trigger button, sitting alongside the top-level
    // "+ Add Effect" row to offer "Add Condition" as a secondary action
    // without a second full-width dashed row. Custom-painted rather than a
    // juce::TextButton for the same reason ExpandButton is -- see its comment
    // just above.
    struct MenuTriggerButton : public juce::Component
    {
        MenuTriggerButton() { setMouseCursor(juce::MouseCursor::PointingHandCursor); }

        void mouseUp(const juce::MouseEvent& e) override
        {
            if (isEnabled() && contains(e.getPosition()) && onClick)
                onClick();
        }

        // Same hover/dashed-box treatment as AddEffectRow, just squared and
        // holding a single "+" glyph instead of label text -- reads as the
        // same family of control sitting right beside it, not a stray icon.
        void mouseEnter(const juce::MouseEvent&) override { hovering = true; repaint(); }
        void mouseExit(const juce::MouseEvent&) override { hovering = false; repaint(); }

        void paint(juce::Graphics& g) override
        {
            const auto& palette = RawdogLookAndFeel::Palette::get();
            const bool enabled = isEnabled();
            const auto lineColour = ! enabled ? palette.divider : (hovering ? palette.ink : palette.inkMuted);
            auto bounds = getLocalBounds().toFloat().reduced(1.0f);

            g.setColour(! enabled ? palette.windowBg : (hovering ? palette.surface.darker(0.04f) : palette.surface));
            g.fillRect(bounds);

            juce::Path outline;
            outline.addRectangle(bounds);
            juce::Path dashedOutline;
            const float dashLengths[] = { 4.0f, 3.0f };
            juce::PathStrokeType(1.0f).createDashedStroke(dashedOutline, outline, dashLengths, 2);
            g.setColour(lineColour);
            g.fillPath(dashedOutline);

            // Drawn as two crossed geometric strokes, not a text glyph --
            // sidesteps font metrics/centring entirely, the same reasoning
            // ExpandButton/RemoveButton's glyphs use.
            g.setColour(lineColour);
            const auto centre = getLocalBounds().toFloat().getCentre();
            constexpr float armLength = 5.0f;
            g.drawLine(centre.x - armLength, centre.y, centre.x + armLength, centre.y, 1.5f);
            g.drawLine(centre.x, centre.y - armLength, centre.x, centre.y + armLength, 1.5f);
        }

        std::function<void()> onClick;

    private:
        bool hovering = false;
    };

    // A row's "x" remove button -- shared by RowComponent and
    // ConditionalRowComponent so both literally use the same control.
    // Custom-painted rather than a juce::TextButton: LookAndFeel_V4's default
    // drawButtonText() reserves edge padding proportional to
    // min(width,height)/2 (its corner-radius heuristic), which eats most of a
    // narrow button's interior and ellipsizes the glyph to "..." -- exactly
    // the same class of bug ExpandButton/MenuTriggerButton were written to
    // avoid, and worse here since it's height-dependent, so the identical
    // pixel width can render fine in one row and fail in a taller one.
    struct RemoveButton : public juce::Component,
                           public juce::SettableTooltipClient
    {
        RemoveButton() { setMouseCursor(juce::MouseCursor::PointingHandCursor); }

        void mouseUp(const juce::MouseEvent& e) override
        {
            if (isEnabled() && contains(e.getPosition()) && onClick)
                onClick();
        }

        void paint(juce::Graphics& g) override
        {
            const auto& palette = RawdogLookAndFeel::Palette::get();
            g.setColour(isEnabled() ? palette.ink : palette.inkMuted);
            g.setFont(RawdogLookAndFeel::chromeFont(11.0f));
            g.drawText(juce::CharPointer_UTF8("\xC3\x97"), getLocalBounds(), juce::Justification::centred); // multiplication sign, reused as "x"
        }

        std::function<void()> onClick;
    };

    // A tiny "current value + dropdown arrow" control, backed by a
    // juce::PopupMenu rather than a real juce::ComboBox. A real ComboBox's
    // own internal text display kept showing "..." for its selected item at
    // every width tried, with no public way to reach in and zero out
    // whatever padding/fitting it applies internally (unlike Label, which at
    // least exposes setBorderSize() for exactly this problem -- see
    // RowComponent's indexLabel). Drawing the text ourselves via plain
    // Graphics::drawText (which never auto-ellipsizes, confirmed by
    // ExpandButton/MenuTriggerButton/RemoveButton above) sidesteps the whole
    // class of bug, and a manually-shown PopupMenu gives the same selection
    // behaviour a real ComboBox would.
    struct MiniDropdown : public juce::Component,
                           public juce::SettableTooltipClient
    {
        MiniDropdown() { setMouseCursor(juce::MouseCursor::PointingHandCursor); }

        void mouseUp(const juce::MouseEvent& e) override
        {
            if (! isEnabled() || ! contains(e.getPosition()))
                return;

            juce::PopupMenu menu;
            for (int i = 0; i < items.size(); ++i)
                menu.addItem(i + 1, items[i], true, i == selectedIndex);

            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [this](int result)
            {
                if (result <= 0)
                    return;

                selectedIndex = result - 1;
                repaint();

                if (onChange)
                    onChange(selectedIndex);
            });
        }

        void paint(juce::Graphics& g) override
        {
            const auto& palette = RawdogLookAndFeel::Palette::get();
            const bool enabled = isEnabled();
            const auto lineColour = enabled ? palette.ink : palette.inkMuted;

            g.setColour(palette.surface);
            g.fillRect(getLocalBounds());
            g.setColour(lineColour);
            g.drawRect(getLocalBounds(), 1);

            auto textArea = getLocalBounds().reduced(4, 0);
            auto arrowZone = textArea.removeFromRight(12);

            g.setColour(lineColour);
            g.setFont(RawdogLookAndFeel::chromeFont(10.0f));
            g.drawText(juce::isPositiveAndBelow(selectedIndex, items.size()) ? items[selectedIndex] : juce::String(),
                       textArea, juce::Justification::centredLeft);

            // Small downward triangle -- same shape idiom
            // RawdogLookAndFeel::drawComboBox() uses for a real ComboBox's
            // arrow, so this still reads as "a dropdown".
            juce::Path arrow;
            const auto centre = arrowZone.toFloat().getCentre();
            constexpr float halfWidth = 3.5f, arrowHeight = 3.0f;
            arrow.startNewSubPath(centre.x - halfWidth, centre.y - arrowHeight * 0.5f);
            arrow.lineTo(centre.x + halfWidth, centre.y - arrowHeight * 0.5f);
            arrow.lineTo(centre.x, centre.y + arrowHeight * 0.5f);
            arrow.closeSubPath();
            g.setColour(lineColour);
            g.fillPath(arrow);
        }

        juce::StringArray items;
        int selectedIndex = 0;
        std::function<void(int)> onChange; // called with the newly selected index
    };

    // Common interface for a top-level rack row -- either a plain RowComponent
    // or a ConditionalRowComponent -- so Content can lay out/measure a
    // heterogeneous list uniformly (a conditional slot's height depends on
    // its expand state, a plain slot's is fixed).
    struct EntryRowComponent : public juce::Component
    {
        virtual int getPreferredHeight() const = 0;
    };

    // One horizontal strip per plain plugin slot, at the top level or nested
    // inside a ConditionalChainSlot's branch. Only overrides mouseUp (not
    // mouseDown) to treat a click on the row itself as "select this slot" --
    // JUCE routes a click that lands on a child (checkbox/grip/remove) to
    // that child instead, so this never double-fires; the child labels have
    // setInterceptsMouseClicks(false, false) so clicks on them still fall
    // through to this row.
    struct RowComponent : public EntryRowComponent
    {
        RowComponent(ChainPath pathIn, const juce::String& name, bool bypassedIn, bool isSelected,
                     const std::function<void(ChainPath)>& onSelectIn, const std::function<void(ChainPath)>& onRemoveIn,
                     const std::function<void(ChainPath)>& onToggleBypassIn,
                     std::function<void(int, const juce::MouseEvent&)> onGripDownIn,
                     std::function<void(const juce::MouseEvent&)> onGripDragIn,
                     std::function<void(const juce::MouseEvent&)> onGripUpIn)
            : path(pathIn), selected(isSelected), bypassed(bypassedIn), onSelect(onSelectIn)
        {
            setMouseCursor(juce::MouseCursor::PointingHandCursor);

            // A row's position within its own list (Content::topLevelRows for
            // a top-level slot, BranchList::rows for a branch slot) is always
            // exactly path.topIndex or path.branchIndex respectively -- used
            // both for the visible "01"/"02" label and for the grip's own
            // drag-source index.
            const int localIndex = pathIn.branch.has_value() ? pathIn.branchIndex : pathIn.topIndex;

            indexLabel.setText(juce::String(localIndex + 1).paddedLeft('0', 2), juce::dontSendNotification);
            indexLabel.setFont(RawdogLookAndFeel::chromeFont(8.0f));
            indexLabel.setJustificationType(juce::Justification::centred);
            indexLabel.setBorderSize(juce::BorderSize<int> (0)); // Label's default border leaves too little width for "01".."99" at this column width
            indexLabel.setInterceptsMouseClicks(false, false);
            addAndMakeVisible(indexLabel);

            checkbox.checked = ! bypassedIn;
            checkbox.onToggle = [onToggleBypassIn, pathIn] { if (onToggleBypassIn) onToggleBypassIn(pathIn); };
            addAndMakeVisible(checkbox);

            nameLabel.setText(name, juce::dontSendNotification);
            nameLabel.setFont(RawdogLookAndFeel::chromeFont(11.0f));
            nameLabel.setMinimumHorizontalScale(0.6f); // shrinks-to-fit rather than truncating a long plugin name outright
            nameLabel.setInterceptsMouseClicks(false, false);
            addAndMakeVisible(nameLabel);

            stateLabel.setText(bypassedIn ? "bypassed" : juce::String(), juce::dontSendNotification);
            stateLabel.setFont(RawdogLookAndFeel::chromeFont(8.0f));
            stateLabel.setJustificationType(juce::Justification::centredRight);
            stateLabel.setInterceptsMouseClicks(false, false);
            addAndMakeVisible(stateLabel);

            applyLabelColours();

            grip.onGripDown = [onGripDownIn, localIndex](const juce::MouseEvent& e) { if (onGripDownIn) onGripDownIn(localIndex, e); };
            grip.onGripDrag = std::move(onGripDragIn);
            grip.onGripUp = std::move(onGripUpIn);
            addAndMakeVisible(grip);

            removeButton.setTooltip("Remove this effect from the chain");
            removeButton.onClick = [onRemoveIn, pathIn] { if (onRemoveIn) onRemoveIn(pathIn); };
            addAndMakeVisible(removeButton);
        }

        void mouseUp(const juce::MouseEvent& e) override
        {
            if (isEnabled() && contains(e.getPosition()) && onSelect)
                onSelect(path);
        }

        int getPreferredHeight() const override { return rowHeight; }

        // indexLabel/nameLabel/stateLabel's textColourId is set directly
        // (rather than left to the LookAndFeel) so selected/bypassed rows can
        // each use a different palette colour -- reapply on a theme switch,
        // since rows are only otherwise rebuilt on chain mutations/selection
        // changes, not on RawdogLookAndFeel::refreshAllWindows().
        void lookAndFeelChanged() override { applyLabelColours(); }

        void paint(juce::Graphics& g) override
        {
            const auto& palette = RawdogLookAndFeel::Palette::get();
            const bool enabled = isEnabled();

            g.setColour(palette.divider);
            g.fillRect(getLocalBounds());

            g.setColour(! enabled ? palette.windowBg : (selected ? palette.selectedBg : palette.surface));
            g.fillRect(boxBounds());

            g.setColour(enabled ? palette.ink : palette.inkMuted);
            g.drawRect(boxBounds(), (enabled && selected) ? 2 : 1);
        }

        void resized() override
        {
            auto area = boxBounds().reduced(8, 5);

            indexLabel.setBounds(area.removeFromLeft(16));
            area.removeFromLeft(4);
            checkbox.setBounds(area.removeFromLeft(14).withSizeKeepingCentre(10, 10));
            area.removeFromLeft(6);

            removeButton.setBounds(area.removeFromRight(18));
            grip.setBounds(area.removeFromRight(20));

            // stateLabel only claims width when it actually holds text, and even
            // then only what nameLabel can spare after keeping its minimum --
            // otherwise an empty stateLabel leaves a dead gap next to an elided
            // name, or a narrow panel starves nameLabel down to zero.
            const auto stateWidth = bypassed ? juce::jlimit(0, 56, area.getWidth() - minNameWidth) : 0;
            stateLabel.setBounds(area.removeFromRight(stateWidth));

            nameLabel.setBounds(area);
        }

        static constexpr int rowHeight = 30;
        static constexpr int minNameWidth = 30;

    private:
        // The row's own bounds trimmed by shadowOffset on the right/bottom --
        // shared by paint() (the actual box fill/border, with the untrimmed
        // full bounds behind it reading as the shadow strip) and resized()
        // (so child controls stay inset within the visible box rather than
        // overlapping the shadow).
        static constexpr int shadowOffset = 2;
        juce::Rectangle<int> boxBounds() const { return getLocalBounds().withTrimmedRight(shadowOffset).withTrimmedBottom(shadowOffset); }

        void applyLabelColours()
        {
            const auto& palette = RawdogLookAndFeel::Palette::get();
            const auto textColour = selected ? palette.selectedFg : (bypassed ? palette.inkMuted : palette.ink);

            indexLabel.setColour(juce::Label::textColourId, selected ? palette.selectedFg : palette.inkMuted);
            nameLabel.setColour(juce::Label::textColourId, textColour);
            stateLabel.setColour(juce::Label::textColourId, selected ? palette.selectedFg : palette.inkMuted);
        }

        ChainPath path;
        bool selected;
        bool bypassed;
        std::function<void(ChainPath)> onSelect;
        juce::Label indexLabel, nameLabel, stateLabel;
        CheckboxComponent checkbox;
        GripHandle grip;
        RemoveButton removeButton;
    };

    // A placeholder row -- not a real chain entry, just an action. Styled
    // with a dashed border and muted glyph/label so it reads as an empty slot
    // to fill rather than another effect. Reused for both "+ Add Effect"
    // (top level or nested in a branch) and "+ Add Condition" (top level
    // only) -- the two differ only in label text and what onClick does.
    struct AddEffectRow : public juce::Component
    {
        AddEffectRow(juce::String labelIn, std::function<void()> onClickIn)
            : label(std::move(labelIn)), onClick(std::move(onClickIn))
        {
            setMouseCursor(juce::MouseCursor::PointingHandCursor);
        }

        void mouseUp(const juce::MouseEvent& e) override
        {
            if (isEnabled() && contains(e.getPosition()) && onClick)
                onClick();
        }

        // Only enabled/hovering needs a repaint -- disabled rows never
        // receive these (the mouse is over the panel's dimming wash instead,
        // see EffectChainPanel::paintOverChildren()).
        void mouseEnter(const juce::MouseEvent&) override { hovering = true; repaint(); }
        void mouseExit(const juce::MouseEvent&) override { hovering = false; repaint(); }

        void paint(juce::Graphics& g) override
        {
            const auto& palette = RawdogLookAndFeel::Palette::get();
            const bool enabled = isEnabled();
            // Full ink (rather than the resting muted tone) is this row's
            // only hover affordance -- it has no separate raised/pressed
            // bevel state to lean on, unlike the real buttons elsewhere.
            const auto lineColour = ! enabled ? palette.divider : (hovering ? palette.ink : palette.inkMuted);
            auto bounds = getLocalBounds().toFloat().reduced(1.0f);

            // Flatter, more uniformly muted block when disabled -- a crisp
            // white fill read as "still interactive" even with dimmed
            // border/text, matching the design mockup's disabled treatment.
            g.setColour(! enabled ? palette.windowBg : (hovering ? palette.surface.darker(0.04f) : palette.surface));
            g.fillRect(bounds);

            juce::Path outline;
            outline.addRectangle(bounds);
            juce::Path dashedOutline;
            const float dashLengths[] = { 4.0f, 3.0f };
            juce::PathStrokeType(1.0f).createDashedStroke(dashedOutline, outline, dashLengths, 2);
            g.setColour(lineColour);
            g.fillPath(dashedOutline);

            g.setColour(lineColour);
            g.setFont(RawdogLookAndFeel::chromeFont(11.0f));
            g.drawText(label, getLocalBounds(), juce::Justification::centred);
        }

        juce::String label;
        std::function<void()> onClick;

        static constexpr int rowHeight = 28;

    private:
        bool hovering = false;
    };

    // A short static vertical line between two adjacent rows (or between a
    // cap and its nearest row) -- purely decorative, no mouse handling. No
    // arrowhead: the IN/OUT caps already make the top-to-bottom signal
    // direction explicit.
    struct ConnectorComponent : public juce::Component
    {
        void paint(juce::Graphics& g) override
        {
            const auto& palette = RawdogLookAndFeel::Palette::get();
            auto area = getLocalBounds().toFloat();
            const float midX = area.getCentreX();

            g.setColour(isEnabled() ? palette.ink : palette.inkMuted);
            g.drawLine(midX, area.getY(), midX, area.getBottom(), 1.0f);
        }

        static constexpr int connectorHeight = 10;
    };

    // The IN/OUT signal-flow caps bookending the chain: a small pill label
    // ("IN -- signal.jpg" / "OUT -- preview") plus a dashed rule filling the
    // rest of the width, mirroring the design mockup's treatment.
    struct IoCapComponent : public juce::Component
    {
        void paint(juce::Graphics& g) override
        {
            const auto& palette = RawdogLookAndFeel::Palette::get();
            const bool enabled = isEnabled();
            auto area = getLocalBounds();

            const auto font = RawdogLookAndFeel::chromeFont(8.0f);
            g.setFont(font);
            const int textWidth = (int) juce::GlyphArrangement::getStringWidth(font, label) + 14;
            auto pillArea = area.removeFromLeft(juce::jmin(textWidth, area.getWidth()));

            g.setColour(palette.surface);
            g.fillRect(pillArea);
            g.setColour(enabled ? palette.ink : palette.inkMuted);
            g.drawRect(pillArea, 1);
            g.drawText(label, pillArea, juce::Justification::centred);

            area.removeFromLeft(6);
            const float midY = (float) area.getCentreY();

            juce::Path line;
            line.startNewSubPath((float) area.getX(), midY);
            line.lineTo((float) area.getRight(), midY);
            juce::Path dashedLine;
            const float dashLengths[] = { 3.0f, 3.0f };
            juce::PathStrokeType(1.0f).createDashedStroke(dashedLine, line, dashLengths, 2);
            g.setColour(enabled ? palette.divider : palette.windowBg.darker(0.05f));
            g.fillPath(dashedLine);
        }

        juce::String label;

        static constexpr int capHeight = 18;
    };

    // One branch's own mini-rack: an ordered list of plain plugin slots plus
    // a trailing "+ Add Effect" row, reusing RowComponent/ConnectorComponent/
    // AddEffectRow verbatim. Deliberately no IN/OUT caps (too much visual
    // weight this deep in the nest) and no support for a conditional slot
    // inside it -- rebuild()'s `slots` parameter is a plain
    // std::vector<ChainSlot>, so nesting is impossible at the type level.
    // Owns its own drag-to-reorder state, independent of whichever Content
    // (top-level or another branch) it happens to be nested inside.
    class BranchList : public juce::Component
    {
    public:
        void rebuild(int topIndexIn, Branch branchIn, const std::vector<ChainSlot>& slots,
                     std::optional<ChainPath> selected,
                     const std::function<void(ChainPath)>& onSelectIn, const std::function<void(ChainPath)>& onRemoveIn,
                     const std::function<void(ChainPath)>& onToggleBypassIn,
                     const std::function<void(int, Branch, int, int)>& onReorderBranchSlotIn,
                     const std::function<void(std::optional<ChainPath>)>& onAddEffectClickedIn)
        {
            topIndex = topIndexIn;
            branch = branchIn;
            draggingIndex = -1;
            dropIndex = -1;
            onReorderBranchSlot = onReorderBranchSlotIn;

            rows.clear();
            connectors.clear();

            for (int j = 0; j < (int) slots.size(); ++j)
            {
                const ChainPath path { topIndex, branch, j };

                auto connector = std::make_unique<ConnectorComponent>();
                addAndMakeVisible(*connector);
                connectors.push_back(std::move(connector));

                const auto& slot = slots[(size_t) j];
                auto row = std::make_unique<RowComponent>(
                    path, slot.plugin->getName(), slot.bypassed, selected.has_value() && *selected == path,
                    onSelectIn, onRemoveIn, onToggleBypassIn,
                    [this](int idx, const juce::MouseEvent& e) { beginDrag(idx, e); },
                    [this](const juce::MouseEvent& e) { updateDrag(e); },
                    [this](const juce::MouseEvent& e) { endDrag(e); });
                addAndMakeVisible(*row);
                rows.push_back(std::move(row));
            }

            const int capturedTopIndex = topIndex;
            const Branch capturedBranch = branch;
            addEffectRow = std::make_unique<AddEffectRow>("+ Add Effect", [onAddEffectClickedIn, capturedTopIndex, capturedBranch]
            {
                if (onAddEffectClickedIn)
                    onAddEffectClickedIn(ChainPath { capturedTopIndex, capturedBranch, -1 });
            });
            addAndMakeVisible(*addEffectRow);

            resized();
        }

        int getPreferredHeight() const
        {
            return (int) rows.size() * (ConnectorComponent::connectorHeight + RowComponent::rowHeight)
                 + ConnectorComponent::connectorHeight + AddEffectRow::rowHeight;
        }

        void paint(juce::Graphics& g) override
        {
            if (draggingIndex < 0)
                return;

            std::vector<juce::Rectangle<int>> staticBounds;
            for (int i = 0; i < (int) rows.size(); ++i)
                if (i != draggingIndex)
                    staticBounds.push_back(rows[(size_t) i]->getBounds());

            if (staticBounds.empty())
                return;

            int insertionY;
            if (dropIndex <= 0)
                insertionY = staticBounds.front().getY() - ConnectorComponent::connectorHeight / 2;
            else if (dropIndex >= (int) staticBounds.size())
                insertionY = staticBounds.back().getBottom() + ConnectorComponent::connectorHeight / 2;
            else
                insertionY = (staticBounds[(size_t) dropIndex - 1].getBottom() + staticBounds[(size_t) dropIndex].getY()) / 2;

            g.setColour(RawdogLookAndFeel::Palette::get().ink);
            g.fillRect(0, insertionY - 1, getWidth(), 2);
        }

        void resized() override
        {
            auto area = getLocalBounds();

            for (int i = 0; i < (int) rows.size(); ++i)
            {
                connectors[(size_t) i]->setBounds(area.removeFromTop(ConnectorComponent::connectorHeight));
                rows[(size_t) i]->setBounds(area.removeFromTop(RowComponent::rowHeight));
            }

            if (addEffectRow != nullptr)
            {
                area.removeFromTop(ConnectorComponent::connectorHeight);
                addEffectRow->setBounds(area.removeFromTop(AddEffectRow::rowHeight));
            }
        }

    private:
        // Same fixed-reference-frame drag idiom Content uses below -- see its
        // beginDrag()/updateDrag()/endDrag() comment for why.
        void beginDrag(int index, const juce::MouseEvent& e)
        {
            draggingIndex = index;
            dropIndex = index;
            dragStartRowTop = rows[(size_t) index]->getY();
            dragStartContentY = e.getEventRelativeTo(this).position.y;
            rows[(size_t) index]->toFront(false);
        }

        void updateDrag(const juce::MouseEvent& e)
        {
            if (draggingIndex < 0)
                return;

            const float currentContentY = e.getEventRelativeTo(this).position.y;
            const int newTop = dragStartRowTop + (int) (currentContentY - dragStartContentY);

            auto* draggedRow = rows[(size_t) draggingIndex].get();
            draggedRow->setTopLeftPosition(draggedRow->getX(), newTop);

            dropIndex = computeDropIndex();
            repaint();
        }

        void endDrag(const juce::MouseEvent&)
        {
            if (draggingIndex < 0)
                return;

            const int from = draggingIndex;
            const int to = dropIndex;
            draggingIndex = -1;
            dropIndex = -1;

            if (to != from && onReorderBranchSlot)
                onReorderBranchSlot(topIndex, branch, from, to);
            else
                resized();

            repaint();
        }

        int computeDropIndex() const
        {
            const float draggedCenterY = rows[(size_t) draggingIndex]->getBounds().toFloat().getCentreY();
            int result = 0;
            for (int i = 0; i < (int) rows.size(); ++i)
                if (i != draggingIndex && rows[(size_t) i]->getBounds().toFloat().getCentreY() < draggedCenterY)
                    ++result;
            return result;
        }

        int topIndex = -1;
        Branch branch = Branch::a;
        std::vector<std::unique_ptr<RowComponent>> rows;
        std::vector<std::unique_ptr<ConnectorComponent>> connectors;
        std::unique_ptr<AddEffectRow> addEffectRow;

        std::function<void(int, Branch, int, int)> onReorderBranchSlot;
        int draggingIndex = -1;
        int dropIndex = -1;
        int dragStartRowTop = 0;
        float dragStartContentY = 0.0f;
    };

    // The header row for a ConditionalChainSlot, plus (when expanded) its two
    // nested BranchList mini-racks. The header holds an expand/collapse
    // arrow, the slot index, a condition-type label, a threshold field, a
    // comparison-operator dropdown, a compositing-mode dropdown, the usual
    // bypass checkbox/remove button/drag handle, and (when expanded) the two
    // branches underneath, each labeled "Branch A (true)"/"Branch B (false)".
    //
    // Threshold/comparison/mode edits call back immediately (onConditionChangedIn/
    // onModeChangedIn) but deliberately do NOT go through a full rack
    // rebuild() -- MainComponent's handlers only call refreshLivePreview(),
    // not refreshEffectChainPanel(), for exactly this edit path: rebuilding
    // the rack while thresholdEditor is mid-edit would destroy and recreate
    // the very juce::TextEditor the user is typing into, resetting focus and
    // cursor position on every keystroke.
    struct ConditionalRowComponent : public EntryRowComponent
    {
        ConditionalRowComponent(int topIndexIn, const ConditionalChainSlot& conditional, std::optional<ChainPath> selected,
                                 bool expandedIn,
                                 const std::function<void(ChainPath)>& onSelectIn,
                                 const std::function<void(ChainPath)>& onRemoveIn,
                                 const std::function<void(ChainPath)>& onToggleBypassIn,
                                 const std::function<void(int, Branch, int, int)>& onReorderBranchSlotIn,
                                 const std::function<void(std::optional<ChainPath>)>& onAddEffectClickedIn,
                                 std::function<void(int, const juce::MouseEvent&)> onGripDownIn,
                                 std::function<void(const juce::MouseEvent&)> onGripDragIn,
                                 std::function<void(const juce::MouseEvent&)> onGripUpIn,
                                 std::function<void()> onToggleExpandIn,
                                 std::function<void(CompositingMode)> onModeChangedIn,
                                 std::function<void(PixelCondition)> onConditionChangedIn)
            : topIndex(topIndexIn), expanded(expandedIn), currentCondition(conditional.condition),
              onToggleExpand(std::move(onToggleExpandIn))
        {
            setMouseCursor(juce::MouseCursor::PointingHandCursor);

            expandButton.expanded = expandedIn;
            expandButton.onClick = [this] { if (onToggleExpand) onToggleExpand(); };
            addAndMakeVisible(expandButton);

            indexLabel.setText(juce::String(topIndexIn + 1).paddedLeft('0', 2), juce::dontSendNotification);
            indexLabel.setFont(RawdogLookAndFeel::chromeFont(8.0f));
            indexLabel.setJustificationType(juce::Justification::centred);
            indexLabel.setBorderSize(juce::BorderSize<int> (0)); // Label's default border leaves too little width for "01".."99" at this column width
            indexLabel.setInterceptsMouseClicks(false, false);
            addAndMakeVisible(indexLabel);

            // .add() with an explicit fromUTF8() conversion, not a braced
            // initializer list assigned to items directly -- that path
            // decoded the >= glyph's UTF-8 bytes as if they were Latin-1,
            // rendering as garbled "a-circumflex yen" text instead of the
            // intended single >= character.
            comparisonCombo.items.add(juce::String::fromUTF8("\xE2\x89\xA5")); // >=
            comparisonCombo.items.add("<");
            comparisonCombo.selectedIndex = currentCondition.op == ComparisonOp::greaterOrEqual ? 0 : 1;
            comparisonCombo.onChange = [this, onConditionChangedIn](int index)
            {
                currentCondition.op = index == 0 ? ComparisonOp::greaterOrEqual : ComparisonOp::lessThan;
                if (onConditionChangedIn)
                    onConditionChangedIn(currentCondition);
            };
            addAndMakeVisible(comparisonCombo);

            thresholdEditor.setInputRestrictions(3, "0123456789");
            thresholdEditor.setText(juce::String((int) currentCondition.threshold), juce::dontSendNotification);
            thresholdEditor.setTooltip("Brightness threshold (0-255)");
            thresholdEditor.setColour(juce::TextEditor::textColourId, RawdogLookAndFeel::Palette::get().ink);
            thresholdEditor.onTextChange = [this, onConditionChangedIn]
            {
                currentCondition.threshold = (juce::uint8) juce::jlimit(0, 255, thresholdEditor.getText().getIntValue());
                if (onConditionChangedIn)
                    onConditionChangedIn(currentCondition);
            };
            addAndMakeVisible(thresholdEditor);

            // Short forms -- the tooltip below spells out what each actually
            // does; the dropdown itself just needs to fit comfortably at
            // this row's width.
            modeCombo.items = { "Masked", "Compact" };
            modeCombo.selectedIndex = conditional.mode == CompositingMode::masked ? 0 : 1;
            modeCombo.setTooltip("Masked: both branches process the whole scope, then composite per pixel. "
                                  "Compacted: each branch only processes its own matching samples, packed together.");
            modeCombo.onChange = [this, onModeChangedIn](int index)
            {
                if (onModeChangedIn)
                    onModeChangedIn(index == 0 ? CompositingMode::masked : CompositingMode::compacted);
            };
            addAndMakeVisible(modeCombo);

            checkbox.checked = ! conditional.bypassed;
            checkbox.onToggle = [onToggleBypassIn, topIndexIn] { if (onToggleBypassIn) onToggleBypassIn({ topIndexIn, std::nullopt, -1 }); };
            addAndMakeVisible(checkbox);

            grip.onGripDown = [onGripDownIn, topIndexIn](const juce::MouseEvent& e) { if (onGripDownIn) onGripDownIn(topIndexIn, e); };
            grip.onGripDrag = std::move(onGripDragIn);
            grip.onGripUp = std::move(onGripUpIn);
            addAndMakeVisible(grip);

            removeButton.setTooltip("Remove this condition from the chain");
            removeButton.onClick = [onRemoveIn, topIndexIn] { if (onRemoveIn) onRemoveIn({ topIndexIn, std::nullopt, -1 }); };
            addAndMakeVisible(removeButton);

            if (expanded)
            {
                branchALabel.setText("Branch A (true)", juce::dontSendNotification);
                branchALabel.setFont(RawdogLookAndFeel::chromeFont(9.0f));
                branchALabel.setColour(juce::Label::textColourId, RawdogLookAndFeel::Palette::get().inkMuted);
                addAndMakeVisible(branchALabel);
                branchAList.rebuild(topIndex, Branch::a, conditional.branchA, selected, onSelectIn, onRemoveIn,
                                     onToggleBypassIn, onReorderBranchSlotIn, onAddEffectClickedIn);
                addAndMakeVisible(branchAList);

                branchBLabel.setText("Branch B (false)", juce::dontSendNotification);
                branchBLabel.setFont(RawdogLookAndFeel::chromeFont(9.0f));
                branchBLabel.setColour(juce::Label::textColourId, RawdogLookAndFeel::Palette::get().inkMuted);
                addAndMakeVisible(branchBLabel);
                branchBList.rebuild(topIndex, Branch::b, conditional.branchB, selected, onSelectIn, onRemoveIn,
                                     onToggleBypassIn, onReorderBranchSlotIn, onAddEffectClickedIn);
                addAndMakeVisible(branchBList);
            }
        }

        // Clicking anywhere on the header (not just the small expand arrow)
        // toggles expand/collapse. Only ever fires for a click that landed
        // outside every header child -- JUCE routes a click on a child
        // (checkbox/grip/remove/combos/expandButton) to that child instead,
        // and a click over the expanded branch area is claimed by
        // branchAList/branchBList before it would ever reach here.
        void mouseUp(const juce::MouseEvent& e) override
        {
            if (isEnabled() && e.getPosition().y < headerHeight && onToggleExpand)
                onToggleExpand();
        }

        int getPreferredHeight() const override
        {
            if (! expanded)
                return headerHeight;

            // branchAreaMargin appears 3 times: above Branch A, between the
            // two branches, and below Branch B -- see resized()'s matching
            // layout below.
            return headerHeight + branchAreaMargin + branchSectionLabelHeight + branchAList.getPreferredHeight()
                                 + branchAreaMargin + branchSectionLabelHeight + branchBList.getPreferredHeight()
                                 + branchAreaMargin;
        }

        void paint(juce::Graphics& g) override
        {
            const auto& palette = RawdogLookAndFeel::Palette::get();
            const bool enabled = isEnabled();

            // The divider-grey "shadow" fill only belongs behind the
            // header's own trimmed box (matching RowComponent's fixed-height
            // shadow trick) -- NOT the whole component. Unlike RowComponent,
            // this component's bounds extend well past a single row height
            // when expanded, so filling getLocalBounds() with divider grey
            // here would wash the entire branch/padding area in shadow grey
            // instead of the panel's normal white surface.
            auto headerArea = getLocalBounds().withHeight(headerHeight);
            g.setColour(palette.divider);
            g.fillRect(headerArea);

            auto header = headerArea.withTrimmedRight(shadowOffset).withTrimmedBottom(shadowOffset);
            g.setColour(! enabled ? palette.windowBg : palette.surface);
            g.fillRect(header);
            g.setColour(enabled ? palette.ink : palette.inkMuted);
            g.drawRect(header, 1);

            if (expanded)
            {
                g.setColour(palette.surface);
                g.fillRect(getLocalBounds().withTrimmedTop(headerHeight));
            }
        }

        void resized() override
        {
            auto area = getLocalBounds();
            auto header = area.removeFromTop(headerHeight).reduced(6, 4);

            expandButton.setBounds(header.removeFromLeft(16));
            indexLabel.setBounds(header.removeFromLeft(16));
            header.removeFromLeft(4);
            checkbox.setBounds(header.removeFromLeft(14).withSizeKeepingCentre(10, 10));
            header.removeFromLeft(6);

            removeButton.setBounds(header.removeFromRight(18));
            grip.setBounds(header.removeFromRight(20));
            modeCombo.setBounds(header.removeFromRight(56));
            header.removeFromRight(4);
            thresholdEditor.setBounds(header.removeFromRight(36));
            header.removeFromRight(4);
            comparisonCombo.setBounds(header.removeFromRight(46));
            header.removeFromRight(4);

            if (expanded)
            {
                auto branchArea = area.reduced(branchAreaMargin, 0);
                branchArea.removeFromTop(branchAreaMargin);

                branchALabel.setBounds(branchArea.removeFromTop(branchSectionLabelHeight));
                branchAList.setBounds(branchArea.removeFromTop(branchAList.getPreferredHeight()));

                branchArea.removeFromTop(branchAreaMargin);
                branchBLabel.setBounds(branchArea.removeFromTop(branchSectionLabelHeight));
                branchBList.setBounds(branchArea.removeFromTop(branchBList.getPreferredHeight()));
            }
        }

        static constexpr int headerHeight = 34;
        static constexpr int branchSectionLabelHeight = 16;
        // Breathing room around the expanded branch section's contents --
        // without this, a branch's rows sit flush against the conditional
        // row's own outer edge.
        static constexpr int branchAreaMargin = 6;

    private:
        static constexpr int shadowOffset = 2;

        int topIndex;
        bool expanded;
        PixelCondition currentCondition;
        std::function<void()> onToggleExpand;

        juce::Label indexLabel, branchALabel, branchBLabel;
        juce::TextEditor thresholdEditor;
        MiniDropdown comparisonCombo, modeCombo;
        CheckboxComponent checkbox;
        GripHandle grip;
        ExpandButton expandButton;
        RemoveButton removeButton;

        BranchList branchAList, branchBList;
    };

    // The scrollable body: an IN cap, one connector+row pair per top-level
    // chain entry (connector first, so there's one right after the IN cap
    // too), a trailing connector + "+ Add Effect" row, a connector + "+ Add
    // Condition" row, then a connector + OUT cap -- all built and laid out in
    // ascending chain-index order (top to bottom).
    class Content : public juce::Component
    {
    public:
        Content()
        {
            inCap.label = "IN - NO IMAGE"; // overwritten by setInputLabel() once a real image loads
            addAndMakeVisible(inCap);
            outCap.label = "OUT - preview";
            addAndMakeVisible(outCap);
        }

        void rebuild(const std::vector<ChainEntry>& chain, std::optional<ChainPath> selected,
                     const std::function<void(ChainPath)>& onSelectIn, const std::function<void(ChainPath)>& onRemoveIn,
                     const std::function<void(ChainPath)>& onToggleBypassIn,
                     const std::function<void(int, int)>& onReorderIn,
                     const std::function<void(int, Branch, int, int)>& onReorderBranchSlotIn,
                     const std::function<void(std::optional<ChainPath>)>& onAddEffectClickedIn,
                     const std::function<void()>& onAddConditionClickedIn,
                     const std::function<void(int, PixelCondition)>& onConditionChangedIn,
                     const std::function<void(int, CompositingMode)>& onModeChangedIn,
                     const std::function<void()>& onExpansionChangedIn)
        {
            // Drag state can never survive a rebuild -- either the drag just
            // committed a reorder (which is what triggered this rebuild), or
            // this rebuild is unrelated and any in-flight drag's rows are
            // about to be destroyed anyway.
            draggingIndex = -1;
            dropIndex = -1;
            onReorderSlot = onReorderIn;
            onExpansionChanged = onExpansionChangedIn;

            topLevelRows.clear();
            connectors.clear();

            for (int i = 0; i < (int) chain.size(); ++i)
            {
                auto connector = std::make_unique<ConnectorComponent>();
                addAndMakeVisible(*connector);
                connectors.push_back(std::move(connector));

                const auto& entry = chain[(size_t) i];

                if (auto* slot = std::get_if<ChainSlot>(&entry))
                {
                    const ChainPath path { i, std::nullopt, -1 };
                    auto row = std::make_unique<RowComponent>(
                        path, slot->plugin->getName(), slot->bypassed, selected.has_value() && *selected == path,
                        onSelectIn, onRemoveIn, onToggleBypassIn,
                        [this](int idx, const juce::MouseEvent& e) { beginDrag(idx, e); },
                        [this](const juce::MouseEvent& e) { updateDrag(e); },
                        [this](const juce::MouseEvent& e) { endDrag(e); });
                    addAndMakeVisible(*row);
                    topLevelRows.push_back(std::move(row));
                }
                else
                {
                    auto& conditional = std::get<ConditionalChainSlot>(entry);
                    auto row = std::make_unique<ConditionalRowComponent>(
                        i, conditional, selected, expandedConditionalSlots.count(i) > 0,
                        onSelectIn, onRemoveIn, onToggleBypassIn, onReorderBranchSlotIn, onAddEffectClickedIn,
                        [this](int idx, const juce::MouseEvent& e) { beginDrag(idx, e); },
                        [this](const juce::MouseEvent& e) { updateDrag(e); },
                        [this](const juce::MouseEvent& e) { endDrag(e); },
                        [this, i] { toggleExpanded(i); },
                        [onModeChangedIn, i](CompositingMode mode) { if (onModeChangedIn) onModeChangedIn(i, mode); },
                        [onConditionChangedIn, i](PixelCondition condition) { if (onConditionChangedIn) onConditionChangedIn(i, condition); });
                    addAndMakeVisible(*row);
                    topLevelRows.push_back(std::move(row));
                }
            }

            // A single top-level placeholder row: a wide "+ Add Effect"
            // dashed box (direct click, same as a branch's own "+ Add
            // Effect" row) plus a small "..." trigger alongside it that pops
            // up "Add Condition" as a secondary action -- conditions are
            // never nested, so a branch's own row never gets this trigger.
            addEffectConnector = std::make_unique<ConnectorComponent>();
            addAndMakeVisible(*addEffectConnector);
            addEffectRow = std::make_unique<AddEffectRow>("+ Add Effect", [onAddEffectClickedIn]
            {
                if (onAddEffectClickedIn)
                    onAddEffectClickedIn(std::nullopt);
            });
            addAndMakeVisible(*addEffectRow);

            addEffectMenuTrigger = std::make_unique<MenuTriggerButton>();
            addEffectMenuTrigger->onClick = [onAddConditionClickedIn]
            {
                juce::PopupMenu menu;
                menu.addItem(1, "Add Condition");
                menu.showMenuAsync(juce::PopupMenu::Options(), [onAddConditionClickedIn](int result)
                {
                    if (result == 1 && onAddConditionClickedIn)
                        onAddConditionClickedIn();
                });
            };
            addAndMakeVisible(*addEffectMenuTrigger);

            outConnector = std::make_unique<ConnectorComponent>();
            addAndMakeVisible(*outConnector);

            resized();
        }

        void setInputLabel(const juce::String& name)
        {
            inCap.label = "IN - " + name;
            inCap.repaint();
        }

        int getPreferredHeight() const
        {
            int height = margin * 2;
            height += IoCapComponent::capHeight; // IN cap
            for (auto& row : topLevelRows)
                height += ConnectorComponent::connectorHeight + row->getPreferredHeight();
            height += ConnectorComponent::connectorHeight + AddEffectRow::rowHeight;    // trailing connector + Add Effect
            height += ConnectorComponent::connectorHeight + IoCapComponent::capHeight;  // connector + OUT cap
            return height;
        }

        void paint(juce::Graphics& g) override
        {
            // Same white dot-mat texture as the outer EffectChainPanel::paint()
            // -- this Content is what actually sits behind the rows/connectors
            // inside the scrolling viewport, so the outer panel's own paint()
            // never shows through here.
            RawdogLookAndFeel::drawDotMat(g, getLocalBounds(), RawdogLookAndFeel::Palette::get().surface);

            if (draggingIndex < 0)
                return;

            // Insertion-line indicator: find the gap between the STATIC
            // (non-dragged) rows nearest to dropIndex -- the dragged row's
            // own (translated) bounds don't factor into this at all.
            std::vector<juce::Rectangle<int>> staticBounds;
            for (int i = 0; i < (int) topLevelRows.size(); ++i)
                if (i != draggingIndex)
                    staticBounds.push_back(topLevelRows[(size_t) i]->getBounds());

            if (staticBounds.empty())
                return;

            int insertionY;
            if (dropIndex <= 0)
                insertionY = staticBounds.front().getY() - ConnectorComponent::connectorHeight / 2;
            else if (dropIndex >= (int) staticBounds.size())
                insertionY = staticBounds.back().getBottom() + ConnectorComponent::connectorHeight / 2;
            else
                insertionY = (staticBounds[(size_t) dropIndex - 1].getBottom() + staticBounds[(size_t) dropIndex].getY()) / 2;

            g.setColour(RawdogLookAndFeel::Palette::get().ink);
            g.fillRect(margin, insertionY - 1, getWidth() - margin * 2, 2);
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced(margin);

            inCap.setBounds(area.removeFromTop(IoCapComponent::capHeight));

            for (int i = 0; i < (int) topLevelRows.size(); ++i)
            {
                connectors[(size_t) i]->setBounds(area.removeFromTop(ConnectorComponent::connectorHeight));
                topLevelRows[(size_t) i]->setBounds(area.removeFromTop(topLevelRows[(size_t) i]->getPreferredHeight()));
            }

            if (addEffectConnector != nullptr)
                addEffectConnector->setBounds(area.removeFromTop(ConnectorComponent::connectorHeight));
            if (addEffectRow != nullptr)
            {
                auto addEffectArea = area.removeFromTop(AddEffectRow::rowHeight);
                if (addEffectMenuTrigger != nullptr)
                {
                    // Square -- width == the row's own height, rather than a
                    // separately maintained constant that could drift from it.
                    addEffectMenuTrigger->setBounds(addEffectArea.removeFromRight(AddEffectRow::rowHeight));
                    addEffectArea.removeFromRight(4); // gap -- keeps the two dashed boxes visually distinct
                }
                addEffectRow->setBounds(addEffectArea);
            }

            if (outConnector != nullptr)
                outConnector->setBounds(area.removeFromTop(ConnectorComponent::connectorHeight));
            outCap.setBounds(area.removeFromTop(IoCapComponent::capHeight));
        }

    private:
        // Toggles topIndex's membership in expandedConditionalSlots (purely
        // presentational -- see the class comment) and asks the owner to
        // re-derive the rack from the current chain, the same
        // toggle-then-notify idiom PluginListModel's own vendor/preset
        // disclosure triangles use. Doesn't rebuild anything itself: this
        // object has no safe way to hold onto the chain data between
        // rebuild() calls (ChainSlot owns a non-copyable
        // unique_ptr<AudioPluginInstance>), so re-deriving always goes back
        // through MainComponent -> refreshEffectChainPanel() -> rebuild().
        void toggleExpanded(int topIndex)
        {
            if (! expandedConditionalSlots.erase(topIndex))
                expandedConditionalSlots.insert(topIndex);

            if (onExpansionChanged)
                onExpansionChanged();
        }

        // Drag gestures are captured relative to Content's own coordinate
        // space (via e.getEventRelativeTo(this)) and never accumulated as
        // local per-event deltas -- the dragged row's own local origin shifts
        // under it as it moves, so a fixed reference frame is the only safe
        // way to compute its new position.
        void beginDrag(int index, const juce::MouseEvent& e)
        {
            draggingIndex = index;
            dropIndex = index;
            dragStartRowTop = topLevelRows[(size_t) index]->getY();
            dragStartContentY = e.getEventRelativeTo(this).position.y;
            topLevelRows[(size_t) index]->toFront(false);
        }

        void updateDrag(const juce::MouseEvent& e)
        {
            if (draggingIndex < 0)
                return;

            const float currentContentY = e.getEventRelativeTo(this).position.y;
            const int newTop = dragStartRowTop + (int) (currentContentY - dragStartContentY);

            auto* draggedRow = topLevelRows[(size_t) draggingIndex].get();
            draggedRow->setTopLeftPosition(draggedRow->getX(), newTop);

            dropIndex = computeDropIndex();
            repaint();
        }

        void endDrag(const juce::MouseEvent&)
        {
            if (draggingIndex < 0)
                return;

            const int from = draggingIndex;
            const int to = dropIndex;
            draggingIndex = -1;
            dropIndex = -1;

            // A same-position drop never mutates pluginChain, so MainComponent
            // never calls rebuild() for it -- restore this row's static
            // position ourselves rather than relying on a rebuild that isn't
            // coming.
            if (to != from && onReorderSlot)
                onReorderSlot(from, to);
            else
                resized();

            repaint();
        }

        // Compares the dragged row's current (translated) center-Y against
        // the other rows' unchanged, statically-laid-out center-Ys -- the
        // result is directly the `to` index MainComponent::moveChainSlot
        // expects (index in the resulting array), no further adjustment.
        int computeDropIndex() const
        {
            const float draggedCenterY = topLevelRows[(size_t) draggingIndex]->getBounds().toFloat().getCentreY();
            int result = 0;
            for (int i = 0; i < (int) topLevelRows.size(); ++i)
                if (i != draggingIndex && topLevelRows[(size_t) i]->getBounds().toFloat().getCentreY() < draggedCenterY)
                    ++result;
            return result;
        }

        static constexpr int margin = 8;

        IoCapComponent inCap, outCap;
        std::vector<std::unique_ptr<EntryRowComponent>> topLevelRows;
        std::vector<std::unique_ptr<ConnectorComponent>> connectors;
        std::unique_ptr<ConnectorComponent> addEffectConnector;
        std::unique_ptr<AddEffectRow> addEffectRow;
        std::unique_ptr<MenuTriggerButton> addEffectMenuTrigger;
        std::unique_ptr<ConnectorComponent> outConnector;

        // Which top-level conditional slots (by index) currently show their
        // branches -- purely presentational, survives across rebuild() calls
        // since Content itself (unlike its child rows) is never destroyed.
        std::set<int> expandedConditionalSlots;

        std::function<void(int, int)> onReorderSlot;
        std::function<void()> onExpansionChanged;
        int draggingIndex = -1;
        int dropIndex = -1;
        int dragStartRowTop = 0;
        float dragStartContentY = 0.0f;
    };

    juce::Viewport viewport;
    Content content;
    juce::Label chainHeaderLabel, chainCountLabel;
    juce::TextButton applyButton { "Apply" };
};
