#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "ParameterAutomationPanel.h"
#include "RawdogLookAndFeel.h"

// Embeds a plugin's editor in-place (instead of a separate OS popup window),
// wrapped in a Viewport so any editor size/aspect ratio scrolls cleanly rather
// than clipping or being force-resized. A single top strip holds the
// "Editor"/"Automation" tab switcher on the left and "Save as Preset"/Cancel/
// Apply on the right, so the buttons stay put regardless of which tab is
// showing -- Apply always commits whatever the live preview currently
// reflects and Save as Preset always captures the plugin's current parameter
// state, neither is specific to the Editor or Automation tab.
class PluginEditorPanel : public juce::Component
{
public:
    PluginEditorPanel(std::unique_ptr<juce::AudioProcessorEditor> editorIn, juce::AudioProcessor& processor,
                      std::function<void()> onApply, std::function<void()> onCancel,
                      std::function<void()> onAutomationChanged, std::function<void()> onSaveAsPreset)
        : editor(std::move(editorIn)), automationPanel(processor)
    {
        viewport.setViewedComponent(editor.get(), false); // false: editor stays owned by our unique_ptr
        viewport.setScrollBarsShown(true, true); // explicit: show scrollbars whenever the editor's own
                                                  // (unforced) size exceeds the viewport, rather than
                                                  // relying on Viewport's implicit default policy.
        addAndMakeVisible(viewport);
        addChildComponent(automationPanel); // hidden until the Automation tab is selected
        automationPanel.onChanged = std::move(onAutomationChanged);

        addAndMakeVisible(modeTabs);
        modeTabs.onTabChanged = [this](int) { updateTabVisibility(); };
        updateTabVisibility();

        addAndMakeVisible(savePresetButton);
        addAndMakeVisible(cancelButton);
        addAndMakeVisible(applyButton);
        savePresetButton.onClick = std::move(onSaveAsPreset);
        cancelButton.onClick = std::move(onCancel);
        applyButton.onClick = std::move(onApply);

        // Apply is the default/emphasized action of this strip (matching the
        // Platinum mockup's double-border Apply button) -- purely a paint
        // hint, not a toggle state.
        RawdogLookAndFeel::setEmphasized(applyButton);
    }

    int getPreferredWidth() const { return editor->getWidth(); }

    const std::vector<ParameterAutomation>& getParameterRamps() const { return automationPanel.getRamps(); }

    void resized() override
    {
        auto area = getLocalBounds();

        auto topStrip = area.removeFromTop(32).reduced(4);
        auto buttonArea = topStrip.removeFromRight(260);
        applyButton.setBounds(buttonArea.removeFromRight(76).reduced(2, 0));
        cancelButton.setBounds(buttonArea.removeFromRight(76).reduced(2, 0));
        savePresetButton.setBounds(buttonArea.reduced(2, 0));

        modeTabs.setBounds(topStrip);

        viewport.setBounds(area);
        automationPanel.setBounds(area);
    }

private:
    // Tiny standalone TabbedButtonBar, same idiom as LeftColumnPanel's
    // PluginFilterTabs -- we're swapping which single content area is
    // visible, not managing separate content pages.
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
    juce::TextButton cancelButton { "Cancel" };
    juce::TextButton applyButton { "Apply" };
};
