#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include "ui/MainComponent.h"
#include "ui/analysis/AnalysisCard.h"
#include "ui/analysis/KeyBpmBlock.h"
#include "ui/library/FileListPanel.h"
#include "ui/library/SearchField.h"
#include "ui/preview/PreviewPanel.h"
#include "ui/split/SplitDialog.h"
#include "ui/extract/ExtractDialog.h"
#include "ui/tagging/AutoTagDialog.h"
#include "ui/library/TapTempoDialog.h"
#include "core/tagging/AutoTagService.h"
#include "core/database/SampleRepository.h"
#include "core/dsp/BasicAnalyzer.h"
#include "ui/shell/Theme.h"
#include "TestHelpers.h"

using namespace sonicator;

namespace
{
// Shared by every case in this file: the shell finds its buttons by text today, and the restyle
// keeps that text as the accessible name.
juce::Button* buttonWithText(juce::Component& parent, const juce::String& text)
{
    for (auto* child : parent.getChildren())
        if (auto* button = dynamic_cast<juce::Button*>(child))
            if (button->getButtonText() == text) return button;
    return nullptr;
}
}

TEST_CASE("The header keeps search and settings on the grid", "[ui][studio]")
{
    TestDirectory t;
    MainComponent app(t.dir.getChildFile("header.sqlite"), false);
    app.setSize(880, 580);
    app.resized();
    auto* search = dynamic_cast<SearchField*>(app.findChildWithID("search"));
    REQUIRE(search != nullptr);
    REQUIRE(search->getHeight() == 32);
    REQUIRE(search->leadingIcon() == ActionIcon::search);
    auto* settings = buttonWithText(app, "Settings");
    REQUIRE(settings != nullptr);
    REQUIRE(settings->getButtonText() == "Settings");
    REQUIRE(settings->getWidth() >= 80);
    REQUIRE(settings->getHeight() == studio::controlHeight);
    REQUIRE(settings->getRight() <= app.getWidth() - 8);
    REQUIRE(search->getRight() < settings->getX());
    REQUIRE(search->getWidth() >= 300);

    // The clear control sits in the field whether or not there is text, and it still clears.
    auto* clear = dynamic_cast<juce::TextButton*>(findDescendantWithID(*search, "clearSearch"));
    REQUIRE(clear != nullptr);
    REQUIRE(search->getText().isEmpty());
    REQUIRE(clear->isVisible());
    search->setText("kick", juce::sendNotificationSync);
    REQUIRE(clear->isVisible());
    clear->onClick();
    REQUIRE(search->getText().isEmpty());
}

TEST_CASE("Filter chips keep their state and the shorter hide-empty label", "[ui][studio]")
{
    TestDirectory t;
    MainComponent app(t.dir.getChildFile("chips.sqlite"), false);
    app.setSize(880, 580);
    app.resized();
    auto* hide = dynamic_cast<juce::ToggleButton*>(app.findChildWithID("hideEmpty"));
    REQUIRE(hide != nullptr);
    REQUIRE(hide->getButtonText() == "Hide empty");
    REQUIRE(hide->getTooltip().contains("unsupported"));
    REQUIRE(hide->getWidth() >= 100);
    auto* tags = buttonWithText(app, "Tags");
    REQUIRE(tags != nullptr);
    REQUIRE(tags->getWidth() >= 64);
    auto* next = buttonWithText(app, ">");
    REQUIRE(next != nullptr);
    REQUIRE(next->getWidth() == studio::iconButtonSize);
    // The row must still fit the 880 px window: the old fixed widths overflowed the library pane.
    auto* clear = buttonWithText(app, "Clear filters");
    REQUIRE(clear != nullptr);
    REQUIRE(clear->isVisible());
    REQUIRE(clear->getRight() <= app.getWidth() - 8);
}

