#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>

// Scans a caller-supplied set of directories (see PluginDirectoriesStore) for
// VST3 and Audio Unit plugins and keeps a juce::KnownPluginList populated with
// what it finds. When a plugin ships as both a VST3 and an AU, both copies are
// kept in the list - see computeDuplicateAudioUnits() in the .cpp - but the AU
// copy's identifier is reported via getLastDuplicateAudioUnitIdentifiers() so
// a caller (see PluginEnablementStore) can default it to disabled the first
// time it's seen, without losing track of the fact that it exists.
class PluginScanner
{
public:
    PluginScanner();
    ~PluginScanner();

    // The union of every registered format's OS-default plugin search
    // locations (e.g. ~/Library/Audio/Plug-Ins/VST3, .../Components) -
    // constructs its own throwaway AudioPluginFormatManager so it can be
    // called before any PluginScanner instance exists (PluginDirectoriesStore
    // uses this to seed its persisted directory list on first run).
    static juce::FileSearchPath getUnionOfDefaultLocations();

    // Scans directoriesToSearch across all registered formats on a background
    // thread, showing a modal progress dialog (progress bar, current-plugin
    // status label, and a Cancel button) so the UI thread - and the native
    // macOS menu bar - never blocks during a rescan. Cancelling stops the
    // scan cleanly; whatever plugins were already found are kept in
    // getKnownPluginList() regardless. onComplete is invoked on the message
    // thread once the scan finishes or is cancelled.
    void scanAll(const juce::FileSearchPath& directoriesToSearch, std::function<void()> onComplete);

    // True from the moment scanAll() launches its background thread until
    // onComplete fires (finished or cancelled). Callers (e.g. the "Rescan
    // Plugins" menu item) must check this before allowing another scanAll()
    // call - starting a second scan while one is running would replace
    // scanThread out from under the running thread.
    //
    // Deliberately backed by an explicit flag rather than
    // scanThread->isThreadRunning(): the background thread has already
    // exited by the time ScanThread::threadComplete() invokes onComplete, so
    // isThreadRunning() alone would report false while onComplete is still
    // executing - letting a reentrant scanAll() call replace scanThread out
    // from under the threadComplete() call that's still running it.
    bool isScanning() const;

    // Attempts to load a previously-saved scan result from disk into
    // knownPluginList (see saveCachedPluginListToDisk()). Returns true if a
    // cache file existed and was successfully loaded - callers can skip a
    // fresh scan. Returns false (safely, without throwing/crashing) if the
    // file doesn't exist, is empty, or fails to parse - callers should fall
    // back to a real scan in that case.
    bool loadCachedPluginList();

    // Persists the current knownPluginList to disk so a future launch's
    // loadCachedPluginList() can restore it without rescanning. Call after
    // any scanAll() completes. Creates the parent directory if needed.
    void saveCachedPluginListToDisk() const;

    juce::KnownPluginList& getKnownPluginList() { return knownPluginList; }
    juce::AudioPluginFormatManager& getFormatManager() { return formatManager; }

    // Plugins skipped by the most recent scanAll() because they were still
    // listed in the dead man's pedal file - i.e. they crashed the process
    // during a previous scan attempt and are now being avoided rather than
    // re-probed. Empty if nothing has ever crashed during a scan.
    const juce::StringArray& getLastSkippedCrashers() const { return lastSkippedCrashers; }

    // Identifier strings (PluginDescription::createIdentifierString()) of the
    // AudioUnit-format entries the most recent scanAll() found to duplicate a
    // VST3 entry (same name + manufacturer). Both entries stay in
    // getKnownPluginList() regardless - this just tells the caller which ones
    // are the non-preferred copy, for seeding a default-disabled state.
    const juce::StringArray& getLastDuplicateAudioUnitIdentifiers() const { return lastDuplicateAuIdentifiers; }

private:
    class ScanThread;
    std::unique_ptr<ScanThread> scanThread;

    // See isScanning() - stays true through the completion callback, not
    // just while scanThread->isThreadRunning().
    bool scanInProgress = false;

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPluginList;
    juce::StringArray lastSkippedCrashers;
    juce::StringArray lastDuplicateAuIdentifiers;
};
