#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

class RawdogPropertiesFile
{
public:
    RawdogPropertiesFile() = delete;

    // Returns the shared juce::PropertiesFile for a given filenameSuffix,
    // constructing it (with this app's standard Options) on first use. Every
    // *Store using the same suffix gets the same instance back, so writes
    // through one are immediately visible to the others and saveIfNeeded()
    // from any of them can't clobber a sibling's unsaved keys.
    static juce::PropertiesFile& forSuffix(const juce::String& filenameSuffix);
};