TEST_CASE("A selected library row carries the accent stripe", "[ui][studio]")
{
    TestDirectory t;
    Database db(t.dir.getChildFile("rows.sqlite"));
    SampleRepository repo(db);
    repo.createTables();
    for (int i = 0; i < 4; ++i)
    {
        const auto name = "s" + juce::String(i) + ".wav";
        auto record = BasicAnalyzer().analyze(t.wave(name, 200.0 + 40.0 * i));
        record.fileName = name;
        repo.upsert(record);
    }
    FileListPanel panel;
    panel.setSamples(repo.page());
    panel.setSize(700, 400);
    panel.resized();
    juce::TableListBox* table = nullptr;
    for (auto* child : panel.getChildren())
        if (auto* found = dynamic_cast<juce::TableListBox*>(child)) table = found;
    REQUIRE(table != nullptr);
    REQUIRE(table->getRowHeight() == studio::tableRowHeight);
    table->selectRow(0, false);
    const auto image = panel.createComponentSnapshot(panel.getLocalBounds());
    const int y = table->getY() + table->getHeaderHeight() + table->getRowHeight() / 2;
    // The stripe is a filled 2 px rectangle on the row's left edge, so both its pixels are exact.
    REQUIRE(image.getPixelAt(table->getX(), y) == theme::accent);
    REQUIRE(image.getPixelAt(table->getX() + 1, y) == theme::accent);
    REQUIRE(image.getPixelAt(table->getX() + 20, y) != theme::accent);
}

TEST_CASE("The analysis card shows one tile per statistic", "[ui][studio]")
{
    AnalysisCard card;
    AnalysisContent content;
    content.title = "FINE_CRASH_01.wav";
    content.meta = "WAV · 2 ch · 48 kHz";
    content.stats = { { "2.184 s", "Length" }, { "48.0 kHz", "Rate" }, { "-0.3 dBFS", "Peak" } };
    content.notes = { "Shown copy is not the catalog representative." };
    card.setContent(content);
    card.setSize(270, 400);
    card.resized();
    REQUIRE(card.content().stats.size() == 3);
    REQUIRE(card.getNumChildComponents() == 6);   // title, meta, three tiles, one note
    REQUIRE(AnalysisCard::columnsFor(240) == 2);
    REQUIRE(AnalysisCard::columnsFor(200) == 1);
    // Two columns are shorter than one, which is the whole point of the grid.
    REQUIRE(card.preferredHeight(240) < card.preferredHeight(200));
}

TEST_CASE("A wrapped note grows to the lines it needs", "[ui][studio]")
{
    AnalysisCard card;
    AnalysisContent content;
    content.notes = { "Select a sample to inspect its measured audio features." };
    card.setContent(content);
    card.setSize(224, 300);
    card.resized();
    REQUIRE(card.getNumChildComponents() == 1);
    // One line would truncate this sentence at 224 px; it wraps to two.
    REQUIRE(card.getChildComponent(0)->getHeight()
            >= 2 * juce::roundToInt(studio::captionFont().getHeight()));
}

TEST_CASE("The analysis card keeps every note the old label printed", "[ui][studio]")
{
    AnalysisCard card;
    AnalysisContent content;
    content.title = "x.wav";
    content.notes = { "File is missing", "Inside image disc.iso" };
    card.setContent(content);
    card.setSize(270, 300);
    card.resized();
    REQUIRE(card.getNumChildComponents() == 3);   // title and both notes
}

TEST_CASE("Selecting a sample fills the analysis card and keeps its notes", "[ui][studio]")
{
    TestDirectory t;
    const auto dbPath = t.dir.getChildFile("analysis.sqlite");
    const auto good = t.wave("b_ok.wav", 440.0, 2);
    const auto broken = t.dir.getChildFile("a_broken.wav");
    {
        Database db(dbPath);
        SampleRepository repo(db);
        repo.createTables();
        auto ok = BasicAnalyzer().analyze(good);
        ok.fileName = "b_ok.wav";
        repo.upsert(ok);
        auto bad = ok;
        bad.filePath = broken.getFullPathName();
        bad.fileName = "a_broken.wav";
        bad.missing = true;
        bad.error = "Not a supported format";
        repo.upsert(bad);
        repo.addRoot(t.dir.getFullPathName());
    }
    MainComponent app(dbPath, false, {}, t.dir.getChildFile("analysis-settings.json"));
    app.setSize(1200, 800);
    app.resized();
    auto* library = dynamic_cast<FileListPanel*>(app.findChildWithID("library"));
    REQUIRE(library != nullptr);
    REQUIRE(library->getNumRows() == 2);
    auto* card = dynamic_cast<AnalysisCard*>(findDescendantWithID(app, "analysisDetails"));
    REQUIRE(card != nullptr);

    library->selectRows({ 0 });
    REQUIRE(card->content().title == "a_broken.wav");
    REQUIRE(card->content().notes.size() >= 2);
    juce::StringArray notes;
    for (const auto& note : card->content().notes) notes.add(note);
    REQUIRE(notes.contains("File is missing"));
    REQUIRE(notes.contains("Not a supported format"));

    library->selectRows({ 1 });
    REQUIRE(card->content().title == "b_ok.wav");
    REQUIRE(card->content().stats.size() >= 8);
    REQUIRE(card->content().tooltip.endsWith("b_ok.wav"));
}

