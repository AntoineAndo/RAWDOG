#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginScanner.h"

// M0: lists discovered VST3/AU plugins. No image/audio logic yet.
class MainComponent : public juce::Component
{
public:
    MainComponent();

    void resized() override;

private:
    void refreshList();

    PluginScanner scanner;
    juce::TextButton rescanButton { "Rescan Plugins" };
    juce::ListBox pluginListBox;

    class PluginListModel : public juce::ListBoxModel
    {
    public:
        explicit PluginListModel(juce::KnownPluginList& list) : knownPluginList(list) {}

        void refresh() { cachedTypes = knownPluginList.getTypes(); }

        int getNumRows() override { return cachedTypes.size(); }

        void paintListBoxItem(int rowNumber, juce::Graphics& g,
                               int width, int height, bool rowIsSelected) override;

    private:
        juce::KnownPluginList& knownPluginList;
        juce::Array<juce::PluginDescription> cachedTypes;
    };

    PluginListModel listModel { scanner.getKnownPluginList() };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
