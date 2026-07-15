#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Scans the standard VST3 and Audio Unit plugin locations and keeps a
// juce::KnownPluginList populated with what it finds.
class PluginScanner
{
public:
    PluginScanner();

    // Scans all registered formats' default search paths synchronously.
    void scanAll();

    juce::KnownPluginList& getKnownPluginList() { return knownPluginList; }
    juce::AudioPluginFormatManager& getFormatManager() { return formatManager; }

private:
    void scanFormat(juce::AudioPluginFormat& format);

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPluginList;
};
