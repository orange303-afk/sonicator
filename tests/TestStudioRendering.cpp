#include <catch2/catch_test_macros.hpp>
#include "ui/MainComponent.h"
#include "ui/settings/SettingsPanel.h"
#include "ui/commands/CommandRegistry.h"
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
    panel.setLookAndFeel(nullptr);
    theme::apply(ThemeMode::System);
}
