#include <juce_gui_extra/juce_gui_extra.h>
#include "MainComponent.h"

class PixelBenderApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Pixel Bender"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }

    void initialise(const juce::String&) override
    {
        mainWindow.reset(new MainWindow(getApplicationName()));
    }

    void shutdown() override { mainWindow = nullptr; }

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
            setContentOwned(new MainComponent(), true);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(PixelBenderApplication)
