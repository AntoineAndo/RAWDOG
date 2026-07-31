#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

// Persists the folder the user last exported an image to (as a full path
// string) in its own juce::ApplicationProperties-backed settings file, so a
// later File > Export Image... starts back where the user left off rather
// than always defaulting to Documents. Writes immediately
// (millisecondsBeforeSaving = 0), same treatment as FavouritePluginsStore. A
// distinct filenameSuffix from FavouritePluginsStore/PluginPresetsStore keeps
// this in its own file rather than a second juce::ApplicationProperties
// instance racing another one over the same underlying file.
class ExportSettingsStore
{
public:
    ExportSettingsStore()
    {
        juce::PropertiesFile::Options options;
        options.applicationName = "RAWDOG";
        options.filenameSuffix = "exportsettings";
        options.folderName = "RAWDOG";
        options.osxLibrarySubFolder = "Application Support";
        options.millisecondsBeforeSaving = 0; // write immediately
        appProperties.setStorageParameters(options);
    }

    // Documents if nothing's been remembered yet, or the remembered folder no
    // longer exists (e.g. an external drive that's since been unmounted).
    juce::File getLastExportDirectory()
    {
        const auto stored = appProperties.getUserSettings()->getValue("lastExportDirectory");

        if (stored.isNotEmpty())
        {
            juce::File dir(stored);
            if (dir.isDirectory())
                return dir;
        }

        return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    }

    void setLastExportDirectory(const juce::File& directory)
    {
        appProperties.getUserSettings()->setValue("lastExportDirectory", directory.getFullPathName());
        appProperties.saveIfNeeded();
    }

private:
    juce::ApplicationProperties appProperties;
};
