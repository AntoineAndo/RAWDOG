#include "PluginScanner.h"
#include <juce_gui_basics/juce_gui_basics.h>

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
                                                  juce::File(),
                                                  true); // allowAsync

            juce::String pluginBeingScanned;

            while (! threadShouldExit() && scanner.scanNextFile(true, pluginBeingScanned))
            {
                setStatusMessage(format.getName() + ": " + pluginBeingScanned);
                setProgress(((double) formatIndex + (double) scanner.getProgress()) / (double) juce::jmax(1, numFormats));
            }
        }
    }

    void threadComplete(bool /*userPressedCancel*/) override
    {
        if (onComplete != nullptr)
            onComplete();
    }

private:
    juce::AudioPluginFormatManager& formatManager;
    juce::KnownPluginList& knownPluginList;
    std::function<void()> onComplete;
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

// Same "~/Library/Application Support/PixelBender/" folder FavouritePluginsStore
// writes its settings file into (juce::PropertiesFile::Options with
// folderName = "PixelBender", osxLibrarySubFolder = "Application Support") —
// a sibling file there keeps all of this app's persisted state in one place.
static juce::File getCachedPluginListFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Application Support")
        .getChildFile("PixelBender")
        .getChildFile("KnownPlugins.xml");
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

    scanThread = std::make_unique<ScanThread>(formatManager, knownPluginList, std::move(onComplete));
    scanThread->launchThread();
}
