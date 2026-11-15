#include <catch2/catch_test_macros.hpp>
#include "ui/settings/SettingsPanel.h"
#include "ui/settings/SettingsSection.h"
#include "ui/settings/HotkeysPanel.h"
#include "ui/commands/CommandRegistry.h"
#include "ui/MainComponent.h"
#include "ui/shell/Theme.h"
#include "core/Version.h"
#include "TestHelpers.h"

using namespace sonicator;

namespace
{
CommandRegistry makeRegistry()
{
    CommandRegistry registry;
    CommandSpec spec;
    spec.id = "waveform.normalize";
    spec.name = "Normalize";
    spec.category = "Waveform edit";
    spec.scope = CommandScope::waveform;
    spec.defaults = { juce::KeyPress('n') };
    registry.add(spec, {}, [] {});
    spec.id = "library.selectAll";
    spec.name = "Select all";
    spec.category = "Library";
    spec.scope = CommandScope::library;
    registry.add(spec, {}, [] {});
    return registry;
}
}

TEST_CASE("Settings opens on the stored section and falls back to General", "[settings][navigation]")
{
    AppSettings settings;
    settings.lastSettingsSection = "hotkeys";
    SettingsPanel panel(settings, juce::File("/tmp/catalog.sqlite"), false);
    const auto registry = makeRegistry();
    panel.setCommandRegistry(&registry);
    REQUIRE(panel.selectedSection() == SettingsSection::hotkeys);

    AppSettings unknown;
    unknown.lastSettingsSection = "nonsense";
    SettingsPanel other(unknown, juce::File("/tmp/catalog.sqlite"), false);
    REQUIRE(other.selectedSection() == SettingsSection::general);
}

TEST_CASE("Every section is reachable in standalone and switchable", "[settings][navigation]")
{
    SettingsPanel panel(AppSettings{}, juce::File("/tmp/catalog.sqlite"), false);
    const auto registry = makeRegistry();
    panel.setCommandRegistry(&registry);
    panel.setSize(800, 640);
    for (const auto section : { SettingsSection::general, SettingsSection::audioMidi,
                                SettingsSection::hotkeys, SettingsSection::plugins,
                                SettingsSection::about })
    {
        panel.selectSection(section);
        REQUIRE(panel.selectedSection() == section);
        const auto id = "settingsNav" + settingsSectionId(section);
        auto* button = dynamic_cast<juce::TextButton*>(findDescendantWithID(panel, id));
        REQUIRE(button != nullptr);
        REQUIRE(button->isVisible());
        REQUIRE(button->getToggleState());
    }
}

TEST_CASE("Switching sections does not rebuild the audio page", "[settings][navigation]")
{
    SettingsPanel panel(AppSettings{}, juce::File("/tmp/catalog.sqlite"), false);
    const auto registry = makeRegistry();
    panel.setCommandRegistry(&registry);
    int built = 0;
    panel.setAudioSettingsFactory([&built]()
    {
        ++built;
        return std::make_unique<juce::Component>();
    });
    panel.selectSection(SettingsSection::audioMidi);
    REQUIRE(built == 1);
    panel.selectSection(SettingsSection::general);
    panel.selectSection(SettingsSection::audioMidi);
    REQUIRE(built == 1);
}

TEST_CASE("Plugin mode hides the host-owned pages and stays on General", "[settings][navigation]")
{
    AppSettings settings;
    settings.lastSettingsSection = "audioMidi";
    SettingsPanel panel(settings, juce::File("/tmp/catalog.sqlite"), false);
    const auto registry = makeRegistry();
    panel.setCommandRegistry(&registry);
    panel.setPluginMode(true);
    REQUIRE(panel.selectedSection() == SettingsSection::general);
    auto* audioNav = dynamic_cast<juce::TextButton*>(findDescendantWithID(panel, "settingsNavaudioMidi"));
    auto* pluginsNav = dynamic_cast<juce::TextButton*>(findDescendantWithID(panel, "settingsNavplugins"));
    REQUIRE(audioNav != nullptr);
    REQUIRE(pluginsNav != nullptr);
    REQUIRE_FALSE(audioNav->isVisible());
    REQUIRE_FALSE(pluginsNav->isVisible());
    // Asking for a hidden page falls back instead of showing an empty panel.
    panel.selectSection(SettingsSection::plugins);
    REQUIRE(panel.selectedSection() == SettingsSection::general);
}

