#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>

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
