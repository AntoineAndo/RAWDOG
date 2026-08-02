#include "PluginScanner.h"
#include <juce_gui_basics/juce_gui_basics.h>

// Same "~/Library/Application Support/RAWDOG/" folder FavouritePluginsStore
// writes its settings file into — a sibling file there keeps all of this app's
// persisted state in one place.
static juce::File getRawdogSupportFolder()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Application Support")
        .getChildFile("RAWDOG");
}

// JUCE's crash-recovery mechanism for plugin scanning: before probing a
// plugin, PluginDirectoryScanner records its path here; the entry is removed
// once the probe returns safely. If this process crashes mid-scan (as some
// third-party plugins are known to do), the *next* scanner constructed with
// this same file sees the still-present entry, treats that plugin as a known
// crasher, and skips it — turning what would otherwise be an infinite
// crash-on-rescan loop into a one-time loss of that single plugin.
static juce::File getDeadMansPedalFile()
{
    return getRawdogSupportFolder().getChildFile("DeadMansPedal.txt");
}

// Runs PluginDirectoryScanner across every registered format on a background
// thread, driving juce::ThreadWithProgressWindow's built-in modal dialog
// (progress bar + status label + Cancel button). This is JUCE's standard
// idiom for exactly this "long task, show progress, allow cancel" case, so we
// use it rather than hand-rolling a DialogWindow. launchThread() (as opposed
// to the JUCE_MODAL_LOOPS_PERMITTED-gated runThread()) starts the scan and
// returns immediately, calling threadComplete() on the message thread once
// the scan finishes or is cancelled — so the message loop (and menu bar)
// keeps pumping the whole time.
class PluginScanner::ScanThread : public juce::ThreadWithProgressWindow
{
public:
    ScanThread(juce::AudioPluginFormatManager& formatManagerIn,
               juce::KnownPluginList& knownPluginListIn,
               juce::FileSearchPath directoriesToSearchIn,
               std::function<void()> onCompleteIn)
        : ThreadWithProgressWindow("Scanning for plugins...", true, true),
          formatManager(formatManagerIn),
          knownPluginList(knownPluginListIn),
          directoriesToSearch(std::move(directoriesToSearchIn)),
          onComplete(std::move(onCompleteIn))
    {
    }

    void run() override
    {
        // PluginDirectoryScanner's own getFailedFiles() only reports files
        // that were probed but yielded zero plugin types — it deliberately
        // excludes dead man's pedal skips (those are recorded as blacklist
        // entries on knownPluginList instead, before the probe even starts).
        // So the only way to find out which plugins were skipped as known
        // crashers this run is to diff the blacklist before and after.
        const auto blacklistedBefore = knownPluginList.getBlacklistedFiles();

        const auto formats = formatManager.getFormats();
        const int numFormats = formats.size();

        for (int formatIndex = 0; formatIndex < numFormats; ++formatIndex)
        {
            if (threadShouldExit())
                return;

            auto& format = *formats.getUnchecked(formatIndex);

            juce::PluginDirectoryScanner scanner(knownPluginList,
                                                  format,
                                                  directoriesToSearch,
                                                  true, // recursive
                                                  getDeadMansPedalFile(),
                                                  true); // allowAsync

            juce::String pluginBeingScanned;

            while (! threadShouldExit() && scanner.scanNextFile(true, pluginBeingScanned))
            {
                setStatusMessage(format.getName() + ": " + pluginBeingScanned);
                setProgress(((double) formatIndex + (double) scanner.getProgress()) / (double) juce::jmax(1, numFormats));
            }
        }

        computeDuplicateAudioUnits();

        for (const auto& file : knownPluginList.getBlacklistedFiles())
            if (! blacklistedBefore.contains(file))
                skippedCrashers.add(file);
    }

    const juce::StringArray& getSkippedCrashers() const { return skippedCrashers; }
    const juce::StringArray& getDuplicateAudioUnitIdentifiers() const { return duplicateAuIdentifiers; }

