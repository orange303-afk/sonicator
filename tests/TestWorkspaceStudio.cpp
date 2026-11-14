#include <catch2/catch_test_macros.hpp>
#include "ui/MainComponent.h"
#include "ui/analysis/AnalysisCard.h"
#include "ui/library/FileListPanel.h"
#include "ui/library/SearchField.h"
#include "ui/preview/PreviewPanel.h"
#include "ui/split/SplitDialog.h"
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
    REQUIRE(settings->getWidth() == 32);
    REQUIRE(settings->getHeight() == 32);
    REQUIRE(settings->getRight() <= app.getWidth() - 8);
    REQUIRE(search->getRight() < settings->getX());
    REQUIRE(search->getWidth() >= 300);
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
    REQUIRE(AnalysisCard::columnsFor(270) == 2);
    REQUIRE(AnalysisCard::columnsFor(220) == 1);
    // Two columns are shorter than one, which is the whole point of the grid.
    REQUIRE(card.preferredHeight(270) < card.preferredHeight(220));
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
}
