#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "ParameterAutomationPanel.h"
#include "RawdogLookAndFeel.h"

// Embeds a plugin's editor in-place, wrapped in a Viewport so any editor
// size/aspect ratio scrolls cleanly rather than clipping or being
// force-resized. A single top strip holds the "Editor"/"Automation" tab
// switcher on the left and "Save as Preset"/OK on the right, so the buttons
// stay put regardless of which tab is showing.
// This panel only ever represents ONE chain slot -- Apply (which commits the
// *whole* chain) lives on EffectChainPanel instead, since it isn't specific
// to whichever slot happens to be open here:
// - OK just stops showing this slot's editor (deselects it) -- the slot
//   stays in the chain, still live-previewed, untouched.
// - Save as Preset captures only this slot's own parameter state.
// There is deliberately no Cancel button here: removing a slot from the chain
// entirely is handled by the rack's own per-row "x" button (visible the whole
// time this panel is open), so a second control for the same action would be
// redundant. The Escape-key shortcut (MainComponent::cancelEditorCommand) is
// wired independently, straight to MainComponent::perform().
class PluginEditorPanel : public juce::Component
{
public:
    // seedRamps carries over an already-open chain slot's own ramps when its
    // editor is being remounted after a different slot was selected for a
    // while -- see ChainSlot::ramps/MainComponent::selectChainSlot(). Empty
    // for a brand-new slot.
    PluginEditorPanel(std::unique_ptr<juce::AudioProcessorEditor> editorIn, juce::AudioProcessor& processor,
                      std::function<void()> onOk, std::function<void()> onAutomationChanged,
                      std::function<void()> onSaveAsPreset, std::vector<ParameterAutomation> seedRamps = {})
        : editor(std::move(editorIn)), automationPanel(processor, std::move(seedRamps))
    {
        viewport.setViewedComponent(editor.get(), false); // false: editor stays owned by our unique_ptr
        viewport.setScrollBarsShown(true, true); // explicit: show scrollbars whenever the editor's own
                                                  // (unforced) size exceeds the viewport.
        addAndMakeVisible(viewport);
        addChildComponent(automationPanel); // hidden until the Automation tab is selected
        automationPanel.onChanged = std::move(onAutomationChanged);

        addAndMakeVisible(modeTabs);
        modeTabs.onTabChanged = [this](int) { updateTabVisibility(); };
        updateTabVisibility();

        addAndMakeVisible(savePresetButton);
        addAndMakeVisible(okButton);
        savePresetButton.onClick = std::move(onSaveAsPreset);
        okButton.onClick = std::move(onOk);

        savePresetButton.setTooltip("Saves this effect's own settings -- not the whole chain.");
        okButton.setTooltip("Stop editing this effect -- it stays in the chain, still live.");

        // OK is the default/emphasized action of this strip (matching the
        // Platinum mockup's double-border button treatment) -- purely a
        // paint hint, not a toggle state.
        RawdogLookAndFeel::setEmphasized(okButton);
    }

    int getPreferredWidth() const { return editor->getWidth(); }

    const std::vector<ParameterAutomation>& getParameterRamps() const { return automationPanel.getRamps(); }

    void resized() override
    {
        auto area = getLocalBounds();

        auto topStrip = area.removeFromTop(32).reduced(4);
        auto buttonArea = topStrip.removeFromRight(184);
        okButton.setBounds(buttonArea.removeFromRight(76).reduced(2, 0));
        savePresetButton.setBounds(buttonArea.reduced(2, 0));

        modeTabs.setBounds(topStrip);

        viewport.setBounds(area);
        automationPanel.setBounds(area);
    }

private:
    // Tiny standalone TabbedButtonBar: it only swaps which single content
    // area is visible, so it doesn't need to manage separate content pages.
    class ModeTabs : public juce::TabbedButtonBar
    {
    public:
        ModeTabs() : TabbedButtonBar(TabsAtTop)
        {
            addTab("Editor", juce::Colours::transparentBlack, 0);
            addTab("Automation", juce::Colours::transparentBlack, 1);
        }

        std::function<void(int)> onTabChanged;

    private:
        void currentTabChanged(int newIndex, const juce::String&) override
        {
            if (onTabChanged)
                onTabChanged(newIndex);
        }
    };

    void updateTabVisibility()
    {
        const bool showEditor = modeTabs.getCurrentTabIndex() == 0;
        viewport.setVisible(showEditor);
        automationPanel.setVisible(! showEditor);
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor;
    juce::Viewport viewport;
    ModeTabs modeTabs;
    ParameterAutomationPanel automationPanel;
    juce::TextButton savePresetButton { "Save as Preset" };
    juce::TextButton okButton { "OK" };
};