    void threadComplete(bool /*userPressedCancel*/) override
    {
        if (onComplete != nullptr)
            onComplete();
    }

private:
    // Some plugins ship as both a VST3 and an AU; hosting both formats means
    // the exact same plugin (same name+vendor) shows up twice for no benefit,
    // since either format works equally well here. Both entries stay in
    // knownPluginList (the Settings plugin list needs to show both), but the
    // AU side of an actual duplicate is recorded here so the caller can
    // default it to disabled the first time it's seen — VST3 is the
    // preferred copy. A plugin that exists in just one format (VST3-only or
    // AU-only) is untouched either way.
    void computeDuplicateAudioUnits()
    {
        juce::StringArray vst3Keys;

        for (const auto& type : knownPluginList.getTypes())
            if (type.pluginFormatName == "VST3")
                vst3Keys.add(type.name + "|" + type.manufacturerName);

        for (const auto& type : knownPluginList.getTypes())
            if (type.pluginFormatName == "AudioUnit" && vst3Keys.contains(type.name + "|" + type.manufacturerName))
                duplicateAuIdentifiers.add(type.createIdentifierString());
    }

    juce::AudioPluginFormatManager& formatManager;
    juce::KnownPluginList& knownPluginList;
    juce::FileSearchPath directoriesToSearch;
    std::function<void()> onComplete;
    juce::StringArray skippedCrashers;
    juce::StringArray duplicateAuIdentifiers;
};

PluginScanner::PluginScanner()
{
    // Both VST3 and AU are scanned — some plugins ship only as one or the
    // other. When a plugin ships as both, ScanThread::computeDuplicateAudioUnits()
    // reports the AU copy as a duplicate so the caller can default it to
    // disabled, keeping VST3 as the preferred format shown by default rather
    // than showing the same plugin twice.
    juce::addDefaultFormatsToManager(formatManager);
}

juce::FileSearchPath PluginScanner::getUnionOfDefaultLocations()
{
    juce::AudioPluginFormatManager tempManager;
    juce::addDefaultFormatsToManager(tempManager);

    juce::FileSearchPath merged;
    for (auto* format : tempManager.getFormats())
        merged.addPath(format->getDefaultLocationsToSearch());

    return merged;
}

PluginScanner::~PluginScanner() = default;

bool PluginScanner::isScanning() const
{
    return scanThread != nullptr && scanThread->isThreadRunning();
}

static juce::File getCachedPluginListFile()
{
    return getRawdogSupportFolder().getChildFile("KnownPlugins.xml");
}

bool PluginScanner::loadCachedPluginList()
{
    const auto file = getCachedPluginListFile();

    if (! file.existsAsFile())
        return false;

    auto xml = juce::XmlDocument::parse(file);

    // parse() returns nullptr on a missing/empty/malformed file rather than
    // throwing, so this guard covers "file exists but is corrupt/hand-edited
    // garbage" without crashing or leaving knownPluginList in a weird state.
    if (xml == nullptr)
        return false;

    knownPluginList.recreateFromXml(*xml);
    return true;
}

void PluginScanner::saveCachedPluginListToDisk() const
{
    const auto file = getCachedPluginListFile();

    file.getParentDirectory().createDirectory();

    if (auto xml = knownPluginList.createXml())
        xml->writeTo(file);
}

void PluginScanner::scanAll(const juce::FileSearchPath& directoriesToSearch, std::function<void()> onComplete)
{
    // Guard against re-entrancy: callers (MainComponent disables "Rescan
    // Plugins" while isScanning()) shouldn't normally hit this, but bail out
    // rather than replacing scanThread out from under a still-running scan,
    // which would destroy it mid-flight and re-block the message thread via
    // ~ThreadWithProgressWindow's stopThread().
    if (isScanning())
        return;

    scanThread = std::make_unique<ScanThread>(formatManager, knownPluginList, directoriesToSearch,
        [this, userOnComplete = std::move(onComplete)]
        {
            lastSkippedCrashers = scanThread->getSkippedCrashers();
            lastDuplicateAuIdentifiers = scanThread->getDuplicateAudioUnitIdentifiers();

            if (userOnComplete != nullptr)
                userOnComplete();
        });
    scanThread->launchThread();
}