TEST_CASE("Plugin settings expose scale and a working close button", "[settings][acceptance-fixes]")
{
    SettingsPanel panel({}, juce::File("/tmp/catalog.sqlite"), false);
    panel.setPluginMode(true);
    auto* scale = findDescendantWithID(panel, "settingsScale");
    REQUIRE(scale != nullptr);
    CHECK(scale->isVisible());
    juce::TextButton* close = nullptr;
    for (auto* child : panel.getChildren())
        if (auto* button = dynamic_cast<juce::TextButton*>(child))
            if (button->getButtonText() == "Close") close = button;
    REQUIRE(close != nullptr);
    CHECK(close->isVisible());
    CHECK_FALSE(close->getBounds().isEmpty());
    bool closed = false;
    panel.onClose = [&] { closed = true; };
    close->onClick();
    CHECK(closed);
}

TEST_CASE("Support links are stacked without overlap", "[settings][acceptance-fixes]")
{
    SettingsPanel panel({}, juce::File("/tmp/catalog.sqlite"), false);
    panel.selectSection(SettingsSection::about);
    juce::HyperlinkButton* kofi = nullptr;
    juce::HyperlinkButton* boosty = nullptr;
    std::function<void(juce::Component&)> find = [&](juce::Component& parent)
    {
        for (auto* child : parent.getChildren())
        {
            if (auto* link = dynamic_cast<juce::HyperlinkButton*>(child))
            {
                if (link->getButtonText().contains("ko-fi")) kofi = link;
                if (link->getButtonText().contains("boosty")) boosty = link;
            }
            find(*child);
        }
    };
    find(panel);
    REQUIRE(kofi != nullptr);
    REQUIRE(boosty != nullptr);
    CHECK(kofi->getX() == boosty->getX());
    CHECK(kofi->getBottom() <= boosty->getY());
}

TEST_CASE("The Hotkeys page edits the shared registry profile", "[settings][navigation][hotkeys]")
{
    const auto registry = makeRegistry();
    SettingsPanel panel(AppSettings{}, juce::File("/tmp/catalog.sqlite"), false);
    panel.setCommandRegistry(&registry);
    ShortcutProfile stored;
    int writes = 0;
    panel.onShortcutProfileChanged = [&](const ShortcutProfile& candidate)
    {
        ++writes;
        stored = candidate;
        return true;
    };
    panel.selectSection(SettingsSection::hotkeys);
    auto* hotkeys = dynamic_cast<HotkeysPanel*>(findDescendantWithID(panel, "hotkeysPanel"));
    REQUIRE(hotkeys != nullptr);
    REQUIRE(hotkeys->commandRows() == 2);
    hotkeys->setSearch("normalize");
    REQUIRE(hotkeys->commandRows() == 1);
    hotkeys->selectCommandRow(0);
    hotkeys->beginCapture();
    REQUIRE(hotkeys->captureKey(juce::KeyPress('k', juce::ModifierKeys::altModifier, 0)));
    hotkeys->applyCapture();
    REQUIRE(writes == 1);
    REQUIRE(stored.overrides.count("waveform.normalize") == 1);
}

TEST_CASE("An embedded window offers the working Settings pages", "[settings][navigation][ui]")
{
    TestDirectory t;
    const auto dbPath = t.dir.getChildFile("catalog.sqlite");
    const auto settingsFile = t.dir.getChildFile("settings.json");
    {
        Database db(dbPath);
        SampleRepository repo(db);
        repo.createTables();
    }
    AudioEngine engine(false);
    MainComponent app(dbPath, false, {}, settingsFile, &engine, true);
    app.setSize(1400, 900);
    juce::TextButton* settingsButton = nullptr;
    for (int i = 0; i < app.getNumChildComponents() && settingsButton == nullptr; ++i)
        if (auto* button = dynamic_cast<juce::TextButton*>(app.getChildComponent(i)))
            if (button->getButtonText() == "Settings") settingsButton = button;
    REQUIRE(settingsButton != nullptr);
    settingsButton->onClick();
    auto* overlay = app.findChildWithID("settingsOverlay");
    REQUIRE(overlay != nullptr);
    auto* panel = dynamic_cast<SettingsPanel*>(findDescendantWithID(*overlay, "settingsPanel"));
    REQUIRE(panel != nullptr);
    panel->selectSection(SettingsSection::hotkeys);
    auto* hotkeys = dynamic_cast<HotkeysPanel*>(findDescendantWithID(*panel, "hotkeysPanel"));
    REQUIRE(hotkeys != nullptr);
    // The embedded window registers the whole command catalogue, not a demo list.
    REQUIRE(hotkeys->commandRows() > 20);
}

