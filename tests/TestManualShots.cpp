#include <catch2/catch_test_macros.hpp>
#include "ui/MainComponent.h"
#include "ui/settings/SettingsPanel.h"
#include "ui/split/SplitDialog.h"
#include "ui/extract/ExtractDialog.h"
#include "ui/tagging/AutoTagDialog.h"
#include "ui/batch/BatchProcessingDialog.h"
#include "ui/cloud/CloudView.h"
#include "ui/library/FileListPanel.h"
#include "ui/library/FolderTree.h"
#include "ui/preview/PreviewPanel.h"
#include "ui/shell/Theme.h"
#include "core/batch/BatchSources.h"
#include "core/database/Database.h"
#include "core/database/SampleRepository.h"
#include "core/dsp/BasicAnalyzer.h"
#include "core/settings/AppSettings.h"
#include "core/tagging/AutoTagService.h"
#include "plugin/PluginProcessor.h"
#include "plugin/PluginState.h"
#include "TestHelpers.h"

using namespace sonicator;

namespace manual
{
namespace
{
juce::File shotFolder()
{
    const auto path = juce::SystemStats::getEnvironmentVariable("SONICATOR_MANUAL_SHOT_DIR", {});
    if (path.isEmpty()) return {};
    juce::File folder(path);
    folder.createDirectory();
    return folder;
}

void write(const juce::Image& image, const juce::String& name)
{
    REQUIRE(image.isValid());
    REQUIRE(image.getWidth() > 300);
    const auto folder = shotFolder();
    if (folder == juce::File{}) return;
    const auto file = folder.getChildFile(name);
    file.deleteFile();
    auto stream = file.createOutputStream();
    REQUIRE(stream != nullptr);
    REQUIRE(juce::PNGImageFormat().writeImageToStream(image, *stream));
    std::printf("wrote %s (%d x %d)\n", file.getFullPathName().toRawUTF8(),
                image.getWidth(), image.getHeight());
}
} // namespace

void capture(juce::Component& component, int scale, const juce::String& name)
{
    write(component.createComponentSnapshot(component.getLocalBounds(), false, (float) scale), name);
}

void captureArea(juce::Component& component, juce::Rectangle<int> area, int scale,
                 const juce::String& name)
{
    write(component.createComponentSnapshot(area, false, (float) scale), name);
}

struct Seeded
{
    juce::File root, catalog;
    std::vector<int64_t> ids;
    std::map<juce::String, int64_t> byName;
    std::map<juce::String, int64_t> tags;
};

juce::StringArray baseNames()
{
    return {
        "Kick_808_Am_120.wav", "Kick_Acoustic_01.wav", "Kick_Boom_02.wav",
        "Snare_Tight_01.wav", "Snare_Rim_02.wav", "Snare_Clap_Layer.wav",
        "Hat_Closed_16.wav", "Hat_Open_08.wav", "Ride_Jazz_01.wav",
        "Bass_Sub_Cmin_140.wav", "Bass_Reese_Fmin_170.wav", "Bass_Pluck_Amin.wav",
        "Pad_Warm_Cmin.wav", "Pad_Glass_Gmaj.wav", "Pad_Analog_Strings.wav",
        "Loop_Dub_Chords_120.wav", "Loop_Breakbeat_170.wav", "Loop_Garage_Shuffle.wav",
        "Vox_Chop_Ah.wav", "Vox_Phrase_Hey.wav", "Vox_Breathe_Soft.wav",
        "Perc_Conga_01.wav", "Perc_Shaker_16.wav", "Perc_Cowbell.wav",
        "FX_Riser_02.wav", "FX_Impact_Deep.wav", "FX_Noise_Sweep.wav",
        "Keys_Rhodes_Chord.wav", "Keys_Organ_Stab.wav", "Guitar_Muted_Pluck.wav",
        "Tom_Low_01.wav", "Tom_High_02.wav", "Crash_Plate_01.wav",
        "Fill_Drums_120.wav", "Top_Loop_Hats_120.wav", "Breaks_Amen_Chop.wav",
        "Sub_808_Long.wav", "Stab_Chord_Dmin.wav", "Atmos_Cold_Room.wav",
        "Atmos_Cold_Room_alt.wav"
    };
}

juce::StringArray libraryNames()
{
    juce::StringArray names;
    const auto bases = baseNames();
    for (const auto& base : bases)
    {
        if (base.endsWith("_alt.wav"))
        {
            names.add(base);
            continue;
        }
        const auto stem = base.upToLastOccurrenceOf(".", false, false);
        for (int take = 0; take < 8; ++take)
            names.add(take == 0 ? base : stem + "_0" + juce::String(take + 1) + ".wav");
    }
    return names;
}

Seeded seedLibrary(const juce::File& root, TestDirectory& writer)
{
    Seeded s;
    s.root = root;
    s.catalog = root.getChildFile("manual.sqlite");
    root.createDirectory();
    const juce::StringArray folders { "Drums", "Bass", "Pads", "Loops", "Vocals", "Percussion",
                                      "FX", "Keys", "Guitar", "Textures" };
    for (const auto& folder : folders) root.getChildFile(folder).createDirectory();

    Database db(s.catalog);
    SampleRepository repo(db);
    repo.createTables();

    const auto names = libraryNames();
    int index = 0;
    for (const auto& name : names)
    {
        auto folder = folders[index % folders.size()];
        // A few rows live deeper to make the folder tree worth looking at.
        if (name.startsWith("Kick") || name.startsWith("Snare")) folder = "Drums/Kicks";
        if (name.startsWith("Hat") || name.startsWith("Ride")) folder = "Drums/Cymbals";
        if (name.startsWith("Pad")) folder = "Pads";
        if (name.startsWith("Loop") || name.startsWith("Top_Loop") || name.startsWith("Breaks"))
            folder = "Loops";
        if (name.startsWith("Vox")) folder = "Vocals";
        if (name.startsWith("Perc") || name.startsWith("Tom") || name.startsWith("Crash"))
            folder = "Percussion";
        if (name.startsWith("Keys") || name.startsWith("Stab")) folder = "Keys";
        if (name.startsWith("Atmos")) folder = "Textures";
        const auto hz = 55.0 + 11.0 * (index % 12);
        const auto seconds = 0.4 + 0.1 * (index % 9);
        const auto file = root.getChildFile(folder).getChildFile(name);
        file.getParentDirectory().createDirectory();
        // Identical parameters for the duplicate pair keep bytes, hash and size the same.
        if (name == "Atmos_Cold_Room_alt.wav")
            root.getChildFile("Textures").getChildFile("Atmos_Cold_Room.wav").copyFileTo(file);
        else
            // A small per-file length difference keeps every row unique; only the copied pair
            // below shares bytes, hash and size.
            writer.wave(file.getRelativePathFrom(root.getParentDirectory()), hz + index * 0.37,
                        index % 5 == 0 ? 2 : 1, (int) (44100 * seconds) + index * 7);
        auto record = BasicAnalyzer().analyzeAndHash(file);
        record.fileName = name;
        if (name == "FX_Impact_Deep.wav")
        {
            record.error = "Unsupported compressed variant";
            record.version = 0;
        }
        if (name == "Guitar_Muted_Pluck.wav") record.missing = true;
        const auto id = repo.upsert(record);
        s.ids.push_back(id);
        s.byName[name] = id;
        ++index;
    }

    repo.addRoot(root.getFullPathName());

    struct TagSpec { const char* name; uint32_t colour; };
    const TagSpec tagSpecs[] {
        { "Kick", 0xffe06c4a }, { "Snare", 0xffd9a441 }, { "Hat", 0xff7fb069 },
        { "Bass", 0xff5b8dd6 }, { "Pad", 0xff9b6bc4 }, { "Loop", 0xff4aa8a0 },
        { "Vocal", 0xffd4699b }, { "Percussion", 0xff8a8f98 }, { "FX", 0xffb5523f },
        { "Keys", 0xffc9a227 }, { "Dub", 0xff3f7fbf }, { "Favourite", 0xffe0b64a }
    };
    for (const auto& spec : tagSpecs)
        s.tags[spec.name] = repo.createTag(spec.name, spec.colour);

    auto assign = [&repo, &s, &names](const juce::String& prefix, const char* tag)
    {
        std::vector<int64_t> matching;
        for (const auto& name : names)
            if (name.startsWith(prefix)) matching.push_back(s.byName[name]);
        if (! matching.empty()) repo.assignTag(matching, s.tags[tag]);
    };
    assign("Kick", "Kick");
    assign("Snare", "Snare");
    assign("Hat", "Hat");
    assign("Bass", "Bass");
    assign("Sub", "Bass");
    assign("Pad", "Pad");
    assign("Loop", "Loop");
    assign("Breaks", "Loop");
    assign("Vox", "Vocal");
    assign("Perc", "Percussion");
    assign("FX", "FX");
    assign("Keys", "Keys");
    repo.assignTag({ s.byName["Loop_Dub_Chords_120.wav"] }, s.tags["Dub"]);
    repo.assignTag({ s.byName["Pad_Warm_Cmin.wav"] }, s.tags["Favourite"]);
    repo.setStarred({ s.byName["Kick_808_Am_120.wav"], s.byName["Snare_Tight_01.wav"],
                      s.byName["Pad_Warm_Cmin.wav"], s.byName["Loop_Dub_Chords_120.wav"],
                      s.byName["Vox_Chop_Ah.wav"], s.byName["Perc_Shaker_16.wav"] }, true);
    repo.refreshDuplicates({});

    auto setSmart = [&repo, &s](const char* name, SmartField field, const juce::String& text,
                                double numeric = 0, const juce::String& aux = {})
    {
        SmartOverride value;
        value.textValue = text;
        value.numericValue = numeric;
        value.auxText = aux;
        repo.setSmartOverride(s.byName[name], field, value);
    };
    setSmart("Kick_808_Am_120.wav", SmartField::Type, "Kick");
    setSmart("Kick_808_Am_120.wav", SmartField::Key, "Am", 9, "minor");
    setSmart("Kick_808_Am_120.wav", SmartField::Bpm, "120", 120);
    setSmart("Kick_808_Am_120.wav", SmartField::RootNote, "C4", 60);
    setSmart("Kick_Acoustic_01.wav", SmartField::Type, "Kick");
    setSmart("Kick_Boom_02.wav", SmartField::Type, "Kick");
    setSmart("Snare_Tight_01.wav", SmartField::Type, "Snare");
    setSmart("Snare_Clap_Layer.wav", SmartField::Type, "Snare");
    setSmart("Hat_Closed_16.wav", SmartField::Type, "Hat");
    setSmart("Bass_Sub_Cmin_140.wav", SmartField::Type, "Bass");
    setSmart("Bass_Sub_Cmin_140.wav", SmartField::Key, "Cmin", 0, "minor");
    setSmart("Bass_Sub_Cmin_140.wav", SmartField::Bpm, "140", 140);
    setSmart("Pad_Warm_Cmin.wav", SmartField::Type, "Pad");
    setSmart("Pad_Warm_Cmin.wav", SmartField::Key, "Cmin", 0, "minor");
    setSmart("Pad_Warm_Cmin.wav", SmartField::Bpm, "90", 90);
    setSmart("Pad_Warm_Cmin.wav", SmartField::Genre, "ambient");
    setSmart("Pad_Glass_Gmaj.wav", SmartField::Type, "Pad");
    setSmart("Loop_Dub_Chords_120.wav", SmartField::Type, "Loop");
    setSmart("Loop_Dub_Chords_120.wav", SmartField::Bpm, "120", 120);
    setSmart("Loop_Dub_Chords_120.wav", SmartField::Structure, "loop");
    setSmart("Vox_Chop_Ah.wav", SmartField::Type, "Vocal");
    setSmart("FX_Riser_02.wav", SmartField::Bpm, "128", 128);
    setSmart("FX_Riser_02.wav", SmartField::Structure, "one-shot");
    return s;
}
} // namespace manual

