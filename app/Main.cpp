#include "ui/MainComponent.h"

class SonicatorApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "SONICATOR"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    void initialise(const juce::String&) override { window = std::make_unique<Window>(); }
    void shutdown() override { window.reset(); }
    void systemRequestedQuit() override { quit(); }
private:
    class Window final : public juce::DocumentWindow
    {
    public:
        Window() : DocumentWindow("SONICATOR - Intelligent Sample Explorer",
                                  juce::Colour(0xff10131b), allButtons)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new sonicator::MainComponent(), true);
            setResizable(true, false);
            setResizeLimits(880, 580, 3840, 2160);
            centreWithSize(1280, 800);
            setVisible(true);
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };
    std::unique_ptr<Window> window;
};
START_JUCE_APPLICATION(SonicatorApplication)
