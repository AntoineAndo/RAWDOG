#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <functional>
#include <memory>
#include <vector>
#include "ChainSlot.h"
#include "RawdogLookAndFeel.h"

namespace
{
    // U+2014 EM DASH -- used in the IN/OUT caps' "<label> -- <value>" text,
    // factored out since three separate call sites need the same literal.
    juce::String emDash() { return juce::String(juce::CharPointer_UTF8("\xE2\x80\x94")); }
}

// The effect-chain "rack": a signal-path list read top to bottom -- an IN cap
// (source label), one horizontal-strip row per ChainSlot with a connecting
// line between each adjacent pair, a trailing dashed "+ Add Effect" row (not
// a real ChainSlot -- switches to the plugin browser when clicked), and an
// OUT cap. A fixed "CHAIN" header above the scrolling list shows the slot
// count and the whole-chain commit action ("Apply").
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
// to).
//
// Holds no chain state of its own -- MainComponent owns the real
// std::vector<ChainSlot> and calls rebuild() after every mutation (add,
// remove, reorder, select, bypass-toggle), fully re-deriving the row list
// from scratch on any change rather than trying to incrementally patch
// existing rows.
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

    void rebuild(const std::vector<ChainSlot>& chain, int selectedIndex)
    {
        content.rebuild(chain, selectedIndex, onSelectSlot, onRemoveSlot, onToggleBypass, onReorderSlot,
                         onAddEffectClicked);

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

    // Each fired with the mutated/selected slot's index. MainComponent wires
    // these once in its constructor to addPluginToChain()'s sibling
    // methods -- selectChainSlot()/removeChainSlot()/toggleChainSlotBypass().
    // onReorderSlot fires once per completed drag with the slot's original and
    // final index, wired to moveChainSlot(). onAddEffectClicked (no index --
    // it isn't tied to any slot) is wired to LeftColumnPanel::showPluginsTab(),
    // so clicking the placeholder row switches to the plugin browser to pick
    // one. onApplyClicked is wired to MainComponent::applyClicked() directly.
    std::function<void(int)> onSelectSlot;
    std::function<void(int)> onRemoveSlot;
    std::function<void(int)> onToggleBypass;
    std::function<void(int, int)> onReorderSlot;
    std::function<void()> onAddEffectClicked;
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
    // bypass via the onToggleBypass callback.
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

    // One horizontal strip per chain slot. Only overrides mouseUp (not
    // mouseDown) to treat a click
    // on the row itself as "select this slot" -- JUCE routes a click that
    // lands on a child (checkbox/grip/remove) to that child instead, so this
    // never double-fires; the child labels have setInterceptsMouseClicks(false,
    // false) so clicks on them still fall through to this row.
    struct RowComponent : public juce::Component
    {
        RowComponent(int indexIn, const juce::String& name, bool bypassedIn, bool isSelected,
                     const std::function<void(int)>& onSelectIn, const std::function<void(int)>& onRemoveIn,
                     const std::function<void(int)>& onToggleBypassIn,
                     std::function<void(int, const juce::MouseEvent&)> onGripDownIn,
                     std::function<void(const juce::MouseEvent&)> onGripDragIn,
                     std::function<void(const juce::MouseEvent&)> onGripUpIn)
            : index(indexIn), selected(isSelected), onSelect(onSelectIn)
        {
            const auto& palette = RawdogLookAndFeel::Palette::get();
            const auto textColour = selected ? palette.selectedFg : (bypassedIn ? palette.inkMuted : palette.ink);

            setMouseCursor(juce::MouseCursor::PointingHandCursor);

            indexLabel.setText(juce::String(indexIn + 1).paddedLeft('0', 2), juce::dontSendNotification);
            indexLabel.setFont(RawdogLookAndFeel::chromeFont(8.0f));
            indexLabel.setJustificationType(juce::Justification::centred);
            indexLabel.setInterceptsMouseClicks(false, false);
            indexLabel.setColour(juce::Label::textColourId, selected ? palette.selectedFg : palette.inkMuted);
            addAndMakeVisible(indexLabel);

            checkbox.checked = ! bypassedIn;
            checkbox.onToggle = [onToggleBypassIn, indexIn] { if (onToggleBypassIn) onToggleBypassIn(indexIn); };
            addAndMakeVisible(checkbox);

            nameLabel.setText(name, juce::dontSendNotification);
            nameLabel.setFont(RawdogLookAndFeel::chromeFont(11.0f));
            nameLabel.setMinimumHorizontalScale(0.6f); // shrinks-to-fit rather than truncating a long plugin name outright
            nameLabel.setInterceptsMouseClicks(false, false);
            nameLabel.setColour(juce::Label::textColourId, textColour);
            addAndMakeVisible(nameLabel);

            stateLabel.setText(bypassedIn ? "bypassed" : juce::String(), juce::dontSendNotification);
            stateLabel.setFont(RawdogLookAndFeel::chromeFont(8.0f));
            stateLabel.setJustificationType(juce::Justification::centredRight);
            stateLabel.setInterceptsMouseClicks(false, false);
            stateLabel.setColour(juce::Label::textColourId, selected ? palette.selectedFg : palette.inkMuted);
            addAndMakeVisible(stateLabel);

            grip.onGripDown = [onGripDownIn, indexIn](const juce::MouseEvent& e) { if (onGripDownIn) onGripDownIn(indexIn, e); };
            grip.onGripDrag = std::move(onGripDragIn);
            grip.onGripUp = std::move(onGripUpIn);
            addAndMakeVisible(grip);

            removeButton.setButtonText(juce::CharPointer_UTF8("\xC3\x97")); // multiplication sign, reused as "x"
            removeButton.setTooltip("Remove this effect from the chain");
            removeButton.onClick = [onRemoveIn, indexIn] { if (onRemoveIn) onRemoveIn(indexIn); };
            addAndMakeVisible(removeButton);
        }

        void mouseUp(const juce::MouseEvent& e) override
        {
            if (isEnabled() && contains(e.getPosition()) && onSelect)
                onSelect(index);
        }

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
            stateLabel.setBounds(area.removeFromRight(56));

            nameLabel.setBounds(area);
        }

        static constexpr int rowHeight = 30;

    private:
        // The row's own bounds trimmed by shadowOffset on the right/bottom --
        // shared by paint() (the actual box fill/border, with the untrimmed
        // full bounds behind it reading as the shadow strip) and resized()
        // (so child controls stay inset within the visible box rather than
        // overlapping the shadow).
        static constexpr int shadowOffset = 2;
        juce::Rectangle<int> boxBounds() const { return getLocalBounds().withTrimmedRight(shadowOffset).withTrimmedBottom(shadowOffset); }

        int index;
        bool selected;
        std::function<void(int)> onSelect;
        juce::Label indexLabel, nameLabel, stateLabel;
        CheckboxComponent checkbox;
        GripHandle grip;
        juce::TextButton removeButton;
    };

    // The trailing placeholder row -- not a real chain slot, just an action.
    // Styled with a dashed border and muted "+" glyph so it reads as an empty
    // slot to fill rather than another effect.
    struct AddEffectRow : public juce::Component
    {
        explicit AddEffectRow(std::function<void()> onClickIn) : onClick(std::move(onClickIn))
        {
            setMouseCursor(juce::MouseCursor::PointingHandCursor);
        }

        void mouseUp(const juce::MouseEvent& e) override
        {
            if (isEnabled() && contains(e.getPosition()) && onClick)
                onClick();
        }

        void paint(juce::Graphics& g) override
        {
            const auto& palette = RawdogLookAndFeel::Palette::get();
            const bool enabled = isEnabled();
            const auto lineColour = enabled ? palette.inkMuted : palette.divider;
            auto bounds = getLocalBounds().toFloat().reduced(1.0f);

            // Flatter, more uniformly muted block when disabled -- a crisp
            // white fill read as "still interactive" even with dimmed
            // border/text, matching the design mockup's disabled treatment.
            g.setColour(enabled ? palette.surface : palette.windowBg);
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
            g.drawText("+ Add Effect", getLocalBounds(), juce::Justification::centred);
        }

        std::function<void()> onClick;

        static constexpr int rowHeight = 28;
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

    // The scrollable body: an IN cap, one connector+row pair per chain slot
    // (connector first, so there's one right after the IN cap too), a
    // trailing connector + Add Effect row, then a connector + OUT cap -- all
    // built and laid out in ascending chain-index order (top to bottom).
    class Content : public juce::Component
    {
    public:
        Content()
        {
            inCap.label = "IN " + emDash() + " NO IMAGE"; // overwritten by setInputLabel() once a real image loads
            addAndMakeVisible(inCap);
            outCap.label = "OUT " + emDash() + " preview";
            addAndMakeVisible(outCap);
        }

        void rebuild(const std::vector<ChainSlot>& chain, int selectedIndex,
                     const std::function<void(int)>& onSelectIn, const std::function<void(int)>& onRemoveIn,
                     const std::function<void(int)>& onToggleBypassIn,
                     const std::function<void(int, int)>& onReorderIn,
                     const std::function<void()>& onAddEffectClickedIn)
        {
            // Drag state can never survive a rebuild -- either the drag just
            // committed a reorder (which is what triggered this rebuild), or
            // this rebuild is unrelated and any in-flight drag's rows are
            // about to be destroyed anyway.
            draggingIndex = -1;
            dropIndex = -1;
            onReorderSlot = onReorderIn;

            rows.clear();
            connectors.clear();

            for (int i = 0; i < (int) chain.size(); ++i)
            {
                auto connector = std::make_unique<ConnectorComponent>();
                addAndMakeVisible(*connector);
                connectors.push_back(std::move(connector));

                const auto& slot = chain[(size_t) i];
                auto row = std::make_unique<RowComponent>(
                    i, slot.plugin->getName(), slot.bypassed, i == selectedIndex, onSelectIn, onRemoveIn,
                    onToggleBypassIn,
                    [this](int idx, const juce::MouseEvent& e) { beginDrag(idx, e); },
                    [this](const juce::MouseEvent& e) { updateDrag(e); },
                    [this](const juce::MouseEvent& e) { endDrag(e); });
                addAndMakeVisible(*row);
                rows.push_back(std::move(row));
            }

            addEffectConnector = std::make_unique<ConnectorComponent>();
            addAndMakeVisible(*addEffectConnector);

            addEffectRow = std::make_unique<AddEffectRow>(onAddEffectClickedIn);
            addAndMakeVisible(*addEffectRow);

            outConnector = std::make_unique<ConnectorComponent>();
            addAndMakeVisible(*outConnector);

            resized();
        }

        void setInputLabel(const juce::String& name)
        {
            inCap.label = "IN " + emDash() + " " + name;
            inCap.repaint();
        }

        int getPreferredHeight() const
        {
            int height = margin * 2;
            height += IoCapComponent::capHeight; // IN cap
            height += (int) rows.size() * (ConnectorComponent::connectorHeight + RowComponent::rowHeight);
            height += ConnectorComponent::connectorHeight + AddEffectRow::rowHeight; // trailing connector + Add Effect
            height += ConnectorComponent::connectorHeight + IoCapComponent::capHeight; // connector + OUT cap
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
            g.fillRect(margin, insertionY - 1, getWidth() - margin * 2, 2);
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced(margin);

            inCap.setBounds(area.removeFromTop(IoCapComponent::capHeight));

            for (int i = 0; i < (int) rows.size(); ++i)
            {
                connectors[(size_t) i]->setBounds(area.removeFromTop(ConnectorComponent::connectorHeight));
                rows[(size_t) i]->setBounds(area.removeFromTop(RowComponent::rowHeight));
            }

            if (addEffectConnector != nullptr)
                addEffectConnector->setBounds(area.removeFromTop(ConnectorComponent::connectorHeight));
            if (addEffectRow != nullptr)
                addEffectRow->setBounds(area.removeFromTop(AddEffectRow::rowHeight));

            if (outConnector != nullptr)
                outConnector->setBounds(area.removeFromTop(ConnectorComponent::connectorHeight));
            outCap.setBounds(area.removeFromTop(IoCapComponent::capHeight));
        }

    private:
        // Drag gestures are captured relative to Content's own coordinate
        // space (via e.getEventRelativeTo(this)) and never accumulated as
        // local per-event deltas -- the dragged row's own local origin shifts
        // under it as it moves, so a fixed reference frame is the only safe
        // way to compute its new position.
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
            const float draggedCenterY = rows[(size_t) draggingIndex]->getBounds().toFloat().getCentreY();
            int result = 0;
            for (int i = 0; i < (int) rows.size(); ++i)
                if (i != draggingIndex && rows[(size_t) i]->getBounds().toFloat().getCentreY() < draggedCenterY)
                    ++result;
            return result;
        }

        static constexpr int margin = 8;

        IoCapComponent inCap, outCap;
        std::vector<std::unique_ptr<RowComponent>> rows;
        std::vector<std::unique_ptr<ConnectorComponent>> connectors;
        std::unique_ptr<ConnectorComponent> addEffectConnector;
        std::unique_ptr<AddEffectRow> addEffectRow;
        std::unique_ptr<ConnectorComponent> outConnector;

        std::function<void(int, int)> onReorderSlot;
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
