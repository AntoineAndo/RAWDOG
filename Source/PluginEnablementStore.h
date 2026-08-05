#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "RawdogPropertiesFile.h"

// Persists which plugins (by juce::PluginDescription::createIdentifierString())
// are excluded from the main editor's plugin list, plus which identifiers have
// already been assigned a default (so a rescan re-finding the same duplicate
// AudioUnit never overwrites a user's manual re-enable). Same
// PropertiesFile-backed, write-immediately pattern as
// FavouritePluginsStore -- two flat StringArrays under their own keys in the
// same shared settings file (see RawdogPropertiesFile).
class PluginEnablementStore
{
public:
    PluginEnablementStore() : propertiesFile(RawdogPropertiesFile::forSuffix("settings"))
    {
        disabledIdentifiers.addTokens(propertiesFile.getValue("disabledPlugins"), "\n", "");
        disabledIdentifiers.removeEmptyStrings();

        seenIdentifiers.addTokens(propertiesFile.getValue("pluginDefaultsSeen"), "\n", "");
        seenIdentifiers.removeEmptyStrings();
    }

    // True for any identifier never explicitly disabled -- a normal,
    // non-duplicate plugin needs no seeding call at all to show up enabled.
    bool isEnabled(const juce::String& pluginIdentifier) const { return ! disabledIdentifiers.contains(pluginIdentifier); }

    void setEnabled(const juce::String& pluginIdentifier, bool shouldBeEnabled)
    {
        if (shouldBeEnabled == isEnabled(pluginIdentifier))
            return;

        if (shouldBeEnabled)
            disabledIdentifiers.removeString(pluginIdentifier);
        else
            disabledIdentifiers.add(pluginIdentifier);

        propertiesFile.setValue("disabledPlugins", disabledIdentifiers.joinIntoString("\n"));
        if (! propertiesFile.saveIfNeeded())
            DBG("PluginEnablementStore: failed to save settings file");
    }

    bool hasDefaultBeenAssigned(const juce::String& pluginIdentifier) const { return seenIdentifiers.contains(pluginIdentifier); }

    void markDefaultAssigned(const juce::String& pluginIdentifier)
    {
        if (seenIdentifiers.contains(pluginIdentifier))
            return;

        seenIdentifiers.add(pluginIdentifier);
        propertiesFile.setValue("pluginDefaultsSeen", seenIdentifiers.joinIntoString("\n"));
        if (! propertiesFile.saveIfNeeded())
            DBG("PluginEnablementStore: failed to save settings file");
    }

private:
    juce::PropertiesFile& propertiesFile;
    juce::StringArray disabledIdentifiers;
    juce::StringArray seenIdentifiers;
};
