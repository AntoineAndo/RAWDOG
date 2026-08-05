#include "RawdogPropertiesFile.h"

juce::PropertiesFile& RawdogPropertiesFile::forSuffix(const juce::String& filenameSuffix)
{
    // Function-local static: outlives every *Store (all non-static
    // MainComponent members, destroyed well before process exit), and
    // entries are never erased here, so a returned reference stays valid
    // for the rest of the process's life.
    static std::map<juce::String, std::unique_ptr<juce::PropertiesFile>> instances;

    auto existing = instances.find(filenameSuffix);
    if (existing != instances.end())
        return *existing->second;

    juce::PropertiesFile::Options options;
    options.applicationName = "RAWDOG";
    options.filenameSuffix = filenameSuffix;
    options.folderName = "RAWDOG";
    options.osxLibrarySubFolder = "Application Support";
    options.millisecondsBeforeSaving = 0;

    return *instances.emplace(filenameSuffix, std::make_unique<juce::PropertiesFile>(options)).first->second;
}