TEST_CASE("Preview shows the format line and fits the 156 px panel", "[ui][studio]")
{
    TestDirectory t;
    AudioEngine engine(false);
    PreviewPanel panel(engine, t.dir);
    REQUIRE(panel.loadFile(t.wave("fmt.wav", 440.0, 2), false));
    panel.setSize(1100, 156);
    panel.resized();
    auto* format = dynamic_cast<juce::Label*>(panel.findChildWithID("previewFormat"));
    REQUIRE(format != nullptr);
    REQUIRE(format->getText().contains("WAV"));
    REQUIRE(format->getText().contains("44.1"));
    REQUIRE(format->getText().contains("Stereo"));
    auto* play = buttonWithText(panel, "Play");
    REQUIRE(play != nullptr);
    REQUIRE(play->getWidth() == studio::circleButtonSize);
    for (auto* child : panel.getChildren())
        if (child->isVisible())
            REQUIRE(child->getBottom() <= panel.getHeight());
}

TEST_CASE("The split dialog keeps its sliders and visible check boxes", "[ui][studio]")
{
    TestDirectory t;
    SplitDialog dialog({ t.wave("a.wav") }, AppSettings{}, [](SplitOptions) { return true; });
    dialog.setSize(520, 430);
    dialog.resized();
    juce::Label* title = nullptr;
    int sliders = 0, checks = 0;
    for (auto* child : dialog.getChildren())
    {
        if (auto* label = dynamic_cast<juce::Label*>(child))
            if (label->getText().startsWith("Split ")) title = label;
        if (auto* slider = dynamic_cast<juce::Slider*>(child))
        {
            ++sliders;
            REQUIRE(slider->getWidth() > 150);
            REQUIRE(slider->getTextBoxWidth() == 90);
        }
        if (auto* toggle = dynamic_cast<juce::ToggleButton*>(child))
        {
            ++checks;
            REQUIRE(toggle->getHeight() == 24);
        }
        REQUIRE(child->getBounds().getRight() <= dialog.getWidth());
        REQUIRE(child->getBottom() <= dialog.getHeight());
    }
    REQUIRE(sliders == 3);
    REQUIRE(checks == 3);
    REQUIRE(title != nullptr);
    REQUIRE(title->getX() == 20);
    REQUIRE(title->getY() == 20);
    // The folder field sits on the window background so its edge shows on the panel.
    bool sawField = false;
    for (auto* child : dialog.getChildren())
        if (auto* editor = dynamic_cast<juce::TextEditor*>(child))
        {
            sawField = true;
            REQUIRE(editor->findColour(juce::TextEditor::backgroundColourId) == theme::background);
        }
    REQUIRE(sawField);
}

TEST_CASE("Every dialog lays its content out from the shared surface", "[ui][studio]")
{
    TestDirectory t;
    ExtractDialog extract({ juce::File("/tmp/bank.sf2") }, AppSettings{},
                          [](ExtractOptions) { return true; });
    Database db(t.dir.getChildFile("auto.sqlite"));
    SampleRepository repo(db);
    repo.createTables();
    AutoTagService service(repo);
    AutoTagResult result;
    result.checked = 1;
    result.scope = "selected files";
    AutoTagDialog autoTag(std::move(result), service, [] {});
    TapTempoDialog tap("Tap tempo");
    {
        // The idle value is an em dash, stored as a code point rather than a mishandled literal.
        bool foundValue = false;
        for (auto* child : tap.getChildren())
            if (auto* label = dynamic_cast<juce::Label*>(child); label != nullptr
                && label->getFont().getHeight() > 30.0f)
            {
                foundValue = true;
                REQUIRE(label->getText() == juce::String::charToString(0x2014));
            }
        REQUIRE(foundValue);
    }
    struct Case { juce::Component* dialog; int width, height; };
    for (const auto& one : std::array<Case, 3> { Case { &extract, 560, 500 },
                                                 Case { &autoTag, 980, 620 },
                                                 Case { &tap, 340, 260 } })
    {
        one.dialog->setSize(one.width, one.height);
        one.dialog->resized();
        for (auto* child : one.dialog->getChildren())
        {
            if (! child->isVisible()) continue;   // hidden controls keep their old bounds
            REQUIRE(child->getX() >= 20);
            REQUIRE(child->getY() >= 20);
            REQUIRE(child->getBounds().getRight() <= one.dialog->getWidth() - 20);
            REQUIRE(child->getBottom() <= one.dialog->getHeight() - 20);
        }
    }
}

