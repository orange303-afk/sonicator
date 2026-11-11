#include "ui/MainComponent.h"
#include "ui/shell/WindowBounds.h"
#include "core/settings/AppSettings.h"
#include "core/database/SampleRepository.h"
#include "core/diagnostics/Diagnostics.h"
#include "core/Version.h"

class SonicatorApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "SONICATOR"; }
    const juce::String getApplicationVersion() override { return sonicator::version(); }
    void initialise(const juce::String&) override
    {
        // Logging and the crash marker start before anything else can fail.
        sonicator::diagnostics::install();
        juce::Logger::writeToLog("SONICATOR " + juce::String(sonicator::version()) + " starting on "
                                 + juce::SystemStats::getOperatingSystemName());
        auto settings = sonicator::SettingsStore().load();
        sonicator::theme::apply(settings.theme);
        juce::Desktop::getInstance().setGlobalScaleFactor(
            juce::jlimit(0.75f, 2.0f, settings.scalePercent / 100.0f));
        auto database = settings.databasePath.isNotEmpty()
            ? juce::File(settings.databasePath)
            : sonicator::MainComponent::defaultDatabaseFile();
        if (settings.databasePath.isNotEmpty() && ! sonicator::SampleRepository::isCatalog(database))
        {
            juce::Logger::writeToLog("Saved catalog is missing or invalid; using the default catalog: " + database.getFullPathName());
            settings.databasePath.clear();
            try { sonicator::SettingsStore().update([](sonicator::AppSettings& saved) { saved.databasePath.clear(); }); } catch (...) {}
            database = sonicator::MainComponent::defaultDatabaseFile();
        }
        window = std::make_unique<Window>(std::move(settings), std::move(database));
    }
    void shutdown() override
    {
        if (window != nullptr) window->persistGeometry();
        window.reset();
        juce::Logger::writeToLog("SONICATOR shutting down");
        sonicator::diagnostics::uninstall();
    }
    void systemRequestedQuit() override { quit(); }
private:
    class Window final : public juce::DocumentWindow, private juce::Timer
    {
    public:
        Window(sonicator::AppSettings settings, juce::File database)
            : DocumentWindow("SONICATOR - Intelligent Sample Explorer", sonicator::theme::background, allButtons),
              appSettings(std::move(settings))
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new sonicator::MainComponent(database, true, appSettings), true);
            setResizable(true, false);
            setResizeLimits(880, 580, 3840, 2160);
            const auto displays = juce::Desktop::getInstance().getDisplays();
            const juce::Rectangle<int> saved { appSettings.windowX, appSettings.windowY,
                                               appSettings.windowWidth, appSettings.windowHeight };
            juce::Rectangle<int> area;
            if (const auto* display = displays.getDisplayForRect(saved)) area = display->userArea;
            else if (const auto* primary = displays.getPrimaryDisplay()) area = primary->userArea;
            setBounds(sonicator::resolveWindowBounds(saved, area));
            setVisible(true);
            geometryReady = true;
        }
        void persistGeometry()
        {
            if (! geometryReady) return;
            try
            {
                store.update([this](sonicator::AppSettings& saved)
                {
                    const auto bounds = getBounds();
                    saved.windowX = bounds.getX(); saved.windowY = bounds.getY();
                    saved.windowWidth = bounds.getWidth(); saved.windowHeight = bounds.getHeight();
                });
            }
            catch (...) {}
        }
        void closeButtonPressed() override
        {
            persistGeometry();
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    private:
        void moved() override { if (geometryReady) startTimer(500); }
        void resized() override
        {
            // The base class lays the content component out inside the new window area.
            juce::DocumentWindow::resized();
            if (geometryReady) startTimer(500);
        }
        void timerCallback() override { stopTimer(); persistGeometry(); }
        sonicator::SettingsStore store;
        sonicator::AppSettings appSettings;
        bool geometryReady = false;
    };
    std::unique_ptr<Window> window;
};
START_JUCE_APPLICATION(SonicatorApplication)
