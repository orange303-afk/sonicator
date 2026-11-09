#include <catch2/catch_test_macros.hpp>
#include "core/reason/ReasonPatch.h"
#include "TestHelpers.h"
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
}
TEST_CASE("Reason XML provides deviceProduct and ReFill is recognised without decoding", "[reason]")
{
    TestDirectory t; auto f=t.dir.getChildFile("p.repatch");
    f.replaceWithText("<JukeboxPatch version=\"2.0\" deviceProduct=\"se.propellerheads.Europa\"/>");
    auto r=readReasonPatch(f); REQUIRE(r.error.isEmpty()); REQUIRE(r.devices.contains("se.propellerheads.Europa"));
    REQUIRE(r.references.empty()); f.replaceWithText("<broken"); REQUIRE(readReasonPatch(f).error.isNotEmpty());
    f=t.dir.getChildFile("bank.rfl"); f.replaceWithText("Propellerheads ReFill File\x1a" "opaque");
    r=readReasonPatch(f); REQUIRE(r.error.isEmpty()); REQUIRE(r.message=="Reason ReFill: закрытый формат, содержимое недоступно");
    f.replaceWithText("not a refill"); REQUIRE(readReasonPatch(f).error.isNotEmpty());
}
