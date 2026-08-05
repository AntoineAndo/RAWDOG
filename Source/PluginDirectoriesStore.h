#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginScanner.h"
#include "RawdogPropertiesFile.h"

// Persists the ordered list of directories PluginScanner::scanAll() searches,
// as a single newline-joined string in a juce::PropertiesFile-backed
// settings file shared with the other *Store classes using the "settings"
// suffix (see RawdogPropertiesFile). Seeded on first-ever run with
// PluginScanner::getUnionOfDefaultLocations() so the OS-default VST3/AU
// folders are already in the list as regular, removable entries - not a
// separate hardcoded set layered underneath. Writes immediately
// (millisecondsBeforeSaving = 0), same as every other *Store in this app.
class PluginDirectoriesStore
{
public:
    PluginDirectoriesStore() : propertiesFile(RawdogPropertiesFile::forSuffix("settings"))
    {
        // containsKey(), not "is the stored value empty" - a user who
        // deliberately removes every directory must not have the defaults
        // silently reappear on the next launch.
        if (propertiesFile.containsKey("pluginDirectories"))
        {
            directories.addTokens(propertiesFile.getValue("pluginDirectories"), "\n", "");
            directories.removeEmptyStrings();
        }
        else
        {
            const auto defaults = PluginScanner::getUnionOfDefaultLocations();
            for (int i = 0; i < defaults.getNumPaths(); ++i)
                directories.add(defaults.getRawString(i));
            save();
        }
    }

    juce::StringArray getDirectories() const { return directories; }

    void addDirectory(const juce::File& dir)
    {
        const auto path = dir.getFullPathName();
        if (directories.contains(path))
            return;

        directories.add(path);
        save();
    }

    void removeDirectory(const juce::String& path)
    {
        if (! directories.contains(path))
            return;

        directories.removeString(path);
        save();
    }

    // Discards every custom directory and restores just the OS defaults --
    // same paths the constructor seeds on first-ever run, computed fresh
    // rather than cached, so it stays correct if the OS-default locations
    // ever change between app versions.
    void resetToDefaults()
    {
        directories.clear();

        const auto defaults = PluginScanner::getUnionOfDefaultLocations();
        for (int i = 0; i < defaults.getNumPaths(); ++i)
            directories.add(defaults.getRawString(i));

        save();
    }

    juce::FileSearchPath getAsSearchPath() const
    {
        juce::FileSearchPath path;
        for (const auto& dir : directories)
            path.addIfNotAlreadyThere(juce::File(dir));
        return path;
    }

private:
    void save()
    {
        propertiesFile.setValue("pluginDirectories", directories.joinIntoString("\n"));
        if (! propertiesFile.saveIfNeeded())
            DBG("PluginDirectoriesStore: failed to save settings file");
    }

    juce::PropertiesFile& propertiesFile;
    juce::StringArray directories;
};
