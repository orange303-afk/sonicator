#include <catch2/catch_test_macros.hpp>
#include "ui/MainComponent.h"
#include "ui/settings/SettingsPanel.h"
#include "ui/commands/CommandRegistry.h"
#include "ui/library/FileListPanel.h"
#include "ui/split/SplitDialog.h"
#include "core/database/SampleRepository.h"
#include "core/dsp/BasicAnalyzer.h"
#include "ui/shell/Theme.h"
#include "TestHelpers.h"

using namespace sonicator;

namespace
{
void writeIfRequested(const juce::Image& image, const char* variable)
{
    const auto path = juce::SystemStats::getEnvironmentVariable(variable, {});
    if (path.isEmpty()) return;
    auto stream = juce::File(path).createOutputStream();
    REQUIRE(stream != nullptr);
    juce::PNGImageFormat png;
    REQUIRE(png.writeImageToStream(image, *stream));
}
}

// Renders the real workspace and both Settings pages. This is a smoke test for the Studio paint
// path (blank or throw-away surfaces fail), and its output is what the visual acceptance reads.
TEST_CASE("The workspace and Settings render in dark and light", "[ui][studio]")
{
    TestDirectory t;
    for (const auto mode : { ThemeMode::Dark, ThemeMode::Light })
    {
        theme::apply(mode);
        AppSettings settings;
        settings.theme = mode;
        MainComponent app(t.dir.getChildFile("workspace.sqlite"), false, settings,
                          t.dir.getChildFile("workspace-settings.json"));
        app.setSize(1400, 900);
        const auto workspace = app.createComponentSnapshot(app.getLocalBounds());
        REQUIRE(workspace.isValid());
        REQUIRE(workspace.getWidth() == 1400);
        REQUIRE(! workspace.getPixelAt(4, 4).isOpaque() == false);
        if (mode == ThemeMode::Dark)
            writeIfRequested(workspace, "SONICATOR_WORKSPACE_SCREENSHOT");
    }
    theme::apply(ThemeMode::System);
}

TEST_CASE("Settings pages render their content", "[ui][studio]")
{
    TestDirectory t;
    CommandRegistry registry;
    CommandSpec spec;
    spec.id = "waveform.normalize";
    spec.name = "Normalize (-0.1 dBFS)";
    spec.category = "Waveform edit";
    spec.scope = CommandScope::waveform;
    spec.defaults = { juce::KeyPress('n') };
    registry.add(spec, {}, [] {});
    spec.id = "waveform.save";
    spec.name = "Save...";
    spec.category = "Waveform edit";
    spec.defaults = { juce::KeyPress('s', juce::ModifierKeys::commandModifier, 0) };
    registry.add(spec, {}, [] {});
    spec.id = "app.settings";
    spec.name = "Settings";
    spec.category = "Application";
    spec.scope = CommandScope::app;
    registry.add(spec, {}, [] {});

    theme::apply(ThemeMode::Dark);
    AppSettings dark;
    dark.theme = ThemeMode::Dark;
    SonicatorLookAndFeel look;
    SettingsPanel panel(dark, t.dir.getChildFile("catalog.sqlite"), false);
    panel.setLookAndFeel(&look);
    panel.setCommandRegistry(&registry);
    panel.setSize(820, 640);
    panel.selectSection(SettingsSection::general);
    const auto general = panel.createComponentSnapshot(panel.getLocalBounds());
    REQUIRE(general.isValid());
    writeIfRequested(general, "SONICATOR_SETTINGS_GENERAL_SCREENSHOT");
    panel.selectSection(SettingsSection::hotkeys);
    const auto hotkeys = panel.createComponentSnapshot(panel.getLocalBounds());
    REQUIRE(hotkeys.isValid());
    writeIfRequested(hotkeys, "SONICATOR_SETTINGS_HOTKEYS_SCREENSHOT");
    panel.selectSection(SettingsSection::about);
    const auto about = panel.createComponentSnapshot(panel.getLocalBounds());
    REQUIRE(about.isValid());
    writeIfRequested(about, "SONICATOR_SETTINGS_ABOUT_SCREENSHOT");
    theme::apply(ThemeMode::Light);
    look.refreshColours();
    panel.sendLookAndFeelChange();
    const auto aboutLight = panel.createComponentSnapshot(panel.getLocalBounds());
    REQUIRE(aboutLight.isValid());
    writeIfRequested(aboutLight, "SONICATOR_SETTINGS_ABOUT_LIGHT_SCREENSHOT");
    theme::apply(ThemeMode::Dark);
    look.refreshColours();
    panel.sendLookAndFeelChange();
    panel.setLookAndFeel(nullptr);
    theme::apply(ThemeMode::System);
}

