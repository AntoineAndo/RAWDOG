#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "ParameterAutomationPanel.h"

// Embeds a plugin's editor in-place (instead of a separate OS popup window),
// wrapped in a Viewport so any editor size/aspect ratio scrolls cleanly rather
// than clipping or being force-resized, with an "Apply" button docked underneath.
// A small "Editor"/"Automation" tab strip at the top swaps between the native
// editor and a ParameterAutomationPanel for defining fade-in/fade-out ramps on
// the plugin's own parameters -- Apply/Cancel stay docked at the bottom either
// way, since Apply always commits whatever the live preview currently reflects.
class PluginEditorPanel : public juce::Component
{
public:
    PluginEditorPanel(std::unique_ptr<juce::AudioProcessorEditor> editorIn, juce::AudioProcessor& processor,
                      std::function<void()> onApply, std::function<void()> onCancel,
                      std::function<void()> onAutomationChanged)
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

        addAndMakeVisible(cancelButton);
        addAndMakeVisible(applyButton);
        cancelButton.onClick = std::move(onCancel);
        applyButton.onClick = std::move(onApply);
    }

    int getPreferredWidth() const { return editor->getWidth(); }

    const std::vector<ParameterAutomation>& getParameterRamps() const { return automationPanel.getRamps(); }

    void resized() override
    {
        auto area = getLocalBounds();
        auto buttonStrip = area.removeFromBottom(40).reduced(8);
        applyButton.setBounds(buttonStrip.removeFromRight(buttonStrip.getWidth() / 2).reduced(4, 0));
        cancelButton.setBounds(buttonStrip.reduced(4, 0));

        modeTabs.setBounds(area.removeFromTop(28));

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
    juce::TextButton cancelButton { "Cancel" };
    juce::TextButton applyButton { "Apply" };
};
