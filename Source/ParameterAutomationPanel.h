#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <functional>
#include <vector>
#include "ParameterAutomation.h"

// Lets the user pick one or more of a plugin's own parameters and, for each,
// define one or more ramp segments (initial value, target value, duration,
// easing) -- so Apply can fade a parameter in (and, with a second segment
// near the end, fade it back out) across a selection instead of holding it
// static for the whole pass. Owns the ParameterAutomation list directly (the
// single source of truth -- MainComponent reads it via getRamps() rather than
// keeping its own copy), so it's automatically discarded whenever the owning
// PluginEditorPanel is (Apply or Cancel).
class ParameterAutomationPanel : public juce::Component,
                                  private juce::AsyncUpdater
{
public:
    explicit ParameterAutomationPanel(juce::AudioProcessor& processorIn) : processor(processorIn)
    {
        addAndMakeVisible(addParameterButton);
        addParameterButton.onClick = [this] { showAddParameterMenu(); };

        viewport.setViewedComponent(&content, false);
        viewport.setScrollBarsShown(true, false);
        addAndMakeVisible(viewport);

        rebuild();
    }

    const std::vector<ParameterAutomation>& getRamps() const { return automations; }

    // Fired (debounced -- see handleAsyncUpdate below) after any edit, so the
    // owner can refresh its live preview without recomputing per keystroke.
    std::function<void()> onChanged;

    void resized() override
    {
        auto area = getLocalBounds();
        addParameterButton.setBounds(area.removeFromTop(28).reduced(4));
        viewport.setBounds(area);

        // setSize() below only calls Content::resized() if the size actually
        // changed -- but a structural rebuild (add/remove parameter or
        // segment) can add freshly-constructed child components that have
        // never been positioned, even when the *overall* preferred height
        // happens to land back on the same value. The explicit call after
        // makes layout unconditional, so a stale/zero-sized row can't survive
        // a rebuild.
        content.setSize(viewport.getMaximumVisibleWidth(), content.getPreferredHeight());
        content.resized();
    }

private:
    // One row per ramp segment, in two tiers so the range slider gets the
    // full row width: a start/end range slider on its own top row, then
    // initial value / target value / easing / remove underneath.
    struct SegmentRow : public juce::Component
    {
        static constexpr int sliderRowHeight = 28;
        static constexpr int labelRowHeight = 14;
        static constexpr int fieldsRowHeight = 26;
        static constexpr int totalHeight = sliderRowHeight + labelRowHeight + fieldsRowHeight;

        SegmentRow(RampSegment& segmentIn, std::function<void()> onAnyChangeIn, std::function<void()> onRemoveIn)
            : segment(segmentIn), onAnyChange(std::move(onAnyChangeIn))
        {
            // Declared before rangeSlider so it's constructed first and
            // destroyed last -- the slider must never outlive the look and
            // feel it's pointing at.
            rangeSlider.setLookAndFeel(&rangeLookAndFeel);
            rangeSlider.setSliderStyle(juce::Slider::TwoValueHorizontal);
            rangeSlider.setRange(0.0, 100.0, 0.1);
            rangeSlider.setMinAndMaxValues(segment.startFraction * 100.0, segment.endFraction * 100.0, juce::dontSendNotification);
            rangeSlider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
            rangeSlider.setTooltip("Orange handle: start; blue handle: end -- both as a % of the selection's length");
            rangeSlider.onValueChange = [this]
            {
                segment.startFraction = rangeSlider.getMinValue() / 100.0;
                segment.endFraction = rangeSlider.getMaxValue() / 100.0;
                updateRangeLabel();
                notify();
            };
            addAndMakeVisible(rangeSlider);

            rangeLabel.setJustificationType(juce::Justification::centred);
            rangeLabel.setFont(juce::Font(juce::FontOptions(11.0f)));
            addAndMakeVisible(rangeLabel);
            updateRangeLabel();

            setUpColumnLabel(initialColumnLabel, "Initial");
            setUpColumnLabel(targetColumnLabel, "Target");
            setUpColumnLabel(easingColumnLabel, "Easing");

            setUpNumeric(initialValueEditor, (double) segment.initialValue, "Initial value (0-1)");
            setUpNumeric(targetValueEditor, (double) segment.targetValue, "Target value (0-1)");

            initialValueEditor.onTextChange = [this] { segment.initialValue = (float) juce::jlimit(0.0, 1.0, initialValueEditor.getText().getDoubleValue()); notify(); };
            targetValueEditor.onTextChange = [this] { segment.targetValue = (float) juce::jlimit(0.0, 1.0, targetValueEditor.getText().getDoubleValue()); notify(); };

            easingCombo.addItem("Linear", 1);
            easingCombo.addItem("Ease In", 2);
            easingCombo.addItem("Ease Out", 3);
            easingCombo.addItem("Ease In/Out", 4);
            easingCombo.setSelectedId(1 + (int) segment.easing, juce::dontSendNotification);
            easingCombo.onChange = [this] { segment.easing = (Easing) (easingCombo.getSelectedId() - 1); notify(); };
            addAndMakeVisible(easingCombo);

            removeButton.setButtonText("x");
            removeButton.onClick = std::move(onRemoveIn);
            addAndMakeVisible(removeButton);
        }

        void resized() override
        {
            auto area = getLocalBounds();

            auto sliderRow = area.removeFromTop(sliderRowHeight);
            rangeLabel.setBounds(sliderRow.removeFromRight(90).reduced(2));
            rangeSlider.setBounds(sliderRow.reduced(2));

            auto labelRow = area.removeFromTop(labelRowHeight);
            const int labelWidth = labelRow.getWidth() / 4;
            initialColumnLabel.setBounds(labelRow.removeFromLeft(labelWidth));
            targetColumnLabel.setBounds(labelRow.removeFromLeft(labelWidth));
            easingColumnLabel.setBounds(labelRow.removeFromLeft(labelWidth));
            // fourth column (over the remove button) intentionally left blank

            auto fieldsRow = area;
            const int fieldWidth = fieldsRow.getWidth() / 4;
            initialValueEditor.setBounds(fieldsRow.removeFromLeft(fieldWidth).reduced(2));
            targetValueEditor.setBounds(fieldsRow.removeFromLeft(fieldWidth).reduced(2));
            easingCombo.setBounds(fieldsRow.removeFromLeft(fieldWidth).reduced(2));
            removeButton.setBounds(fieldsRow.reduced(2));
        }

        RampSegment& segment;
        std::function<void()> onAnyChange;

    private:
        // Gives the range slider's two handles distinct colours (start vs.
        // end) -- JUCE's default TwoValueHorizontal drawing uses the same
        // thumbColourId for both, differing only by pointer direction, which
        // isn't enough to tell them apart at a glance. Scoped to just this
        // one Slider instance via setLookAndFeel(), not installed globally.
        struct RangeSliderLookAndFeel : public juce::LookAndFeel_V4
        {
            void drawLinearSliderThumb(juce::Graphics& g, int, int y, int, int height,
                                        float, float minSliderPos, float maxSliderPos,
                                        juce::Slider::SliderStyle, juce::Slider&) override
            {
                constexpr float radius = 6.0f;
                const float centreY = (float) y + (float) height * 0.5f;

                g.setColour(juce::Colours::orange);
                g.fillEllipse(minSliderPos - radius, centreY - radius, radius * 2.0f, radius * 2.0f);

                g.setColour(juce::Colours::cyan);
                g.fillEllipse(maxSliderPos - radius, centreY - radius, radius * 2.0f, radius * 2.0f);
            }
        };

        void notify() { if (onAnyChange) onAnyChange(); }

        void updateRangeLabel()
        {
            rangeLabel.setText(juce::String(rangeSlider.getMinValue(), 1) + "-" + juce::String(rangeSlider.getMaxValue(), 1) + "%",
                                juce::dontSendNotification);
        }

        void setUpColumnLabel(juce::Label& label, const char* text)
        {
            label.setText(text, juce::dontSendNotification);
            label.setFont(juce::Font(juce::FontOptions(11.0f)));
            label.setColour(juce::Label::textColourId, juce::Colours::grey);
            label.setJustificationType(juce::Justification::centred);
            addAndMakeVisible(label);
        }

        void setUpNumeric(juce::TextEditor& editor, double initial, const char* tooltip)
        {
            editor.setInputRestrictions(10, "0123456789.");
            editor.setText(juce::String(initial), juce::dontSendNotification);
            editor.setTooltip(tooltip);
            addAndMakeVisible(editor);
        }

        RangeSliderLookAndFeel rangeLookAndFeel;
        juce::Slider rangeSlider;
        juce::Label rangeLabel;
        juce::Label initialColumnLabel, targetColumnLabel, easingColumnLabel;
        juce::TextEditor initialValueEditor, targetValueEditor;
        juce::ComboBox easingCombo;
        juce::TextButton removeButton;
    };

    // One block per automated parameter: name + remove button, then its
    // segment rows and an "+ segment" button. onAnyChange is shared for both
    // plain value edits (a segment field changed) and structural changes
    // (a segment was added/removed) -- either way the owning Content just
    // needs to re-layout (heights may have changed) and debounce a preview
    // refresh, so one callback covers both.
    struct ParameterBlock : public juce::Component
    {
        ParameterBlock(ParameterAutomation& automationIn, juce::AudioProcessor& processorIn,
                       std::function<void()> onAnyChangeIn, std::function<void()> onRemoveParameterIn)
            : automation(automationIn), onAnyChange(std::move(onAnyChangeIn))
        {
            auto* param = processorIn.getParameters()[automation.parameterIndex]; // bounds-checked, nullptr if stale
            nameLabel.setText(param != nullptr ? param->getName(128) : "(unknown parameter)", juce::dontSendNotification);
            addAndMakeVisible(nameLabel);

            removeButton.setButtonText("Remove parameter");
            removeButton.onClick = std::move(onRemoveParameterIn);
            addAndMakeVisible(removeButton);

            addSegmentButton.setButtonText("+ segment");
            addSegmentButton.onClick = [this] { addSegment(); };
            addAndMakeVisible(addSegmentButton);

            rebuildSegmentRows();
        }

        int getPreferredHeight() const
        {
            return headerHeight + (int) segmentRows.size() * SegmentRow::totalHeight + footerHeight;
        }

        void resized() override
        {
            auto area = getLocalBounds();

            auto header = area.removeFromTop(headerHeight);
            removeButton.setBounds(header.removeFromRight(120).reduced(2));
            nameLabel.setBounds(header);

            for (auto& row : segmentRows)
                row->setBounds(area.removeFromTop(SegmentRow::totalHeight).reduced(2, 1));

            addSegmentButton.setBounds(area.removeFromTop(footerHeight).reduced(4, 2));
        }

    private:
        static constexpr int headerHeight = 24;
        static constexpr int footerHeight = 24;

        void addSegment()
        {
            // Both ends are fractions of the scope's total length, so a new
            // segment can chain directly onto the previous one's actual end
            // this time (unlike the old absolute-ms scheme) -- start where
            // the last one ended, running for the same span, clamped so it
            // can't run past 100%.
            const double previousEnd = automation.segments.empty() ? 0.0 : automation.segments.back().endFraction;
            const double span = automation.segments.empty() ? 0.1 : (automation.segments.back().endFraction - automation.segments.back().startFraction);
            const double newEnd = juce::jmin(1.0, previousEnd + span);
            automation.segments.push_back({ previousEnd, newEnd, 0.0f, 1.0f, Easing::linear });
            rebuildSegmentRows();
            if (onAnyChange) onAnyChange();
        }

        void removeSegment(int index)
        {
            if (index < 0 || index >= (int) automation.segments.size())
                return;

            automation.segments.erase(automation.segments.begin() + index);
            rebuildSegmentRows();
            if (onAnyChange) onAnyChange();
        }

        void rebuildSegmentRows()
        {
            segmentRows.clear();

            for (int i = 0; i < (int) automation.segments.size(); ++i)
            {
                auto row = std::make_unique<SegmentRow>(automation.segments[(size_t) i], onAnyChange, [this, i] { removeSegment(i); });
                addAndMakeVisible(*row);
                segmentRows.push_back(std::move(row));
            }

            resized();
        }

        ParameterAutomation& automation;
        std::function<void()> onAnyChange;

        juce::Label nameLabel;
        juce::TextButton removeButton, addSegmentButton;
        std::vector<std::unique_ptr<SegmentRow>> segmentRows;
    };

    // The scrollable body: one ParameterBlock per automated parameter.
    class Content : public juce::Component
    {
    public:
        void rebuild(std::vector<ParameterAutomation>& automationsIn, juce::AudioProcessor& processorIn,
                     std::function<void()> onAnyChange, std::function<void(int)> onRemoveParameter)
        {
            blocks.clear();

            for (int i = 0; i < (int) automationsIn.size(); ++i)
            {
                auto block = std::make_unique<ParameterBlock>(automationsIn[(size_t) i], processorIn, onAnyChange,
                                                                [onRemoveParameter, i] { onRemoveParameter(i); });
                addAndMakeVisible(*block);
                blocks.push_back(std::move(block));
            }

            resized();
        }

        int getPreferredHeight() const
        {
            int height = margin;

            for (auto& block : blocks)
                height += block->getPreferredHeight() + blockGap;

            return height + margin;
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced(margin);

            for (auto& block : blocks)
            {
                block->setBounds(area.removeFromTop(block->getPreferredHeight()));
                area.removeFromTop(blockGap);
            }
        }

    private:
        static constexpr int margin = 8;
        static constexpr int blockGap = 12;

        std::vector<std::unique_ptr<ParameterBlock>> blocks;
    };

    void showAddParameterMenu()
    {
        juce::PopupMenu menu;
        auto& params = processor.getParameters();

        for (int i = 0; i < params.size(); ++i)
            if (! isAlreadyAutomated(i))
                menu.addItem(i + 1, params[i]->getName(128));

        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(addParameterButton),
            [this](int result)
            {
                if (result <= 0)
                    return;

                ParameterAutomation automation;
                automation.parameterIndex = result - 1;

                if (auto* param = processor.getParameters()[automation.parameterIndex])
                    automation.originalValue = param->getValue();

                automation.segments.push_back({ 0.0, 0.1, 0.0f, 1.0f, Easing::linear });
                automations.push_back(std::move(automation));

                rebuild();
                relayoutAndNotify();
            });
    }

    bool isAlreadyAutomated(int parameterIndex) const
    {
        for (auto& automation : automations)
            if (automation.parameterIndex == parameterIndex)
                return true;

        return false;
    }

    void removeAutomation(int index)
    {
        if (index < 0 || index >= (int) automations.size())
            return;

        // Ramps drive this parameter via setValueNotifyingHost() and never
        // reset it -- without this, removing the automation would leave the
        // parameter stuck at whatever value the last ramp evaluation left it
        // at, so the preview would look unchanged even though it recomputed.
        if (auto* param = processor.getParameters()[automations[(size_t) index].parameterIndex])
            param->setValueNotifyingHost(automations[(size_t) index].originalValue);

        automations.erase(automations.begin() + index);
        rebuild();
        relayoutAndNotify();
    }

    void rebuild()
    {
        content.rebuild(automations, processor,
                         [this] { relayoutAndNotify(); },
                         [this](int index) { removeAutomation(index); });
    }

    // Re-layout is cheap (just Component bounds) and happens synchronously;
    // onChanged (which drives a full plugin reprocess) is debounced via
    // AsyncUpdater so a burst of edits collapses to one refresh per
    // event-loop turn -- the same idiom PluginParameterWatcher and
    // MainComponent's own selection-drag handling already use.
    void relayoutAndNotify()
    {
        resized(); // unconditionally re-lays-out content too -- see the comment in resized() above
        triggerAsyncUpdate();
    }

    void handleAsyncUpdate() override
    {
        if (onChanged)
            onChanged();
    }

    juce::AudioProcessor& processor;
    std::vector<ParameterAutomation> automations;

    Content content;
    juce::Viewport viewport;
    juce::TextButton addParameterButton { "+ Add automated parameter" };
};
