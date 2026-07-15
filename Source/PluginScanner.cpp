#include "PluginScanner.h"
#include <juce_gui_basics/juce_gui_basics.h>

// Same "~/Library/Application Support/PixelBender/" folder FavouritePluginsStore
// writes its settings file into — a sibling file there keeps all of this app's
// persisted state in one place.
static juce::File getPixelBenderSupportFolder()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Application Support")
        .getChildFile("PixelBender");
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
    return getPixelBenderSupportFolder().getChildFile("DeadMansPedal.txt");
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
               std::function<void()> onCompleteIn)
        : ThreadWithProgressWindow("Scanning for plugins...", true, true),
          formatManager(formatManagerIn),
          knownPluginList(knownPluginListIn),
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
                                                  format.getDefaultLocationsToSearch(),
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

        for (const auto& file : knownPluginList.getBlacklistedFiles())
            if (! blacklistedBefore.contains(file))
                skippedCrashers.add(file);
    }

    const juce::StringArray& getSkippedCrashers() const { return skippedCrashers; }

    void threadComplete(bool /*userPressedCancel*/) override
    {
        if (onComplete != nullptr)
            onComplete();
    }

private:
    juce::AudioPluginFormatManager& formatManager;
    juce::KnownPluginList& knownPluginList;
    std::function<void()> onComplete;
    juce::StringArray skippedCrashers;
};

PluginScanner::PluginScanner()
{
    juce::addDefaultFormatsToManager(formatManager);
}

PluginScanner::~PluginScanner() = default;

bool PluginScanner::isScanning() const
{
    return scanThread != nullptr && scanThread->isThreadRunning();
}

static juce::File getCachedPluginListFile()
{
    return getPixelBenderSupportFolder().getChildFile("KnownPlugins.xml");
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

void PluginScanner::scanAll(std::function<void()> onComplete)
{
    // Guard against re-entrancy: callers (MainComponent disables "Rescan
    // Plugins" while isScanning()) shouldn't normally hit this, but bail out
    // rather than replacing scanThread out from under a still-running scan,
    // which would destroy it mid-flight and re-block the message thread via
    // ~ThreadWithProgressWindow's stopThread().
    if (isScanning())
        return;

    scanThread = std::make_unique<ScanThread>(formatManager, knownPluginList,
        [this, userOnComplete = std::move(onComplete)]
        {
            lastSkippedCrashers = scanThread->getSkippedCrashers();

            if (userOnComplete != nullptr)
                userOnComplete();
        });
    scanThread->launchThread();
}