TEST_CASE("The header keeps the search clear control in place", "[ui][studio]")
{
    TestDirectory t;
    MainComponent app(t.dir.getChildFile("clear.sqlite"), false);
    app.setSize(880, 580);
    app.resized();
    auto* search = dynamic_cast<SearchField*>(app.findChildWithID("search"));
    REQUIRE(search != nullptr);
    juce::Button* clear = nullptr;
    for (auto* child : search->getChildren())
        if (auto* button = dynamic_cast<juce::Button*>(child)) clear = button;
    REQUIRE(clear != nullptr);
    // The owner wants the control always in place: a control that vanishes is one a user hunts.
    REQUIRE(clear->isVisible());
    const auto pump = [] { juce::MessageManager::getInstance()->runDispatchLoopUntil(50); };
    search->setText("pad", true);
    pump();
    REQUIRE(clear->isVisible());
    clear->onClick();
    pump();
    REQUIRE(search->getText().isEmpty());
    REQUIRE(clear->isVisible());
}

TEST_CASE("A restored session shows its sample in the analysis card", "[ui][studio]")
{
    TestDirectory t;
    const auto dbPath = t.dir.getChildFile("restore.sqlite");
    const auto file = t.wave("restored.wav", 440.0, 1);
    {
        Database db(dbPath);
        SampleRepository repo(db);
        repo.createTables();
        auto record = BasicAnalyzer().analyze(file);
        record.fileName = "restored.wav";
        repo.upsert(record);
        repo.addRoot(t.dir.getFullPathName());
    }
    AppSettings settings;
    settings.lastFolder = t.dir.getFullPathName();
    settings.lastSamplePath = file.getFullPathName();
    MainComponent app(dbPath, false, settings, t.dir.getChildFile("restore-settings.json"));
    app.setSize(1200, 800);
    app.resized();
    auto* card = dynamic_cast<AnalysisCard*>(findDescendantWithID(app, "analysisDetails"));
    REQUIRE(card != nullptr);
    REQUIRE(card->content().title == "restored.wav");
    REQUIRE(card->content().stats.size() >= 8);
}

TEST_CASE("Analysis notes are drawn in the muted colour", "[ui][studio]")
{
    theme::apply(ThemeMode::Dark);
    AnalysisCard card;
    AnalysisContent content;
    content.notes = { "File is missing" };
    card.setContent(content);
    card.setSize(240, 60);
    card.resized();
    const auto image = card.createComponentSnapshot(card.getLocalBounds());
    auto distance = [](juce::Colour a, juce::Colour b)
    {
        return std::abs((int) a.getRed() - (int) b.getRed())
             + std::abs((int) a.getGreen() - (int) b.getGreen())
             + std::abs((int) a.getBlue() - (int) b.getBlue());
    };
    int glyphs = 0;
    long long toMuted = 0, toBlack = 0;
    for (int y = 0; y < image.getHeight(); ++y)
        for (int x = 0; x < image.getWidth(); ++x)
        {
            const auto pixel = image.getPixelAt(x, y);
            if (pixel.getAlpha() < 100) continue;       // the card paints no background
            ++glyphs;
            toMuted += distance(pixel, theme::muted);
            toBlack += distance(pixel, juce::Colours::black);
        }
    REQUIRE(glyphs > 0);
    // Every glyph must be recognisably the muted token, not the default black of an unset colour.
    REQUIRE(toMuted / glyphs < toBlack / glyphs);
    REQUIRE(toMuted / glyphs < 80);
}

