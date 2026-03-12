#include <JuceHeader.h>
#include "MainComponent.h"
#include "SplashComponent.h"

/**
 * @file Main.cpp
 * @brief Application entry point for GUI version
 */

//==============================================================================
class LooperApplication : public juce::JUCEApplication,
                          private juce::Timer
{
public:
    LooperApplication() {}

    const juce::String getApplicationName() override
    {
        return "chief";
    }

    const juce::String getApplicationVersion() override
    {
        return "1.0.0";
    }

    bool moreThanOneInstanceAllowed() override
    {
        return true;
    }

    void initialise(const juce::String& commandLine) override
    {
        // 1. Show splash window immediately (before any heavy work)
        splashComponent = std::make_unique<SplashComponent>();

        splashWindow = std::make_unique<juce::DocumentWindow>(
            "", juce::Colour(0xFF1A1A1A), 0);  // no title bar buttons
        splashWindow->setUsingNativeTitleBar(false);
        splashWindow->setTitleBarHeight(0);
        splashWindow->setContentNonOwned(splashComponent.get(), true);
        splashWindow->setDropShadowEnabled(true);
        splashWindow->setColour(juce::DocumentWindow::backgroundColourId,
                                juce::Colour(0xFF1A1A1A));
        splashWindow->centreWithSize(500, 350);
        splashWindow->setVisible(true);
        splashWindow->toFront(true);

        // 2. Defer MainWindow creation so the splash paints first
        //    Use a short timer to let the message loop repaint the splash
        startTime = juce::Time::getMillisecondCounter();
        phase = Phase::WaitingToCreateMain;
        startTimer(50);
    }

    void timerCallback() override
    {
        if (phase == Phase::WaitingToCreateMain)
        {
            // Give the splash one repaint cycle before creating MainWindow
            const juce::uint32 elapsed = juce::Time::getMillisecondCounter() - startTime;
            if (elapsed < 100)
                return;  // wait a bit for splash to paint

            // Create splash status callback BEFORE MainWindow construction
            // so plugin load messages are captured during audio init.
            // We must paint immediately because the message loop is blocked
            // during MainComponent construction.
            auto statusCb = [this](const juce::String& text)
            {
                if (splashComponent)
                {
                    splashComponent->setStatusText(text);
                    // Dispatch pending repaints so the status text
                    // is rendered during synchronous constructor execution
                    if (auto* peer = splashWindow->getPeer())
                        peer->performAnyPendingRepaintsNow();
                }
            };

            // Now create the main window (triggers audio init, plugin loading, etc.)
            mainWindow.reset(new MainWindow(getApplicationName(), false, std::move(statusCb)));

            mainCreatedTime = juce::Time::getMillisecondCounter();
            phase = Phase::WaitingForPlugins;
            return;
        }

        // Phase::WaitingForPlugins — wait for minimum display time and all plugins
        const juce::uint32 elapsed = juce::Time::getMillisecondCounter() - mainCreatedTime;

        auto* mc = dynamic_cast<MainComponent*>(
            mainWindow ? mainWindow->getContentComponent() : nullptr);

        if (elapsed >= 500 && (!mc || mc->getAudioEngine().getPendingPluginLoads() == 0))
        {
            stopTimer();

            // Clear the splash callback
            if (mc)
                mc->onSplashStatus = nullptr;

            splashWindow.reset();
            splashComponent.reset();

            mainWindow->setVisible(true);
            mainWindow->toFront(true);
        }
    }

    void shutdown() override
    {
        stopTimer();
        splashWindow.reset();
        splashComponent.reset();
        mainWindow = nullptr;
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    void anotherInstanceStarted(const juce::String& commandLine) override
    {
    }

    //==============================================================================
    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow(juce::String name, bool visible = true,
                   std::function<void(const juce::String&)> splashCb = nullptr)
            : DocumentWindow(name,
                           juce::Desktop::getInstance().getDefaultLookAndFeel()
                               .findColour(juce::ResizableWindow::backgroundColourId),
                           DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(std::move(splashCb)), true);

           #if JUCE_IOS || JUCE_ANDROID
            setFullScreen(true);
           #else
            setResizable(true, true);
            centreWithSize(getWidth(), getHeight());
           #endif

            setVisible(visible);
        }

        void closeButtonPressed() override
        {
            JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
    };

private:
    enum class Phase { WaitingToCreateMain, WaitingForPlugins };

    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<juce::DocumentWindow> splashWindow;
    std::unique_ptr<SplashComponent> splashComponent;
    juce::uint32 startTime {0};
    juce::uint32 mainCreatedTime {0};
    Phase phase {Phase::WaitingToCreateMain};
};

//==============================================================================
START_JUCE_APPLICATION(LooperApplication)
