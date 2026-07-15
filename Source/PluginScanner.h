#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>

// Scans the standard VST3 and Audio Unit plugin locations and keeps a
// juce::KnownPluginList populated with what it finds.
class PluginScanner
{
public:
    PluginScanner();
    ~PluginScanner();

    // Scans all registered formats' default search paths on a background
    // thread, showing a modal progress dialog (progress bar, current-plugin
    // status label, and a Cancel button) so the UI thread — and the native
    // macOS menu bar — never blocks during a rescan. Cancelling stops the
    // scan cleanly; whatever plugins were already found are kept in
    // getKnownPluginList() regardless. onComplete is invoked on the message
    // thread once the scan finishes or is cancelled.
    void scanAll(std::function<void()> onComplete);

    // True from the moment scanAll() launches its background thread until
    // onComplete fires (finished or cancelled). Callers (e.g. the "Rescan
    // Plugins" menu item) must check this before allowing another scanAll()
    // call — starting a second scan while one is running would replace
    // scanThread out from under the running thread.
    bool isScanning() const;

    // Attempts to load a previously-saved scan result from disk into
    // knownPluginList (see saveCachedPluginListToDisk()). Returns true if a
    // cache file existed and was successfully loaded — callers can skip a
    // fresh scan. Returns false (safely, without throwing/crashing) if the
    // file doesn't exist, is empty, or fails to parse — callers should fall
    // back to a real scan in that case.
    bool loadCachedPluginList();

    // Persists the current knownPluginList to disk so a future launch's
    // loadCachedPluginList() can restore it without rescanning. Call after
    // any scanAll() completes. Creates the parent directory if needed.
    void saveCachedPluginListToDisk() const;

    juce::KnownPluginList& getKnownPluginList() { return knownPluginList; }
    juce::AudioPluginFormatManager& getFormatManager() { return formatManager; }

private:
    class ScanThread;
    std::unique_ptr<ScanThread> scanThread;

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPluginList;
};
