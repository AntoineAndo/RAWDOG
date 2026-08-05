#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "RawdogPropertiesFile.h"

// Persists the folder the user last exported an image to (as a full path
// string) in its own juce::PropertiesFile-backed settings file (via
// RawdogPropertiesFile), so a later File > Export Image... starts back
// where the user left off rather than always defaulting to Documents.
// Writes immediately (millisecondsBeforeSaving = 0) so the choice survives
// a hard app quit right after exporting. filenameSuffix must stay distinct
// from FavouritePluginsStore/PluginPresetsStore: RawdogPropertiesFile hands
// out one shared instance per suffix, and sharing a suffix would point this
// store at the same underlying file as those two.
class ExportSettingsStore
{
public:
    ExportSettingsStore() : propertiesFile(RawdogPropertiesFile::forSuffix("exportsettings")) {}

    // Documents if nothing's been remembered yet, or the remembered folder no
    // longer exists (e.g. an external drive that's since been unmounted).
    juce::File getLastExportDirectory()
    {
        const auto stored = propertiesFile.getValue("lastExportDirectory");

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
        propertiesFile.setValue("lastExportDirectory", directory.getFullPathName());
        if (! propertiesFile.saveIfNeeded())
            DBG("ExportSettingsStore: failed to save settings file");
    }

private:
    juce::PropertiesFile& propertiesFile;
};
