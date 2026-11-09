#include <catch2/catch_test_macros.hpp>
#include <iostream>
#include <set>
#include "core/reason/ReasonPatch.h"
#include "TestHelpers.h"
#include "core/scanner/FileScanner.h"
#include "core/smart/AssetAnalyzer.h"
#include "core/indexing/IndexingService.h"
#include "core/settings/AppSettings.h"
#include "ui/preview/PreviewPanel.h"
#include "core/extract/EmbeddedSamples.h"
using namespace sonicator;
namespace {
using Bytes = std::vector<unsigned char>;
void be(Bytes& b, size_t n) { for (int shift : {24,16,8,0}) b.push_back((unsigned char)(n >> shift)); }
void text(Bytes& b, const juce::String& s)
{
    be(b, s.getNumBytesAsUTF8());
    const auto* p = s.toRawUTF8(); b.insert(b.end(), p, p + s.getNumBytesAsUTF8());
}
Bytes chunk(const char* tag, Bytes data)
{
    Bytes b(tag, tag + 4); be(b, data.size()); b.insert(b.end(), data.begin(), data.end());
    if (data.size() % 2) b.push_back(0); return b;
}
void append(Bytes& a, const Bytes& b) { a.insert(a.end(), b.begin(), b.end()); }
Bytes container(const char* tag, const char* type, const Bytes& data)
{
    Bytes b(type, type + 4); append(b, data); return chunk(tag, b);
}
Bytes reference(const juce::String& sample, const juce::String& refill = {})
{
    Bytes b {0xbc,1,3,0,0}; text(b, "_samples"); text(b, sample);
    if (refill.isNotEmpty()) text(b, refill);
    return chunk("REFE", b);
}
juce::File cmb(TestDirectory& t, const Bytes& refs)
{
    Bytes desc {0xbc,1,3,0,0,0xff}; text(desc,"Test Combinator");
    Bytes dev {0xbc,3,4,0,0,0xff,0,0,0,1}; text(dev,"Dr. Octo Rex");
    Bytes data = container("CAT ","REFS",refs);
    append(data,container("CAT ","DEVL",container("FORM","DEVI",chunk("DESC",dev))));
    append(data,chunk("DESC",desc));
    // This must never be interpreted as nested metadata.
    append(data,chunk("BODY",reference("hidden.wav")));
    const auto bytes = container("FORM","PTCH",data);
    auto f=t.dir.getChildFile("Pack/Patches/test.cmb"); f.getParentDirectory().createDirectory();
    f.replaceWithData(bytes.data(),bytes.size()); return f;
}
}
TEST_CASE("Reason CMB reads bounded metadata and deduplicates external references", "[reason]")
{
    TestDirectory t; auto local=t.wave("Pack/_samples/loop.wav");
    Bytes refs=reference("loop.wav"); append(refs,reference("loop.wav"));
    append(refs,reference("kick.wav","Drum Supply.rfl")); append(refs,reference("missing.aiff"));
    const auto result=readReasonPatch(cmb(t,refs));
    REQUIRE(result.error.isEmpty()); REQUIRE(result.name=="Test Combinator");
    REQUIRE(result.devices.contains("Dr. Octo Rex")); REQUIRE(result.references.size()==3);
    REQUIRE(result.references[0].file==local); REQUIRE(result.references[0].isLocal());
    REQUIRE(result.references[0].message.isEmpty());
    REQUIRE(result.references[1].refill=="Drum Supply.rfl");
    REQUIRE(result.references[1].message.contains(juce::String::fromUTF8("нужен ReFill")));
    REQUIRE_FALSE(result.references[2].isLocal()); REQUIRE(result.references[2].message.isNotEmpty());
}
TEST_CASE("Reason CMB refuses ambiguous missing truncated and oversized metadata", "[reason]")
{
    TestDirectory t; t.wave("Pack/_samples/a/loop.wav"); t.wave("Pack/_samples/b/loop.wav");
    auto f=cmb(t,reference("loop.wav"));
    auto result=readReasonPatch(f); REQUIRE(result.references.size()==1); REQUIRE_FALSE(result.references[0].isLocal());
    REQUIRE(result.references[0].message.isNotEmpty());
    juce::MemoryBlock bytes; f.loadFileAsData(bytes); bytes.setSize(bytes.getSize()-3);
    f.replaceWithData(bytes.getData(),bytes.getSize()); REQUIRE(readReasonPatch(f).error.isNotEmpty());
    f.replaceWithText("FORMxxxxPTCHREFE"); REQUIRE(readReasonPatch(f).error.isNotEmpty());
    const auto oversized=container("FORM","PTCH",chunk("DESC",Bytes(4*1024*1024+1,0)));
    f.replaceWithData(oversized.data(),oversized.size()); REQUIRE(readReasonPatch(f).error.isNotEmpty());
}
TEST_CASE("Reason XML provides deviceProduct and ReFill is recognised without decoding", "[reason]")
{
    TestDirectory t; auto f=t.dir.getChildFile("p.repatch");
    f.replaceWithText("<JukeboxPatch version=\"2.0\" deviceProduct=\"se.propellerheads.Europa\"/>");
    auto r=readReasonPatch(f); REQUIRE(r.error.isEmpty()); REQUIRE(r.devices.contains("se.propellerheads.Europa"));
    REQUIRE(r.references.empty()); f.replaceWithText("<broken"); REQUIRE(readReasonPatch(f).error.isNotEmpty());
    f.replaceWithText("<JukeboxPatch deviceProductID=\"se.propellerheads.Algoritm\"/>");
    r=readReasonPatch(f); REQUIRE(r.error.isEmpty()); REQUIRE(r.devices.contains("se.propellerheads.Algoritm"));
    f=t.dir.getChildFile("bank.rfl"); f.replaceWithText("Propellerheads ReFill File\x1a" "opaque");
    r=readReasonPatch(f); REQUIRE(r.error.isEmpty()); REQUIRE(r.message=="Reason ReFill: закрытый формат, содержимое недоступно");
    f.replaceWithText("not a refill"); REQUIRE(readReasonPatch(f).error.isNotEmpty());
}
TEST_CASE("Reason scanning indexes metadata and local audio but excludes only pack demos", "[reason]")
{
    TestDirectory t;
    const auto patch=cmb(t,{}); const auto wav=t.wave("Pack/_samples/loop.wav");
    t.wave("Pack/_audio previews/demo.wav"); t.wave("Pack/Audio Previews/demo.wav");
    t.wave("ordinary/Audio Previews/keep.wav");
    const auto xml=t.dir.getChildFile("p.repatch"); xml.replaceWithText("<JukeboxPatch deviceProduct=\"Europa\"/>");
    const auto rfl=t.dir.getChildFile("p.rfl"); rfl.replaceWithText("Propellerheads ReFill File\x1a");
    for (const auto& f:{patch,xml,rfl}) REQUIRE(FileScanner::isMetadataOnlyFormat(f));
    REQUIRE(FileScanner::scanDirectory(t.dir).size()==5);
    Database db(t.dir.getChildFile("catalog.sqlite")); SampleRepository repo(db); repo.createTables();
    IndexingService indexer(repo); REQUIRE(indexer.start({t.dir.getFullPathName()})); REQUIRE(indexer.waitUntilFinished(20000));
    REQUIRE(repo.count()==5);
    for (const auto& f:{patch,xml,rfl})
    {
        auto r=repo.findByPath(f.getFullPathName()); REQUIRE(r.has_value());
        REQUIRE(r->version==metadataOnlyVersion); REQUIRE(r->duration==0); REQUIRE(r->sampleRate==0);
        REQUIRE(r->compatibility==AssetCompatibility::supported); // supported metadata, not a playable archive
        REQUIRE(r->fileHash.isEmpty()); // metadata-only Reason reads must not hash BODY/archive payload
    }
    LibraryFilter filter; filter.hideEmpty=true; REQUIRE(repo.query(filter,100,0).total==5);
    REQUIRE(repo.findByPath(wav.getFullPathName())->error.isEmpty());
}
TEST_CASE("Reason demo exclusion does not hide ordinary libraries with common folder names", "[reason]")
{
    TestDirectory t;
    t.wave("Ordinary/_samples/source.wav");
    t.wave("Ordinary/Audio Previews/keep.wav");
    t.wave("Packs/Ordinary/Audio Previews/keep.wav");
    REQUIRE(FileScanner::scanDirectory(t.dir).size()==3);
}
TEST_CASE("Reason Packs registration persists once and respects removal", "[reason]")
{
    TestDirectory t; Database db(t.dir.getChildFile("catalog.sqlite")); SampleRepository repo(db); repo.createTables();
    SettingsStore settings(t.dir.getChildFile("prefs.json")); const auto root=t.dir.getChildFile("Packs");
    REQUIRE_FALSE(registerReasonPacks(repo,settings,root)); root.createDirectory();
    REQUIRE(registerReasonPacks(repo,settings,root)); REQUIRE(repo.roots().contains(root.getFullPathName()));
    settings.update([](AppSettings& s){s.theme=ThemeMode::Dark;});
    // Another empty catalog models removal or switching catalogs: never resurrect the root.
    Database other(t.dir.getChildFile("other.sqlite")); SampleRepository empty(other); empty.createTables();
    REQUIRE_FALSE(registerReasonPacks(empty,settings,root)); REQUIRE(empty.roots().isEmpty());
}
TEST_CASE("Reason preview shows devices and references and auditions only selected local audio", "[reason][ui]")
{
    TestDirectory t; auto wav=t.wave("Pack/_samples/loop.wav");
    Bytes refs=reference("loop.wav"); append(refs,reference("kick.wav","Drum Supply.rfl"));
    auto f=cmb(t,refs); AudioEngine engine(false); PreviewPanel panel(engine,t.dir.getChildFile("cache"));
    REQUIRE(panel.loadFile(f)); REQUIRE(engine.isReasonMode()); REQUIRE_FALSE(engine.isPlaying());
    REQUIRE_FALSE(engine.isSamplerInstrumentMode()); REQUIRE(engine.length()==0);
    auto* card=dynamic_cast<juce::Label*>(panel.findChildWithID("reasonInfo")); REQUIRE(card!=nullptr);
    REQUIRE(card->getText().contains("Dr. Octo Rex"));
    auto& list=panel.containerSourceList(); REQUIRE(list.getListBoxModel()->getNumRows()==2);
    list.selectRow(0); REQUIRE(engine.selectedReasonReference()==0); REQUIRE(engine.length()>0);
    REQUIRE(engine.isPlaying()); REQUIRE(panel.waveformLength()>0);
    list.selectRow(1); REQUIRE_FALSE(engine.isPlaying()); REQUIRE(engine.length()==0);
    REQUIRE(engine.error().contains("Drum Supply.rfl")); REQUIRE(panel.waveformLength()==0);
    REQUIRE_FALSE(engine.selectReasonReference(100));
    REQUIRE(panel.loadFile(wav,false)); REQUIRE_FALSE(engine.isReasonMode()); REQUIRE(list.getListBoxModel()->getNumRows()==0);
    REQUIRE_FALSE(isEmbeddedSampleSource(f));
    REQUIRE(panel.loadFile(f,false)); list.selectRow(0); REQUIRE(engine.isPlaying());
    panel.clearDiscSource(); REQUIRE_FALSE(engine.isReasonMode()); REQUIRE_FALSE(engine.isPlaying());
    REQUIRE_FALSE(card->isVisible()); REQUIRE(list.getListBoxModel()->getNumRows()==0);
    REQUIRE(panel.loadFile(f,false)); REQUIRE(engine.isReasonMode());
}
TEST_CASE("Reason metadata cards have no audio and reset when loading a disc source", "[reason][ui]")
{
    TestDirectory t; AudioEngine engine(false); PreviewPanel panel(engine,t.dir.getChildFile("cache"));
    auto f=t.dir.getChildFile("p.rfl"); f.replaceWithText("Propellerheads ReFill File\x1a");
    REQUIRE(panel.loadFile(f)); REQUIRE(engine.length()==0); REQUIRE_FALSE(engine.isPlaying());
    auto* card=dynamic_cast<juce::Label*>(panel.findChildWithID("reasonInfo")); REQUIRE(card!=nullptr);
    REQUIRE(card->isVisible()); REQUIRE(card->getText().contains(juce::String::fromUTF8("закрытый формат")));
    auto audio=std::make_shared<SampleAudio>(); audio->frames=441; audio->sampleRate=44100;
    audio->left.resize(441,0.1f); audio->right=audio->left;
    REQUIRE(panel.loadDiscSample(t.dir.getChildFile("d.iso"),"Disc sample",audio,false,0,0,false,false));
    REQUIRE_FALSE(card->isVisible());
    auto* play=dynamic_cast<juce::TextButton*>(panel.findChildWithID("play"));
    // Play must not remain disabled after the metadata-only card.
    for (auto* child:panel.getChildren()) if (auto* button=dynamic_cast<juce::TextButton*>(child))
        if (button->getButtonText()=="Play") play=button;
    REQUIRE(play!=nullptr); REQUIRE(play->isEnabled());
}
TEST_CASE("Installed Reason Packs metadata and sources can be opened read-only", "[reason][real]")
{
    const auto root=juce::File::getSpecialLocation(juce::File::userHomeDirectory)
        .getChildFile("Library/Application Support/Propellerhead Software/Packs");
    if (!root.isDirectory()) SKIP("Reason Packs not installed");
    const auto files=FileScanner::scanDirectory(root);
    int patches=0, local=0, refill=0, audio=0;
    std::set<juce::String> playedFormats;
    for (const auto& item:files)
    {
        REQUIRE_FALSE(item.file.getFullPathName().containsIgnoreCase("/Audio Previews/"));
        REQUIRE_FALSE(item.file.getFullPathName().containsIgnoreCase("/_audio previews/"));
        if (item.file.hasFileExtension("wav;aif;aiff;rx2"))
        {
            ++audio;
            if (playedFormats.insert(item.file.getFileExtension()).second)
            {
                AudioEngine engine(false); INFO(item.file.getFullPathName());
                REQUIRE(engine.loadFile(item.file)); REQUIRE(engine.length()>0);
            }
        }
        if (!item.file.hasFileExtension("cmb;repatch")) continue;
        auto patch=readReasonPatch(item.file); INFO(item.file.getFullPathName()); INFO(patch.error);
        REQUIRE(patch.error.isEmpty()); REQUIRE(patch.name.isNotEmpty());
        if (item.file.hasFileExtension("cmb")) ++patches;
        for (const auto& ref:patch.references)
        {
            if (ref.isLocal()) { ++local; REQUIRE(ref.file.isAChildOf(root)); }
            if (ref.refill.isNotEmpty()) ++refill;
        }
    }
    std::cout << "Installed Reason Packs: CMB="<<patches<<" audio="<<audio<<" local references="<<local<<" ReFill references="<<refill<<"\n";
    REQUIRE(patches>0); REQUIRE(audio>0); REQUIRE(local>0); REQUIRE(refill>0);
}