TEST_CASE("The analysis meta line wraps in a narrow panel", "[ui][studio]")
{
    AnalysisCard card;
    AnalysisContent content;
    content.title = "x.wav";
    content.meta = "WAV \xc2\xb7 2 ch \xc2\xb7 44.1 kHz \xc2\xb7 16 bit";
    card.setContent(content);
    // Manrope sets this line about 8% tighter than the system sans, so the 154 px minimum panel
    // holds it on one line; the wrap has to be checked at a genuinely narrow width.
    card.setSize(120, 200);
    card.resized();
    REQUIRE(card.getNumChildComponents() == 2);
    REQUIRE(card.getChildComponent(1)->getHeight()
            >= 2 * juce::roundToInt(studio::captionFont().getHeight()));
}

TEST_CASE("Switching theme repaints the analysis card and the preview format line", "[ui][studio]")
{
    theme::apply(ThemeMode::Dark);
    SonicatorLookAndFeel look;
    AnalysisCard card;
    card.setLookAndFeel(&look);
    AnalysisContent content;
    content.title = "x.wav";
    card.setContent(content);
    juce::Label* title = nullptr;
    for (auto* child : card.getChildren())
        if (auto* label = dynamic_cast<juce::Label*>(child)) title = label;
    REQUIRE(title != nullptr);
    REQUIRE(title->findColour(juce::Label::textColourId) == theme::text);

    theme::apply(ThemeMode::Light);
    card.sendLookAndFeelChange();
    REQUIRE(title->findColour(juce::Label::textColourId) == theme::text);
    REQUIRE(theme::text.getPerceivedBrightness() < 0.5f);

    TestDirectory t;
    AudioEngine engine(false);
    PreviewPanel panel(engine, t.dir);
    panel.setLookAndFeel(&look);
    REQUIRE(panel.loadFile(t.wave("theme.wav", 440.0, 2), false));
    auto* format = dynamic_cast<juce::Label*>(panel.findChildWithID("previewFormat"));
    REQUIRE(format != nullptr);
    panel.lookAndFeelChanged();
    REQUIRE(format->findColour(juce::Label::textColourId) == theme::muted);
    theme::apply(ThemeMode::System);
}

TEST_CASE("A batch selection sizes the analysis card to its content", "[ui][studio]")
{
    TestDirectory t;
    const auto dbPath = t.dir.getChildFile("batch.sqlite");
    {
        Database db(dbPath);
        SampleRepository repo(db);
        repo.createTables();
        for (int i = 0; i < 3; ++i)
        {
            const auto name = "b" + juce::String(i) + ".wav";
            auto record = BasicAnalyzer().analyze(t.wave(name, 200.0 + 40.0 * i));
            record.fileName = name;
            repo.upsert(record);
        }
        repo.addRoot(t.dir.getFullPathName());
    }
    MainComponent app(dbPath, false, {}, t.dir.getChildFile("batch-settings.json"));
    app.setSize(1200, 800);
    app.resized();
    auto* library = dynamic_cast<FileListPanel*>(app.findChildWithID("library"));
    REQUIRE(library != nullptr);
    REQUIRE(library->getNumRows() == 3);
    library->selectRows({ 0, 1, 2 });
    auto* card = dynamic_cast<AnalysisCard*>(findDescendantWithID(app, "analysisDetails"));
    REQUIRE(card != nullptr);
    REQUIRE(card->content().stats.empty());
    REQUIRE(card->getHeight() == card->preferredHeight(card->getWidth()));
}

TEST_CASE("The search field keeps its placeholder hint", "[ui][studio]")
{
    TestDirectory t;
    MainComponent app(t.dir.getChildFile("hint.sqlite"), false);
    auto* search = dynamic_cast<SearchField*>(app.findChildWithID("search"));
    REQUIRE(search != nullptr);
    REQUIRE(search->getTextToShowWhenEmpty().contains("Search:"));
}

