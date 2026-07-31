#include "PluginPresetsStore.h"

PluginPresetsStore::PluginPresetsStore()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "RAWDOG";
    options.filenameSuffix = "settings";
    options.folderName = "RAWDOG";
    options.osxLibrarySubFolder = "Application Support";
    options.millisecondsBeforeSaving = 0; // write immediately
    appProperties.setStorageParameters(options);

    presetsJson = juce::JSON::parse(appProperties.getUserSettings()->getValue("pluginPresets"));

    // Missing/corrupt value (first run, or a hand-edited settings file) --
    // start from a fresh empty object rather than propagating a non-object
    // var that getOrCreatePresetsArray()'s getDynamicObject() call would
    // otherwise silently fail against.
    if (! presetsJson.isObject())
        presetsJson = juce::var(new juce::DynamicObject());
}

juce::Array<juce::var>* PluginPresetsStore::getPresetsArray(const juce::String& pluginIdentifier) const
{
    auto* obj = presetsJson.getDynamicObject();
    if (obj == nullptr)
        return nullptr;

    const juce::Identifier key(pluginIdentifier);
    if (! obj->hasProperty(key))
        return nullptr;

    return obj->getProperty(key).getArray();
}

juce::Array<juce::var>& PluginPresetsStore::getOrCreatePresetsArray(const juce::String& pluginIdentifier)
{
    auto* obj = presetsJson.getDynamicObject();
    const juce::Identifier key(pluginIdentifier);

    if (! obj->hasProperty(key))
        obj->setProperty(key, juce::var(juce::Array<juce::var>()));

    // The var stored above owns a reference-counted Array<var> -- getArray()
    // returns a pointer to that SAME underlying array, so mutating through it
    // (as callers of this method do) mutates presetsJson in place with no
    // need to re-assign the property afterward.
    return *obj->getProperty(key).getArray();
}

juce::StringArray PluginPresetsStore::getPresetNames(const juce::String& pluginIdentifier) const
{
    juce::StringArray names;

    if (auto* arr = getPresetsArray(pluginIdentifier))
        for (const auto& entry : *arr)
            names.add(entry.getProperty("name", {}).toString());

    return names;
}

std::optional<juce::MemoryBlock> PluginPresetsStore::getPresetState(const juce::String& pluginIdentifier,
                                                                     const juce::String& presetName) const
{
    if (auto* arr = getPresetsArray(pluginIdentifier))
    {
        for (const auto& entry : *arr)
        {
            if (entry.getProperty("name", {}).toString() == presetName)
            {
                juce::MemoryBlock state;
                state.fromBase64Encoding(entry.getProperty("state", {}).toString());
                return state;
            }
        }
    }

    return std::nullopt;
}

void PluginPresetsStore::addPreset(const juce::String& pluginIdentifier, const juce::MemoryBlock& state)
{
    auto& arr = getOrCreatePresetsArray(pluginIdentifier);

    auto* preset = new juce::DynamicObject();
    preset->setProperty("name", "Preset " + juce::String(arr.size() + 1));
    preset->setProperty("state", state.toBase64Encoding());
    arr.add(juce::var(preset));

    save();
}

void PluginPresetsStore::deletePreset(const juce::String& pluginIdentifier, const juce::String& presetName)
{
    auto* arr = getPresetsArray(pluginIdentifier);
    if (arr == nullptr)
        return;

    for (int i = arr->size(); --i >= 0;)
        if (arr->getReference(i).getProperty("name", {}).toString() == presetName)
            arr->remove(i);

    save();
}

void PluginPresetsStore::renamePreset(const juce::String& pluginIdentifier,
                                       const juce::String& oldName, const juce::String& newName)
{
    auto* arr = getPresetsArray(pluginIdentifier);
    if (arr == nullptr)
        return;

    for (auto& entry : *arr)
    {
        if (entry.getProperty("name", {}).toString() == oldName)
        {
            if (auto* obj = entry.getDynamicObject())
                obj->setProperty("name", newName);
            break;
        }
    }

    save();
}

void PluginPresetsStore::save()
{
    appProperties.getUserSettings()->setValue("pluginPresets", juce::JSON::toString(presetsJson));
    appProperties.saveIfNeeded();
}