TEST_CASE("Scrolling a Settings page keeps its decorations inside the page", "[settings][navigation][ui]")
{
    theme::apply(ThemeMode::Dark);
    SonicatorLookAndFeel look;
    SettingsPanel panel(AppSettings{}, juce::File("/tmp/catalog.sqlite"), false);
    panel.setLookAndFeel(&look);
    // A short window scrolls far enough that a card straddles the top edge of the page.
    panel.setSize(820, 400);
    panel.selectSection(SettingsSection::about);

    // The About page is taller than the viewport; its cards and section rules are painted in
    // panel space from the scrolled content, so they must be clipped to the page.
    juce::Viewport* about = nullptr;
    for (int i = 0; i < panel.getNumChildComponents(); ++i)
        if (auto* viewport = dynamic_cast<juce::Viewport*>(panel.getChildComponent(i)))
            if (viewport->isVisible()) about = viewport;
    REQUIRE(about != nullptr);
    REQUIRE(about->getViewedComponent() != nullptr);
    REQUIRE(about->getViewedComponent()->getHeight() > about->getHeight());

    const auto before = panel.createComponentSnapshot(panel.getLocalBounds());
    // Scroll so the author/support cards cross the top edge of the viewport.
    about->setViewPosition(0, 230);
    REQUIRE(about->getViewPositionY() == 230);
    const auto scrolled = panel.createComponentSnapshot(panel.getLocalBounds());
    REQUIRE(before.isValid());
    REQUIRE(scrolled.isValid());

    // Everything outside the page rectangle, the title band above it and the band below, must be
    // untouched: a card or section rule must never paint there.
    const auto page = about->getBounds();
    int different = 0;
    for (int y = 0; y < panel.getHeight(); ++y)
        for (int x = 0; x < panel.getWidth(); x += 3)
        {
            const bool outside = y < page.getY() || y >= page.getBottom();
            if (! outside) continue;
            if (before.getPixelAt(x, y) != scrolled.getPixelAt(x, y)) ++different;
        }
    REQUIRE(different == 0);

    look.refreshColours();
    panel.setLookAndFeel(nullptr);
    theme::apply(ThemeMode::System);
}

TEST_CASE("About shows the running version and offers an update check", "[settings][navigation][ui]")
{
    SettingsPanel panel(AppSettings{}, juce::File("/tmp/catalog.sqlite"), false);
    panel.setSize(820, 640);
    panel.selectSection(SettingsSection::about);

    auto* check = dynamic_cast<juce::Button*>(findDescendantWithID(panel, "settingsCheckUpdates"));
    REQUIRE(check != nullptr);
    REQUIRE(check->getButtonText() == "Check for updates");
    REQUIRE(check->isVisible());

    juce::Label* versionShown = nullptr;
    std::function<void(juce::Component&)> walk = [&walk, &versionShown](juce::Component& node)
    {
        for (int i = 0; i < node.getNumChildComponents(); ++i)
        {
            auto* child = node.getChildComponent(i);
            if (auto* label = dynamic_cast<juce::Label*>(child);
                label != nullptr && label->getText() == juce::String("Version ") + version())
                versionShown = label;
            walk(*child);
        }
    };
    walk(panel);
    REQUIRE(versionShown != nullptr);
    REQUIRE(versionShown->isVisible());
    REQUIRE(versionShown->getBounds().getRight() <= panel.getWidth());
}
