#include "PluginScanner.h"

PluginScanner::PluginScanner()
{
    juce::addDefaultFormatsToManager(formatManager);
}

void PluginScanner::scanFormat(juce::AudioPluginFormat& format)
{
    juce::PluginDirectoryScanner scanner(knownPluginList,
                                          format,
                                          format.getDefaultLocationsToSearch(),
                                          true, // recursive
                                          juce::File(),
                                          true); // allowAsync

    juce::String pluginBeingScanned;
    while (scanner.scanNextFile(true, pluginBeingScanned))
    {
        // Intentionally silent per-file; final results are read from knownPluginList.
    }
}

void PluginScanner::scanAll()
{
    for (auto* format : formatManager.getFormats())
        scanFormat(*format);
}