TEST_CASE("An empty search field shows its placeholder", "[ui][studio]")
{
    theme::apply(ThemeMode::Dark);
    SonicatorLookAndFeel look;
    SearchField search;
    search.setLookAndFeel(&look);
    search.setTextToShowWhenEmpty("Search: type:pad, key:Am", theme::muted);
    search.setSize(320, 32);
    const auto image = search.createComponentSnapshot(search.getLocalBounds());
    auto distance = [](juce::Colour a, juce::Colour b)
    {
        return std::abs((int) a.getRed() - (int) b.getRed())
             + std::abs((int) a.getGreen() - (int) b.getGreen())
             + std::abs((int) a.getBlue() - (int) b.getBlue());
    };
    int hintPixels = 0;
    for (int y = 0; y < image.getHeight(); ++y)
        for (int x = 34; x < image.getWidth(); ++x)   // right of the magnifier
            if (image.getPixelAt(x, y).getAlpha() > 100
                && distance(image.getPixelAt(x, y), theme::muted) < 90)
                ++hintPixels;
    REQUIRE(hintPixels > 20);
    theme::apply(ThemeMode::System);
}

TEST_CASE("The analysis card keeps its path tooltip reachable", "[ui][studio]")
{
    AnalysisCard card;
    AnalysisContent content;
    content.title = "x.wav";
    content.meta = "WAV";
    content.stats = { { "1.0 s", "Length" } };
    content.notes = { "/long/path/to/the/sample.wav" };
    content.tooltip = "/long/path/to/the/sample.wav";
    juce::Component host;
    host.setSize(240, 220);
    host.addAndMakeVisible(card);
    card.setBounds(0, 0, 240, 200);
    card.setContent(content);
    card.resized();
    // Tiles and notes must not swallow the mouse: the card owns the tooltip with the full path.
    // One point inside the first tile, one on the gap, one inside the note.
    for (const auto point : { juce::Point<int>(40, 50), juce::Point<int>(120, 40), juce::Point<int>(40, 130) })
        REQUIRE(card.getComponentAt(point) == &card);
}

TEST_CASE("Clicking the star asks the owner to toggle the favorite", "[ui][studio]")
{
    TestDirectory t;
    Database db(t.dir.getChildFile("star.sqlite"));
    SampleRepository repo(db);
    repo.createTables();
    auto record = BasicAnalyzer().analyze(t.wave("star.wav", 440.0, 1));
    record.fileName = "star.wav";
    repo.upsert(record);
    const auto stored = repo.page().front();
    FileListPanel panel;
    panel.setSamples(repo.page());
    panel.setSize(700, 300);
    panel.resized();
    REQUIRE(panel.getNumRows() == 1);
    int calls = 0;
    int64_t clicked = 0;
    panel.onStarClicked = [&](const SampleRecord& r) { ++calls; clicked = r.id; };
    panel.notifyStarClick(0);
    REQUIRE(calls == 1);
    REQUIRE(clicked == stored.id);
    // The click path ignores rows that do not exist.
    panel.notifyStarClick(5);
    REQUIRE(calls == 1);
}

TEST_CASE("The analysis panel shows key and tempo in its free space", "[ui][studio]")
{
    TestDirectory t;
    const auto dbPath = t.dir.getChildFile("hero.sqlite");
    const auto audio = t.wave("hero.wav", 440.0, 2);
    {
        Database db(dbPath);
        SampleRepository repo(db);
        repo.createTables();
        auto record = BasicAnalyzer().analyze(audio);
        record.fileName = "hero.wav";
        const auto id = repo.upsert(record);
        SmartResult smart;
        smart.bpm = 124.0; smart.hasBpm = true; smart.bpmState = SmartState::Resolved;
        smart.keyTonic = 9; smart.keyMode = "minor"; smart.keyState = SmartState::Resolved;
        smart.pipelineState = SmartPipelineState::Complete;
        smart.revision = 1;
        smart.extractorVersion = smartExtractorsVersion;
        smart.resolverVersion = smartResolverVersion;
        smart.dspVersion = smartDspVersion;
        repo.setSmartResult(id, smart);
        repo.addRoot(t.dir.getFullPathName());
    }
    MainComponent app(dbPath, false, {}, t.dir.getChildFile("hero-settings.json"));
    app.setSize(1200, 800);
    app.resized();
    auto* library = dynamic_cast<FileListPanel*>(app.findChildWithID("library"));
    REQUIRE(library != nullptr);
    library->selectRows({ 0 });
    auto* block = dynamic_cast<KeyBpmBlock*>(findDescendantWithID(app, "analysisKeyBpm"));
    REQUIRE(block != nullptr);
    REQUIRE(block->isVisible());
    REQUIRE(block->key() == "A minor");
    REQUIRE(block->bpm() == "124");
    REQUIRE(block->getHeight() == KeyBpmBlock::preferredHeight());
}
