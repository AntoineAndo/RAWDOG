#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "RawdogPropertiesFile.h"

// Persists whether dark mode is enabled, in its own juce::PropertiesFile-
// backed settings file (via RawdogPropertiesFile). Same write-immediately
// pattern as PluginEnablementStore/ExportSettingsStore -- filenameSuffix
// must stay distinct from every other store that isn't meant to share its
// file, since RawdogPropertiesFile hands out one shared instance per suffix.
class AppearanceSettingsStore
{
public:
    AppearanceSettingsStore() : propertiesFile(RawdogPropertiesFile::forSuffix("appearance")) {}

    bool isDarkModeEnabled()
    {
        return propertiesFile.getBoolValue("darkModeEnabled", false);
    }

    void setDarkModeEnabled(bool shouldBeEnabled)
    {
        propertiesFile.setValue("darkModeEnabled", shouldBeEnabled);
        if (! propertiesFile.saveIfNeeded())
            DBG("AppearanceSettingsStore: failed to save settings file");
    }

private:
    juce::PropertiesFile& propertiesFile;
};