TEST_CASE("Manual plates render from the real components", "[.manual-shots]")
{
    const auto folder = manual::shotFolder();
    if (folder == juce::File{}) return;

    TestDirectory t;
    const auto seeded = manual::seedLibrary(t.dir.getChildFile("Library"), t);
    CHECK(seeded.ids.size() == 313);

    theme::apply(ThemeMode::Dark);
    AppSettings settings;
    settings.theme = ThemeMode::Dark;

    SonicatorLookAndFeel look;
    settings.scalePercent = 100;
    MainComponent app(seeded.catalog, false, settings, t.dir.getChildFile("manual-settings.json"));
    app.setSize(1400, 900);
    app.resized();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(500);

    auto* library = dynamic_cast<FileListPanel*>(findDescendantWithID(app, "library"));
    REQUIRE(library != nullptr);
    CHECK(pumpUntil([library] { return library->getNumRows() > 0; }, 20000));

    auto* search = dynamic_cast<juce::TextEditor*>(findDescendantWithID(app, "search"));
    REQUIRE(search != nullptr);

    // Plate 1: the workspace map, rendered at 2x for a landscape page.
    manual::capture(app, 2, "workspace.png");

    // Plate 2: the folder tree, open on a real subfolder.
    if (auto* tree = dynamic_cast<FolderTree*>(findDescendantWithID(app, "folderTree")))
    {
        tree->selectPath(seeded.root.getChildFile("Drums/Kicks").getFullPathName());
        juce::MessageManager::getInstance()->runDispatchLoopUntil(200);
        auto area = app.getLocalArea(tree, tree->getLocalBounds()).expanded(2);
        area.setHeight(juce::jmin(area.getHeight(), 470));
        manual::captureArea(app, area, 2, "folder-tree.png");
    }

    // Plate 3: the table, with the duplicate group and the selection bar in view.
    library->selectRows({ 0 });
    juce::MessageManager::getInstance()->runDispatchLoopUntil(300);
    manual::capture(*library, 2, "table.png");

    // Plate 4: a typed search, with the query visible in the field.
    app.searchLibrary("type:kick key:Am");
    search->setText("type:kick key:Am", juce::dontSendNotification);
    CHECK(pumpUntil([library] { return library->getNumRows() > 0; }, 5000));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(250);
    manual::captureArea(app, { 0, 0, library->getRight() + 2, library->getBottom() }, 2,
                        "search.png");
    app.searchLibrary({});
    search->setText({}, juce::dontSendNotification);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(250);

    // Plate 5: the tag filter and the selected-tag strip.
    app.setTagFilters({ seeded.tags.at("Kick") }, {}, false);
    library->selectRows({ 0, 1 });
    juce::MessageManager::getInstance()->runDispatchLoopUntil(300);
    manual::captureArea(app, { juce::jmax(0, library->getX() - 24),
                               juce::jmax(0, library->getY() - 46), 880,
                               library->getBottom() - library->getY() + 90 },
                        2, "tags.png");
    app.setTagFilters({}, {}, false);

    // Plate 6: the Analysis card on a file with Key and BPM.
    library->selectRows({ 0 });
    juce::MessageManager::getInstance()->runDispatchLoopUntil(400);
    if (auto* card = findDescendantWithID(app, "analysisDetails"))
        manual::captureArea(app, app.getLocalArea(card, card->getLocalBounds()).expanded(12), 2,
                            "analysis-card.png");
    if (auto* keyBpm = findDescendantWithID(app, "analysisKeyBpm"))
        manual::captureArea(app, app.getLocalArea(keyBpm, keyBpm->getLocalBounds()).expanded(12), 2,
                            "analysis-hero.png");

    // Plates 7 and 8: the preview band with a waveform and a prepared selection.
    auto* preview = dynamic_cast<PreviewPanel*>(findDescendantWithID(app, "preview"));
    REQUIRE(preview != nullptr);
    REQUIRE(preview->loadFile(juce::File(seeded.root.getChildFile(
        "Drums/Kicks/Kick_808_Am_120.wav")), false));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(400);
    {
        const auto bounds = preview->getLocalBounds();
        const int split = (int) (bounds.getWidth() * 0.62f);
        manual::captureArea(*preview, bounds.withWidth(split), 2, "preview-wave.png");
        manual::captureArea(*preview, bounds.withTrimmedLeft(split + 8), 2, "preview-controls.png");
    }
    preview->selectRange(0.25, 0.95);
    CHECK(pumpUntil([preview] { return preview->isSelectionReady(); }, 20000));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(150);
    manual::captureArea(*preview, preview->getLocalBounds().withWidth(
        (int) (preview->getWidth() * 0.66f)), 2, "selection.png");

    // Plate 9: the cloud map, overview and a zoomed neighbourhood.
    library->selectRows({});
    app.showCloudView(true);
    if (auto* cloud = dynamic_cast<cloud::CloudView*>(findDescendantWithID(app, "cloudView")))
    {
        CHECK(pumpUntil([cloud] { return cloud->hasLayout(); }, 60000));
        for (int i = 0; i < 200; ++i) cloud->onFrameForTest();
        juce::MessageManager::getInstance()->runDispatchLoopUntil(1200);
        for (int i = 0; i < 90; ++i) cloud->onFrameForTest();
        manual::captureArea(app, { 0, 0, library->getRight() + 2, cloud->getBottom() }, 2,
                            "cloud.png");
    }
    app.showCloudView(false);

    // Plates 10 and 11: Settings General and Hotkeys. The catalog path is the documented
    // default, written with a tilde, so the plate does not carry a developer's temp directory.
    SettingsPanel panel(settings, juce::File("/Users/Shared/Sonicator/sonicator.sqlite"), false);
    panel.setLookAndFeel(&look);
    panel.setCommandRegistry(&app.commandRegistry());
    panel.setSize(820, 640);
    panel.resized();
    panel.selectSection(SettingsSection::general);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(150);
    manual::captureArea(panel, { 0, 0, panel.getWidth(), 430 }, 2, "settings-general.png");
    panel.selectSection(SettingsSection::hotkeys);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(150);
    manual::captureArea(panel, { 0, 0, panel.getWidth(), 360 }, 2, "settings-hotkeys.png");
    panel.setLookAndFeel(nullptr);

    // Plate 12: the plugin window, rendered from the editor the host would open.
    {
        const auto pluginSettings = t.dir.getChildFile("plugin-settings.json");
        SettingsStore(pluginSettings).save(settings);
        plugin::PluginProcessor processor(seeded.catalog, pluginSettings);
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        REQUIRE(editor != nullptr);
        editor->setSize(1400, 820);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(500);
        manual::capture(*editor, 2, "plugin.png");
        editor.reset();
    }

    // The plugin owns its own theme choice; restore the standalone one before the dialogs.
    theme::apply(ThemeMode::Dark);
    look.refreshColours();

    // Plates 13-16: the dialogs the text sends the reader to.
    {
        Database db(seeded.catalog);
        SampleRepository repo(db);
        // Dialogs display the folder of their sources. A stable, plausible path keeps the plate
        // readable; these dialogs never open the file, and the real fixtures stay in the temp
        // library.
        const auto shown = juce::File("/Users/Shared/Sonicator Manual Library/Pads/Pad_Warm_Cmin.wav");

        SplitDialog split({ shown }, settings, [](SplitOptions) { return true; });
        split.setLookAndFeel(&look);
        split.setSize(560, 470);
        split.resized();
        juce::MessageManager::getInstance()->runDispatchLoopUntil(120);
        manual::capture(split, 2, "split.png");
        split.setLookAndFeel(nullptr);

        ExtractDialog extract({ shown }, settings, [](ExtractOptions) { return true; });
        extract.setLookAndFeel(&look);
        extract.setSize(560, 380);
        extract.resized();
        juce::MessageManager::getInstance()->runDispatchLoopUntil(120);
        manual::capture(extract, 2, "extract.png");
        extract.setLookAndFeel(nullptr);

        AutoTagService service(repo);
        std::vector<int64_t> autoTagIds;
        for (const auto& name : manual::libraryNames())
            if (name.startsWith("Ride") || name.startsWith("Tom") || name.startsWith("Crash")
                || name.startsWith("Fill") || name.startsWith("Top_Loop")
                || name.startsWith("Stab") || name.startsWith("Atmos"))
                autoTagIds.push_back(seeded.byName.at(name));
        REQUIRE(! autoTagIds.empty());
        REQUIRE(service.startSelected(autoTagIds));
        REQUIRE(service.waitUntilFinished(60000));
        AutoTagResult result;
        REQUIRE(service.takeResult(result));
        AutoTagDialog autoTag(result, service, [] {});
        autoTag.setLookAndFeel(&look);
        autoTag.setSize(980, 760);
        autoTag.resized();
        // The folder column would print a temp path; the chapter discusses file, tag and reason.
        std::function<juce::TableListBox*(juce::Component&)> firstTable =
            [&firstTable](juce::Component& root) -> juce::TableListBox*
            {
                if (auto* table = dynamic_cast<juce::TableListBox*>(&root)) return table;
                for (int i = 0; i < root.getNumChildComponents(); ++i)
                    if (auto* found = firstTable(*root.getChildComponent(i))) return found;
                return nullptr;
            };
        if (auto* table = firstTable(autoTag)) table->getHeader().setColumnVisible(3, false);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(150);
        manual::capture(autoTag, 2, "autotag.png");
        autoTag.setLookAndFeel(nullptr);

        batch::Request request;
        request.sources.kind = batch::SourceSelection::Kind::rows;
        request.sources.rows.assign(seeded.ids.begin(), seeded.ids.begin() + 8);
        request.destination.nextToSources = true;
        request.recipe.steps = {
            { true, batch::RemoveDc{} },
            { true, batch::TrimSilence{ -60.0, 5.0 } },
            { true, batch::Normalize{ -0.1 } },
            { true, batch::FadeOut{ 2.0, false } } };
        request.recipe.outputs = { batch::OutputPreset{} };
        auto prepared = batch::prepareBatch(repo, request, {});
        REQUIRE(prepared.ok);
        batch::BatchProcessingDialog dialog(prepared.batch,
                                            t.dir.getChildFile("batch-presets.json"));
        dialog.setLookAndFeel(&look);
        dialog.setSize(1000, 660);
        dialog.resized();
        juce::MessageManager::getInstance()->runDispatchLoopUntil(200);
        manual::capture(dialog, 2, "batch.png");
        dialog.setLookAndFeel(nullptr);
    }

    theme::apply(ThemeMode::System);
}
