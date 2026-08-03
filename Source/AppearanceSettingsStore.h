#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

// Persists whether dark mode is enabled, in its own juce::ApplicationProperties-
// backed settings file. Same write-immediately pattern as
// PluginEnablementStore/ExportSettingsStore -- filenameSuffix must stay
// distinct from every other store, since each is a separate
// juce::ApplicationProperties instance pointed at its own file.
class AppearanceSettingsStore
{
public:
    AppearanceSettingsStore()
    {
        juce::PropertiesFile::Options options;
        options.applicationName = "RAWDOG";
        options.filenameSuffix = "appearance";
        options.folderName = "RAWDOG";
        options.osxLibrarySubFolder = "Application Support";
        options.millisecondsBeforeSaving = 0;
        appProperties.setStorageParameters(options);
    }

    bool isDarkModeEnabled()
    {
        return appProperties.getUserSettings()->getBoolValue("darkModeEnabled", false);
    }

    void setDarkModeEnabled(bool shouldBeEnabled)
    {
        appProperties.getUserSettings()->setValue("darkModeEnabled", shouldBeEnabled);
        appProperties.saveIfNeeded();
    }

private:
    juce::ApplicationProperties appProperties;
};
