#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

// Persists the set of favourite plugins (by juce::PluginDescription::createIdentifierString(),
// stable across machines/file locations) as a single newline-joined string in an
// juce::ApplicationProperties-backed settings file. Writes immediately
// (millisecondsBeforeSaving = 0) so a favourite toggle survives even a hard app quit
// right after clicking. It's a single global set, not keyed by anything else.
class FavouritePluginsStore
{
public:
    FavouritePluginsStore()
    {
        juce::PropertiesFile::Options options;
        options.applicationName = "RAWDOG";
        options.filenameSuffix = "settings";
        options.folderName = "RAWDOG";
        options.osxLibrarySubFolder = "Application Support";
        options.millisecondsBeforeSaving = 0; // write immediately
        appProperties.setStorageParameters(options);

        favourites.addTokens(appProperties.getUserSettings()->getValue("favouritePlugins"), "\n", "");
        favourites.removeEmptyStrings();
    }

    bool isFavourite(const juce::String& pluginIdentifier) const { return favourites.contains(pluginIdentifier); }

    void setFavourite(const juce::String& pluginIdentifier, bool shouldBeFavourite)
    {
        if (shouldBeFavourite == favourites.contains(pluginIdentifier))
            return;

        if (shouldBeFavourite)
            favourites.add(pluginIdentifier);
        else
            favourites.removeString(pluginIdentifier);

        appProperties.getUserSettings()->setValue("favouritePlugins", favourites.joinIntoString("\n"));
        appProperties.saveIfNeeded();
    }

private:
    juce::ApplicationProperties appProperties;
    juce::StringArray favourites;
};
