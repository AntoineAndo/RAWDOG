#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

// Persists which plugins (by juce::PluginDescription::createIdentifierString())
// are excluded from the main editor's plugin list, plus which identifiers have
// already been assigned a default (so a rescan re-finding the same duplicate
// AudioUnit never overwrites a user's manual re-enable). Same
// ApplicationProperties-backed, write-immediately pattern as
// FavouritePluginsStore -- two flat StringArrays under their own keys in the
// same shared settings file.
class PluginEnablementStore
{
public:
    PluginEnablementStore()
    {
        juce::PropertiesFile::Options options;
        options.applicationName = "RAWDOG";
        options.filenameSuffix = "settings";
        options.folderName = "RAWDOG";
        options.osxLibrarySubFolder = "Application Support";
        options.millisecondsBeforeSaving = 0;
        appProperties.setStorageParameters(options);

        disabledIdentifiers.addTokens(appProperties.getUserSettings()->getValue("disabledPlugins"), "\n", "");
        disabledIdentifiers.removeEmptyStrings();

        seenIdentifiers.addTokens(appProperties.getUserSettings()->getValue("pluginDefaultsSeen"), "\n", "");
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

        appProperties.getUserSettings()->setValue("disabledPlugins", disabledIdentifiers.joinIntoString("\n"));
        appProperties.saveIfNeeded();
    }

    bool hasDefaultBeenAssigned(const juce::String& pluginIdentifier) const { return seenIdentifiers.contains(pluginIdentifier); }

    void markDefaultAssigned(const juce::String& pluginIdentifier)
    {
        if (seenIdentifiers.contains(pluginIdentifier))
            return;

        seenIdentifiers.add(pluginIdentifier);
        appProperties.getUserSettings()->setValue("pluginDefaultsSeen", seenIdentifiers.joinIntoString("\n"));
        appProperties.saveIfNeeded();
    }

private:
    juce::ApplicationProperties appProperties;
    juce::StringArray disabledIdentifiers;
    juce::StringArray seenIdentifiers;
};
