#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "RawdogPropertiesFile.h"

// Persists the set of favourite plugins (by juce::PluginDescription::createIdentifierString(),
// stable across machines/file locations) as a single newline-joined string in an
// juce::PropertiesFile-backed settings file, shared with the other *Store classes
// that also use the "settings" suffix (see RawdogPropertiesFile). Writes immediately
// (millisecondsBeforeSaving = 0) so a favourite toggle survives even a hard app quit
// right after clicking. It's a single global set, not keyed by anything else.
class FavouritePluginsStore
{
public:
    FavouritePluginsStore() : propertiesFile(RawdogPropertiesFile::forSuffix("settings"))
    {
        favourites.addTokens(propertiesFile.getValue("favouritePlugins"), "\n", "");
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

        propertiesFile.setValue("favouritePlugins", favourites.joinIntoString("\n"));
        if (! propertiesFile.saveIfNeeded())
            DBG("FavouritePluginsStore: failed to save settings file");
    }

private:
    juce::PropertiesFile& propertiesFile;
    juce::StringArray favourites;
};
