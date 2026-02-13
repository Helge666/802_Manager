#include <juce_gui_basics/juce_gui_basics.h>
#include "MainComponent.h"

class MainApplication  : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override       { return "802 Manager JUCE"; }
    const juce::String getApplicationVersion() override    { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override             { return true; }

    void initialise (const juce::String&) override
    {
        mainWindow.reset (new MainWindow (getApplicationName()));
    }

    void shutdown() override
    {
        mainWindow = nullptr;
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    void anotherInstanceStarted (const juce::String&) override {}

    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow (juce::String name)
            : DocumentWindow (name,
                              juce::Desktop::getInstance().getDefaultLookAndFeel()
                                  .findColour (ResizableWindow::backgroundColourId),
                              DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent(), true);

            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (MainApplication)
