#include "ui/MainComponent.h"
#include "core/settings/AppSettings.h"
#include "core/database/SampleRepository.h"

class SonicatorApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "SONICATOR"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    void initialise(const juce::String&) override
    {
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
            try { sonicator::SettingsStore().save(settings); } catch (...) {}
            database = sonicator::MainComponent::defaultDatabaseFile();
        }
        window = std::make_unique<Window>(std::move(settings), std::move(database));
    }
    void shutdown() override { window.reset(); }
    void systemRequestedQuit() override { quit(); }
private:
    class Window final : public juce::DocumentWindow
    {
    public:
        Window(sonicator::AppSettings settings, juce::File database)
            : DocumentWindow("SONICATOR - Intelligent Sample Explorer", sonicator::theme::background, allButtons)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new sonicator::MainComponent(database, true, std::move(settings)), true);
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
