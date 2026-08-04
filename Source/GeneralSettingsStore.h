#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

// Persists whether a selected chain slot's plugin editor opens in its own
// floating window rather than embedded below the effect chain rack. Same
// write-immediately juce::ApplicationProperties pattern as
// AppearanceSettingsStore/PluginEnablementStore -- filenameSuffix must stay
// distinct from every other store, since each is a separate
// juce::ApplicationProperties instance pointed at its own file.
class GeneralSettingsStore
{
public:
    GeneralSettingsStore()
    {
        juce::PropertiesFile::Options options;
        options.applicationName = "RAWDOG";
        options.filenameSuffix = "general";
        options.folderName = "RAWDOG";
        options.osxLibrarySubFolder = "Application Support";
        options.millisecondsBeforeSaving = 0;
        appProperties.setStorageParameters(options);
    }

    bool isPluginWindowModeEnabled()
    {
        return appProperties.getUserSettings()->getBoolValue("pluginWindowModeEnabled", true);
    }

    void setPluginWindowModeEnabled(bool shouldBeEnabled)
    {
        appProperties.getUserSettings()->setValue("pluginWindowModeEnabled", shouldBeEnabled);
        appProperties.saveIfNeeded();
    }

private:
    juce::ApplicationProperties appProperties;
};
