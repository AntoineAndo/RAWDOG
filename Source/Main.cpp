#include <juce_gui_extra/juce_gui_extra.h>
#include "MainComponent.h"
#include "PixelBenderLookAndFeel.h"

class PixelBenderApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Pixel Bender"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }

    void initialise(const juce::String&) override
    {
        juce::LookAndFeel::setDefaultLookAndFeel(&lookAndFeel);
        mainWindow.reset(new MainWindow(getApplicationName()));

        // Restores whatever size/position the user last left the window at,
        // rather than always reopening at the small default -- getWindowState
        // FileLocation()'s file won't exist yet on a first run, so there's
        // nothing to restore and the constructor's centreWithSize() default
        // stands.
        const auto savedState = getWindowStateFileLocation().loadFileAsString();
        if (savedState.isNotEmpty())
            mainWindow->restoreWindowStateFromString(savedState);
    }

    void shutdown() override
    {
        if (mainWindow != nullptr)
        {
            const auto stateFile = getWindowStateFileLocation();
            stateFile.getParentDirectory().createDirectory(); // replaceWithText() doesn't create it
            stateFile.replaceWithText(mainWindow->getWindowStateAsString());
        }

        mainWindow = nullptr;
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    }

    // Per-user file the window's size/position/fullscreen state round-trips
    // through (see initialise()/shutdown() above), via
    // ResizableWindow::getWindowStateAsString()/restoreWindowStateFromString()'s
    // own opaque encoding -- not the properties/settings file JUCE apps
    // typically use for this, since that needs the juce_data_structures
    // module, which this project doesn't otherwise link.
    static juce::File getWindowStateFileLocation()
    {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("Pixel Bender")
            .getChildFile("window-state.txt");
    }

    // Overridden (rather than relying on JUCEApplication's default, which
    // just calls quit() outright) so both the window's close button and the
    // app menu/Cmd+Q route through MainComponent's discard-changes
    // confirmation before anything actually quits -- closeButtonPressed()
    // below calls this same override, so there's exactly one place that
    // knows how to quit "for real".
    void systemRequestedQuit() override
    {
        if (mainWindow == nullptr)
        {
            quit();
            return;
        }

        mainWindow->mainComponent->confirmQuit([this] { quit(); });
    }

    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow(const juce::String& name)
            : DocumentWindow(name,
                              juce::Desktop::getInstance().getDefaultLookAndFeel()
                                  .findColour(juce::ResizableWindow::backgroundColourId),
                              DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(true);
            setResizable(true, true);
            setResizeLimits(700, 500, 10000, 10000);
            mainComponent = new MainComponent();
            setContentOwned(mainComponent, true);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

        // Non-owning -- setContentOwned() above (ownComponent=true) is what
        // actually owns and deletes it, as part of DocumentWindow's own
        // destructor. Kept here only so systemRequestedQuit() has a handle to
        // call confirmQuit() on; safe to use right up until mainWindow itself
        // is torn down in PixelBenderApplication::shutdown(), well after any
        // quit-confirmation dialog this points at would have closed.
        MainComponent* mainComponent = nullptr;
    };

private:
    PixelBenderLookAndFeel lookAndFeel;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(PixelBenderApplication)
