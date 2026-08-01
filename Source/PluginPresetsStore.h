#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <optional>

// Persists named parameter-state snapshots ("presets") per plugin, keyed by
// juce::PluginDescription::createIdentifierString(), a stable plugin
// identity. Backed by a juce::ApplicationProperties settings file, storing
// everything under a single "pluginPresets" key as one JSON object -- a
// plugin identifier string could contain characters a PropertiesFile key
// wouldn't like, so per-plugin dynamic property keys are avoided entirely:
//
//   { "<identifierString>": [ { "name": "Preset 1", "state": "<base64>" }, ... ], ... }
//
// Each plugin's parameter state comes from juce::AudioProcessor::
// getStateInformation() (a raw binary blob) -- base64-encoded here purely
// because JSON has no native binary type, not for any obfuscation reason.
// Writes immediately (millisecondsBeforeSaving = 0) so a saved/renamed/deleted
// preset survives even if the app is force-quit right after.
class PluginPresetsStore
{
public:
    PluginPresetsStore();

    // In the order presets were saved for this plugin (oldest first) --
    // there are typically few enough per plugin that alphabetising would
    // just obscure "which one did I save most recently".
    juce::StringArray getPresetNames(const juce::String& pluginIdentifier) const;

    // Nullopt if no preset with that name exists for that plugin.
    std::optional<juce::MemoryBlock> getPresetState(const juce::String& pluginIdentifier,
                                                     const juce::String& presetName) const;

    // Auto-names it "Preset N", where N = (current preset count for this
    // plugin) + 1 -- no user-facing name prompt.
    void addPreset(const juce::String& pluginIdentifier, const juce::MemoryBlock& state);

    void deletePreset(const juce::String& pluginIdentifier, const juce::String& presetName);

    void renamePreset(const juce::String& pluginIdentifier, const juce::String& oldName, const juce::String& newName);

private:
    // Returns the raw juce::Array<var>* backing this plugin's presets list
    // within presetsJson, or nullptr if it has none yet -- callers that need
    // to mutate should use getOrCreatePresetsArray() instead.
    juce::Array<juce::var>* getPresetsArray(const juce::String& pluginIdentifier) const;
    juce::Array<juce::var>& getOrCreatePresetsArray(const juce::String& pluginIdentifier);

    void save();

    juce::ApplicationProperties appProperties;
    juce::var presetsJson; // a JSON object: identifierString -> array of {name, state}
};