namespace
{
// The acceptance set: the workspace parts the Studio stage touched, in both themes and in the two
// logical sizes that matter (100%, and the logical size an 1120 px-wide window gets at 125%).
void writeAcceptanceShots()
{
    const auto directory = juce::SystemStats::getEnvironmentVariable("SONICATOR_SCREENSHOT_DIR", {});
    if (directory.isEmpty()) return;
    const juce::File folder(directory);
    folder.createDirectory();

    auto write = [&folder](const juce::Image& image, const juce::String& name)
    {
        REQUIRE(image.isValid());
        auto stream = folder.getChildFile(name).createOutputStream();
        REQUIRE(stream != nullptr);
        juce::PNGImageFormat png;
        REQUIRE(png.writeImageToStream(image, *stream));
    };

    struct Size { int width, height; const char* label; };
    for (const auto mode : { ThemeMode::Dark, ThemeMode::Light })
    {
        theme::apply(mode);
        const auto themeName = mode == ThemeMode::Dark ? juce::String("dark") : juce::String("light");
        for (const auto size : { Size { 1400, 900, "1400x900" }, Size { 1120, 720, "1120x720" } })
        {
            TestDirectory t;
            const auto dbPath = t.dir.getChildFile("shots.sqlite");
            const auto audio = t.wave("PAD_WARM_A#m.wav", 220.0, 2);
            {
                Database db(dbPath);
                SampleRepository repo(db);
                repo.createTables();
                auto record = BasicAnalyzer().analyze(audio);
                record.fileName = "PAD_WARM_A#m.wav";
                repo.upsert(record);
                repo.addRoot(t.dir.getFullPathName());
            }
            AppSettings settings;
            settings.theme = mode;
            MainComponent app(dbPath, false, settings, t.dir.getChildFile("shots-settings.json"));
            app.setSize(size.width, size.height);
            app.resized();
            const auto suffix = themeName + "-" + size.label + ".png";
            write(app.createComponentSnapshot(app.getLocalBounds()), "library-" + suffix);
            if (auto* library = dynamic_cast<FileListPanel*>(app.findChildWithID("library")))
                library->selectRows({ 0 });
            app.resized();

            // The analysis card as it sits in its panel, and the preview band under it.
            if (auto* card = dynamic_cast<AnalysisCard*>(findDescendantWithID(app, "analysisDetails")))
            {
                const int width = card->getWidth() + 2 * studio::spacingWide;
                const int height = card->getHeight() + 2 * studio::spacingWide;
                juce::Image image(juce::Image::ARGB, width, height, true);
                juce::Graphics g(image);
                g.fillAll(theme::panel);
                g.drawImageAt(card->createComponentSnapshot(card->getLocalBounds()),
                              studio::spacingWide, studio::spacingWide);
                write(image, "analysis-" + suffix);
            }
            if (auto* preview = app.findChildWithID("preview"))
                write(preview->createComponentSnapshot(preview->getLocalBounds()), "preview-" + suffix);
        }

        // The look-and-feel must outlive the dialog it is set on.
        SonicatorLookAndFeel look;
        SplitDialog split({ juce::File("/tmp/bank.wav") }, AppSettings{},
                          [](SplitOptions) { return true; });
        split.setLookAndFeel(&look);
        split.setSize(520, 430);
        split.resized();
        write(split.createComponentSnapshot(split.getLocalBounds()),
              "dialog-" + themeName + "-520x430.png");
        split.setLookAndFeel(nullptr);
    }
    theme::apply(ThemeMode::System);
}
} // namespace

TEST_CASE("The workspace renders its Studio parts for acceptance", "[ui][studio]")
{
    writeAcceptanceShots();
}

