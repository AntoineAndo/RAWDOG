#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "RawdogPropertiesFile.h"

// Persists whether a selected chain slot's plugin editor opens in its own
// floating window rather than embedded below the effect chain rack. Same
// write-immediately juce::PropertiesFile pattern (via RawdogPropertiesFile)
// as AppearanceSettingsStore/PluginEnablementStore -- filenameSuffix must
// stay distinct from every other store that isn't meant to share its file,
// since RawdogPropertiesFile hands out one shared instance per suffix.
class GeneralSettingsStore
{
public:
    GeneralSettingsStore() : propertiesFile(RawdogPropertiesFile::forSuffix("general")) {}

    bool isPluginWindowModeEnabled()
    {
        return propertiesFile.getBoolValue("pluginWindowModeEnabled", true);
    }

    void setPluginWindowModeEnabled(bool shouldBeEnabled)
    {
        propertiesFile.setValue("pluginWindowModeEnabled", shouldBeEnabled);
        if (! propertiesFile.saveIfNeeded())
            DBG("GeneralSettingsStore: failed to save settings file");
    }

private:
    juce::PropertiesFile& propertiesFile;
};
