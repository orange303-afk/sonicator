#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "core/batch/BatchEncoder.h"
#include "core/batch/BatchMetadata.h"
#include "core/batch/BatchPaths.h"
#include "core/batch/BatchSources.h"
#include "core/audio/SonicatorFormats.h"
#include "TestHelpers.h"
#include <sys/stat.h>

using namespace sonicator;
using namespace sonicator::batch;

namespace
{
juce::File floatWav(const juce::File& file, const std::vector<float>& samples,
                    juce::StringPairArray metadata = {})
{
    file.deleteFile();
    file.getParentDirectory().createDirectory();
    juce::WavAudioFormat format;
    auto stream = std::unique_ptr<juce::OutputStream>(file.createOutputStream());
    REQUIRE(stream != nullptr);
    auto options = juce::AudioFormatWriterOptions{}.withSampleRate(44100.0).withNumChannels(1)
        .withBitsPerSample(32)
        .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
    for (int i = 0; i < metadata.size(); ++i)
        options = options.withMetadata(metadata.getAllKeys()[i], metadata.getAllValues()[i]);
    auto writer = format.createWriterFor(stream, options);
    REQUIRE(writer != nullptr);
    const float* channels[1] { samples.data() };
    REQUIRE(writer->writeFromFloatArrays(channels, 1, (int) samples.size()));
    writer.reset();
    stream.reset();
    return file;
}

SourceSnapshot snapshotFor(const juce::File& file)
{
    SourceSnapshot snapshot;
    snapshot.path = file;
    snapshot.identity = physicalIdentity(file);
    snapshot.fileSize = file.getSize();
    snapshot.modifiedMs = file.getLastModificationTime().toMilliseconds();
    juce::AudioFormatManager formats;
    registerSonicatorFormats(formats);
    auto reader = std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(file));
    REQUIRE(reader != nullptr);
    snapshot.sampleRate = (int) reader->sampleRate;
    snapshot.channels = (int) reader->numChannels;
    snapshot.bits = (int) reader->bitsPerSample;
    snapshot.floating = reader->usesFloatingPointData;
    snapshot.frames = reader->lengthInSamples;
    return snapshot;
}

std::unique_ptr<PreparedAudio> preparedFor(const juce::File& file, const Recipe& recipe)
{
    auto prepared = prepareAudio(snapshotFor(file), recipe);
    REQUIRE(prepared.ok);
    return std::move(prepared.audio);
}

OutputPlan planFor(const juce::File& source, Format format, Encoding encoding, int bits,
                   bool floating, const juce::File& target)
{
    OutputPlan plan;
    plan.format = format;
    plan.encoding = encoding;
    plan.bits = bits;
    plan.floating = floating;
    plan.sampleRate = 44100;
    plan.channels = 1;
    plan.target = target;
    (void) source;
    return plan;
}

bool fileContains(const juce::File& file, const std::vector<unsigned char>& needle)
{
    juce::MemoryBlock block;
    if (! file.loadFileAsData(block)) return false;
    const auto* begin = static_cast<const unsigned char*>(block.getData());
    return std::search(begin, begin + block.getSize(), needle.begin(), needle.end()) != begin + block.getSize();
}

std::vector<unsigned char> asciiBytes(const juce::String& text)
{
    std::vector<unsigned char> out;
    for (auto c : text) out.push_back((unsigned char) c);
    return out;
}

// Reads the frame count from a Xing/Info header. LAME reserves that frame at the start; a
// streaming encoder that never writes it back leaves the placeholder count at zero, which makes
// decoders estimate the length instead of reading it.
int tagFrameCount(const juce::File& file, const char* fourCC)
{
    juce::MemoryBlock block;
    if (! file.loadFileAsData(block)) return -1;
    const auto* begin = static_cast<const unsigned char*>(block.getData());
    const auto* end = begin + block.getSize();
    const unsigned char needle[4] { (unsigned char) fourCC[0], (unsigned char) fourCC[1],
                                    (unsigned char) fourCC[2], (unsigned char) fourCC[3] };
    const auto* found = std::search(begin, end, needle, needle + 4);
    if (found == end || found + 12 > end) return -1;
    return (int) (((unsigned int) found[8] << 24) | ((unsigned int) found[9] << 16)
                | ((unsigned int) found[10] << 8) | found[11]);
}

std::vector<unsigned char> utf8Bytes(const juce::String& text)
{
    const auto* data = reinterpret_cast<const unsigned char*>(text.toRawUTF8());
    return { data, data + text.getNumBytesAsUTF8() };
}

std::vector<unsigned char> utf16Bytes(const juce::String& text, bool bigEndian)
{
    std::vector<unsigned char> out;
    auto pointer = text.toUTF16();
    for (; ! pointer.isEmpty(); ++pointer)
    {
        const auto value = (unsigned short) *pointer;
        if (bigEndian) { out.push_back((unsigned char) (value >> 8)); out.push_back((unsigned char) value); }
        else { out.push_back((unsigned char) value); out.push_back((unsigned char) (value >> 8)); }
    }
    return out;
}

std::vector<float> readBack(const juce::File& file)
{
    juce::AudioFormatManager formats;
    registerSonicatorFormats(formats);
    auto reader = std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(file));
    REQUIRE(reader != nullptr);
    juce::AudioBuffer<float> buffer((int) reader->numChannels, (int) reader->lengthInSamples);
    reader->read(&buffer, 0, (int) reader->lengthInSamples, 0, true, true);
    std::vector<float> out((size_t) reader->lengthInSamples);
    for (int i = 0; i < (int) reader->lengthInSamples; ++i) out[(size_t) i] = buffer.getSample(0, i);
    return out;
}
} // namespace

TEST_CASE("Every supported WAV, AIFF and FLAC depth is written and validates",
          "[batch][batch-encoding]")
{
    TestDirectory t;
    const auto source = floatWav(t.dir.getChildFile("Source/tone.wav"),
                                 { 0.25f, -0.25f, 0.5f, -0.5f });
    const auto snapshot = snapshotFor(source);
    InputProtectionSet protection;
    protection.identities.push_back(snapshot.identity);
    protection.paths.push_back(snapshot.path.getFullPathName().toLowerCase());

    struct Case { Format format; Encoding encoding; int bits; bool floating; const char* ext; };
    const Case cases[] {
        { Format::wav, Encoding::int16, 16, false, ".wav" },
        { Format::wav, Encoding::int24, 24, false, ".wav" },
        { Format::wav, Encoding::float32, 32, true, ".wav" },
        { Format::aiff, Encoding::int16, 16, false, ".aif" },
        { Format::aiff, Encoding::int24, 24, false, ".aif" },
        { Format::flac, Encoding::int16, 16, false, ".flac" },
        { Format::flac, Encoding::int24, 24, false, ".flac" },
    };
    for (const auto& item : cases)
    {
        const auto target = t.dir.getChildFile("Out/" + juce::String((int) item.encoding)
                                               + juce::String("." + juce::String((int) item.format))
                                               + item.ext);
        Recipe recipe;
        recipe.outputs = { OutputPreset { item.format, item.encoding, 5 } };
        auto plan = planFor(source, item.format, item.encoding, item.bits, item.floating, target);
        auto result = renderAndPublish(preparedFor(source, recipe), plan, snapshot, recipe,
                                       ConflictPolicy::rename, protection);
        INFO("case " << (int) item.format << "/" << item.bits);
        REQUIRE(result.state == OutputState::completed);
        REQUIRE(result.target.existsAsFile());
        juce::AudioFormatManager formats;
        registerSonicatorFormats(formats);
        auto reader = std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(result.target));
        REQUIRE(reader != nullptr);
        REQUIRE(reader->lengthInSamples == 4);
        REQUIRE((int) reader->bitsPerSample == item.bits);
        REQUIRE(reader->usesFloatingPointData == item.floating);
        REQUIRE(reader->numChannels == 1);
    }

    // FLAC compression levels 1..8 are accepted; an invalid format/depth pair is not.
    for (int level = 1; level <= 8; ++level)
    {
        EncodeSettings settings;
        settings.format = Format::flac;
        settings.bits = 16;
        settings.sampleRate = 44100;
        settings.channels = 1;
        settings.flacCompressionLevel = level;
        Recipe recipe;
        recipe.outputs = { OutputPreset { Format::flac, Encoding::int16, level } };
        auto plan = planFor(source, Format::flac, Encoding::int16, 16, false,
                            t.dir.getChildFile("Levels/level" + juce::String(level) + ".flac"));
        auto result = renderAndPublish(preparedFor(source, recipe), plan, snapshot, recipe,
                                       ConflictPolicy::rename, protection);
        REQUIRE(result.state == OutputState::completed);
    }

    EncodeSettings invalid;
    invalid.format = Format::aiff;
    invalid.bits = 32;
    invalid.floating = true;
    invalid.sampleRate = 44100;
    invalid.channels = 1;
    Recipe recipe;
    auto prepared = preparedFor(source, recipe);
    auto converted = std::make_unique<ConvertedStream>(std::move(prepared), 44100, 1);
    const auto bad = encodeToTemporary(t.dir.getChildFile("bad.aif"), invalid, *converted, {});
    REQUIRE_FALSE(bad.ok);
}

TEST_CASE("Clipping is rejected by default and reported when explicitly allowed",
          "[batch][batch-encoding]")
{
    TestDirectory t;
    const auto source = floatWav(t.dir.getChildFile("Source/hot.wav"), { 0.9f, -0.9f });
    const auto snapshot = snapshotFor(source);
    InputProtectionSet protection;
    protection.identities.push_back(snapshot.identity);
    protection.paths.push_back(source.getFullPathName().toLowerCase());
    Recipe recipe;
    recipe.steps.push_back({ true, Gain { 6.0 } });
    recipe.outputs = { OutputPreset { Format::wav, Encoding::int16, 5 } };

    const auto rejectedTarget = t.dir.getChildFile("Out/rejected.wav");
    auto rejected = renderAndPublish(preparedFor(source, recipe),
        planFor(source, Format::wav, Encoding::int16, 16, false, rejectedTarget), snapshot, recipe,
        ConflictPolicy::rename, protection);
    REQUIRE(rejected.state == OutputState::failed);
    REQUIRE(rejected.code == ErrorCode::clipping_prevented);
    REQUIRE_FALSE(rejectedTarget.exists());
    REQUIRE_FALSE(createBatchTemporary(rejectedTarget).exists());

    recipe.allowIntegerClipping = true;
    const auto allowedTarget = t.dir.getChildFile("Out/allowed.wav");
    auto allowed = renderAndPublish(preparedFor(source, recipe),
        planFor(source, Format::wav, Encoding::int16, 16, false, allowedTarget), snapshot, recipe,
        ConflictPolicy::rename, protection);
    REQUIRE(allowed.state == OutputState::completed);
    bool clippingWarning = false;
    for (const auto& warning : allowed.warnings)
        if (warning.code == ErrorCode::integer_clipped) clippingWarning = true;
    REQUIRE(clippingWarning);

    Recipe floatRecipe;
    floatRecipe.steps.push_back({ true, Gain { 6.0 } });
    floatRecipe.outputs = { OutputPreset { Format::wav, Encoding::float32, 5 } };
    const auto floatTarget = t.dir.getChildFile("Out/float.wav");
    auto floatResult = renderAndPublish(preparedFor(source, floatRecipe),
        planFor(source, Format::wav, Encoding::float32, 32, true, floatTarget), snapshot,
        floatRecipe, ConflictPolicy::rename, protection);
    REQUIRE(floatResult.state == OutputState::completed);
    bool unityWarning = false;
    for (const auto& warning : floatResult.warnings)
        if (warning.code == ErrorCode::float_over_unity) unityWarning = true;
    REQUIRE(unityWarning);
}

TEST_CASE("Publication preserves the source and an older output on every failure",
          "[batch][batch-publish]")
{
    TestDirectory t;
    const auto source = floatWav(t.dir.getChildFile("Source/a.wav"), { 0.5f, -0.5f });
    const auto snapshot = snapshotFor(source);
    InputProtectionSet protection;
    protection.identities.push_back(snapshot.identity);
    protection.paths.push_back(source.getFullPathName().toLowerCase());
    Recipe recipe;
    recipe.outputs = { OutputPreset { Format::wav, Encoding::int16, 5 } };
    const auto target = source.getSiblingFile("a processed.wav");
    const auto oldBytes = juce::String("old output");
    target.replaceWithText(oldBytes);
    const auto oldHash = juce::SHA256(target).toHexString();

    auto prepared = preparedFor(source, recipe);
    // Change the source after preparation: publishing must refuse and keep both files.
    source.appendText("changed");
    const auto sourceHashAfterChange = juce::SHA256(source).toHexString();
    auto plan = planFor(source, Format::wav, Encoding::int16, 16, false, target);
    plan.replaceExisting = true;
    plan.previousFingerprint = physicalIdentity(target);
    auto result = renderAndPublish(std::move(prepared), plan, snapshot, recipe,
                                   ConflictPolicy::rename, protection);
    REQUIRE(result.state == OutputState::failed);
    REQUIRE(result.code == ErrorCode::source_changed);
    REQUIRE(juce::SHA256(target).toHexString() == oldHash);
    REQUIRE(juce::SHA256(source).toHexString() == sourceHashAfterChange);

    // A file that appears after preflight is never overwritten by Rename.
    const auto raceSource = floatWav(t.dir.getChildFile("Source/race.wav"), { 0.2f, -0.2f });
    const auto raceSnapshot = snapshotFor(raceSource);
    protection.identities.push_back(raceSnapshot.identity);
    protection.paths.push_back(raceSource.getFullPathName().toLowerCase());
    const auto raceTarget = raceSource.getSiblingFile("race processed.wav");
    auto racePlan = planFor(raceSource, Format::wav, Encoding::int16, 16, false, raceTarget);
    raceTarget.replaceWithText("external file");
    const auto externalHash = juce::SHA256(raceTarget).toHexString();
    auto race = renderAndPublish(preparedFor(raceSource, recipe), racePlan, raceSnapshot, recipe,
                                 ConflictPolicy::rename, protection);
    REQUIRE(race.state == OutputState::completed);
    REQUIRE(race.target != raceTarget);
    REQUIRE(juce::SHA256(raceTarget).toHexString() == externalHash);

    // Replacing an output that changed after preflight is refused.
    const auto replaceSource = floatWav(t.dir.getChildFile("Source/replace.wav"), { 0.1f });
    const auto replaceSnapshot = snapshotFor(replaceSource);
    protection.identities.push_back(replaceSnapshot.identity);
    protection.paths.push_back(replaceSource.getFullPathName().toLowerCase());
    const auto replaceTarget = replaceSource.getSiblingFile("replace processed.wav");
    replaceTarget.replaceWithText("first");
    auto replacePlan = planFor(replaceSource, Format::wav, Encoding::int16, 16, false,
                               replaceTarget);
    replacePlan.replaceExisting = true;
    replacePlan.previousFingerprint = physicalIdentity(replaceTarget);
    replaceTarget.replaceWithText("second");
    const auto secondHash = juce::SHA256(replaceTarget).toHexString();
    auto replace = renderAndPublish(preparedFor(replaceSource, recipe), replacePlan,
                                    replaceSnapshot, recipe, ConflictPolicy::replaceOutput,
                                    protection);
    REQUIRE(replace.state == OutputState::failed);
    REQUIRE(replace.code == ErrorCode::destination_changed);
    REQUIRE(juce::SHA256(replaceTarget).toHexString() == secondHash);
}

TEST_CASE("WAV metadata whitelist survives and time-indexed chunks are dropped with a warning",
          "[batch][batch-metadata]")
{
    TestDirectory t;
    juce::StringPairArray metadata;
    metadata.set("INAM", "Kick");
    metadata.set("NumSampleLoops", "1");
    metadata.set("MidiUnityNote", "36");
    metadata.set("Loop0Start", "10");
    metadata.set("Loop0End", "99");
    const auto source = floatWav(t.dir.getChildFile("Meta/meta.wav"), { 0.1f, 0.2f }, metadata);
    const auto snapshot = snapshotFor(source);
    const auto sidecar = source.getSiblingFile("meta.wav.asd");
    sidecar.replaceWithText("sidecar bytes");

    juce::AudioFormatManager formats;
    registerSonicatorFormats(formats);
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(source));
    REQUIRE(reader != nullptr);
    const auto untouched = prepareMetadata(*reader, Format::wav, false);
    REQUIRE(untouched.values["INAM"] == "Kick");
    REQUIRE(untouched.values["MidiUnityNote"] == "36");
    REQUIRE(untouched.values["Loop0Start"] == "10");

    const auto changed = prepareMetadata(*reader, Format::wav, true);
    REQUIRE(changed.values["INAM"] == "Kick");
    REQUIRE(changed.values["MidiUnityNote"] == "36");
    REQUIRE(changed.values["Loop0Start"].isEmpty());
    REQUIRE(changed.timeIndexedDropped);
    bool warned = false;
    for (const auto& issue : changed.warnings)
        if (issue.code == ErrorCode::metadata_not_preserved) warned = true;
    REQUIRE(warned);

    const auto flacMetadata = prepareMetadata(*reader, Format::flac, false);
    REQUIRE(flacMetadata.values.size() == 0);
    REQUIRE_FALSE(flacMetadata.warnings.empty());

    Recipe recipe;
    recipe.outputs = { OutputPreset { Format::wav, Encoding::int24, 5 } };
    InputProtectionSet protection;
    protection.identities.push_back(snapshot.identity);
    protection.paths.push_back(source.getFullPathName().toLowerCase());
    auto result = renderAndPublish(preparedFor(source, recipe),
        planFor(source, Format::wav, Encoding::int24, 24, false,
                source.getSiblingFile("meta processed.wav")), snapshot, recipe,
        ConflictPolicy::rename, protection);
    REQUIRE(result.state == OutputState::completed);
    REQUIRE(sidecar.existsAsFile());
    REQUIRE(sidecar.loadFileAsString() == "sidecar bytes");

    std::unique_ptr<juce::AudioFormatReader> written(formats.createReaderFor(result.target));
    REQUIRE(written != nullptr);
    REQUIRE(written->metadataValues["INAM"] == "Kick");
}

TEST_CASE("Validation can be cancelled and refuses an undecodable tail",
          "[batch][batch-encoding]")
{
    TestDirectory t;
    const auto source = floatWav(t.dir.getChildFile("Source/v.wav"), { 0.1f, -0.1f });
    Recipe recipe;
    recipe.outputs = { OutputPreset { Format::wav, Encoding::int16, 5 } };
    const auto snapshot = snapshotFor(source);
    InputProtectionSet protection;
    protection.identities.push_back(snapshot.identity);
    protection.paths.push_back(source.getFullPathName().toLowerCase());
    const auto target = t.dir.getChildFile("Out/v.wav");
    auto plan = planFor(source, Format::wav, Encoding::int16, 16, false, target);
    auto prepared = preparedFor(source, recipe);
    auto converted = std::make_unique<ConvertedStream>(std::move(prepared), 44100, 1);
    EncodeSettings settings;
    settings.format = Format::wav;
    settings.bits = 16;
    settings.sampleRate = 44100;
    settings.channels = 1;
    bool cancelNow = false;
    int phaseCalls = 0;
    auto outcome = encodeToTemporary(t.dir.getChildFile("Out/temp.wav"), settings, *converted,
        [&cancelNow] { return cancelNow; },
        [&cancelNow, &phaseCalls](const juce::String& phase)
        {
            ++phaseCalls;
            if (phase == "Validating") cancelNow = true;
        });
    REQUIRE_FALSE(outcome.ok);
    REQUIRE(outcome.code == ErrorCode::cancelled);
    REQUIRE_FALSE(t.dir.getChildFile("Out/temp.wav").exists());
    REQUIRE(phaseCalls > 0);
}

TEST_CASE("MP3 output is a readable lossy copy and honours the clipping policy",
          "[batch][batch-encoding]")
{
    TestDirectory t;
    std::vector<float> samples((size_t) 44100 * 2);
    for (size_t i = 0; i < samples.size(); ++i)
        samples[i] = (float) std::sin((double) i * 0.05) * 0.4f;
    juce::StringPairArray metadata;
    metadata.set("INAM", juce::String::fromUTF8("Тест"));
    metadata.set("IART", "Artist");
    const auto source = floatWav(t.dir.getChildFile("Source/mp3.wav"), samples, metadata);
    const auto hash = juce::SHA256(source).toHexString();
    const auto snapshot = snapshotFor(source);
    InputProtectionSet protection;
    protection.identities.push_back(snapshot.identity);
    protection.paths.push_back(source.getFullPathName().toLowerCase());
    Recipe recipe;
    OutputPreset mp3;
    mp3.format = Format::mp3;
    mp3.encoding = Encoding::int16;
    mp3.mp3BitrateKbps = 192;
    recipe.outputs = { mp3 };
    recipe.sampleRate = 44100;
    const auto target = t.dir.getChildFile("Out/mp3.mp3");
    auto plan = planFor(source, Format::mp3, Encoding::int16, 16, false, target);
    plan.mp3BitrateKbps = 192;
    auto result = renderAndPublish(preparedFor(source, recipe), plan, snapshot, recipe,
                                   ConflictPolicy::rename, protection);
    INFO(result.message);
    REQUIRE(result.state == OutputState::completed);
    REQUIRE(result.target.existsAsFile());
    REQUIRE(juce::SHA256(source).toHexString() == hash);
    REQUIRE(result.target.getSize() > 8000);
    REQUIRE(result.target.getSize() < 120000);
    REQUIRE(tagFrameCount(result.target, "Info") > 0);

    juce::AudioFormatManager formats;
    registerSonicatorFormats(formats);
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(result.target));
    REQUIRE(reader != nullptr);
    REQUIRE(reader->sampleRate == 44100.0);
    REQUIRE(reader->numChannels == 1);
    REQUIRE(std::abs(reader->lengthInSamples - 88200) <= 2 * 1152 + 4410);
    // ID3v2 frames are present and the Cyrillic title is stored as UTF-16.
    REQUIRE(fileContains(result.target, asciiBytes("TIT2")));
    REQUIRE(fileContains(result.target, asciiBytes("TPE1")));
    const auto titleUtf16 = juce::String::fromUTF8("Тест");
    REQUIRE((fileContains(result.target, utf16Bytes(titleUtf16, false))
             || fileContains(result.target, utf16Bytes(titleUtf16, true))));

    // VBR uses the quality control and produces a different, valid stream.
    Recipe vbrRecipe = recipe;
    vbrRecipe.outputs[0].mp3Mode = Mp3Mode::vbr;
    vbrRecipe.outputs[0].mp3VbrQuality = 2;
    const auto vbrTarget = t.dir.getChildFile("Out/mp3-vbr.mp3");
    auto vbrPlan = planFor(source, Format::mp3, Encoding::int16, 16, false, vbrTarget);
    vbrPlan.mp3Mode = Mp3Mode::vbr;
    vbrPlan.mp3VbrQuality = 2;
    auto vbrResult = renderAndPublish(preparedFor(source, vbrRecipe), vbrPlan, snapshot,
                                      vbrRecipe, ConflictPolicy::rename, protection);
    INFO(vbrResult.message);
    REQUIRE(vbrResult.state == OutputState::completed);
    REQUIRE(tagFrameCount(vbrResult.target, "Xing") > 0);
    std::unique_ptr<juce::AudioFormatReader> vbrReader(formats.createReaderFor(vbrResult.target));
    REQUIRE(vbrReader != nullptr);
    REQUIRE(std::abs(vbrReader->lengthInSamples - 88200) <= 2 * 1152 + 4410);

    // Ogg Vorbis carries the same text tags as comments.
    Recipe oggRecipe = recipe;
    OutputPreset ogg;
    ogg.format = Format::ogg;
    ogg.encoding = Encoding::int16;
    ogg.oggQuality = 8;
    oggRecipe.outputs = { ogg };
    const auto oggTarget = t.dir.getChildFile("Out/tone.ogg");
    auto oggPlan = planFor(source, Format::ogg, Encoding::int16, 16, false, oggTarget);
    oggPlan.oggQuality = 8;
    auto oggResult = renderAndPublish(preparedFor(source, oggRecipe), oggPlan, snapshot,
                                      oggRecipe, ConflictPolicy::rename, protection);
    REQUIRE(oggResult.state == OutputState::completed);
    std::unique_ptr<juce::AudioFormatReader> oggReader(formats.createReaderFor(oggResult.target));
    REQUIRE(oggReader != nullptr);
    REQUIRE(oggReader->sampleRate == 44100.0);
    REQUIRE(oggReader->numChannels == 1);
    REQUIRE(std::abs(oggReader->lengthInSamples - 88200) <= 4096 + 4410);
    // Vorbis comments are UTF-8; verify the bytes rather than JUCE's Latin-1 reader view.
    REQUIRE(fileContains(oggResult.target, utf8Bytes(titleUtf16)));
    REQUIRE(oggReader->metadataValues[juce::OggVorbisAudioFormat::id3title].isNotEmpty());

    Recipe loud = recipe;
    loud.steps = { { true, Gain { 12.0 } } };
    loud.allowIntegerClipping = false;
    const auto blockedTarget = t.dir.getChildFile("Out/mp3-loud.mp3");
    auto blockedPlan = planFor(source, Format::mp3, Encoding::int16, 16, false, blockedTarget);
    blockedPlan.mp3BitrateKbps = 128;
    auto blocked = renderAndPublish(preparedFor(source, loud), blockedPlan, snapshot, loud,
                                    ConflictPolicy::rename, protection);
    REQUIRE(blocked.state == OutputState::failed);
    REQUIRE(blocked.code == ErrorCode::clipping_prevented);
    REQUIRE_FALSE(blockedTarget.exists());

    Recipe invalid = recipe;
    invalid.outputs[0].mp3BitrateKbps = 10;
    const auto invalidIssues = validateRecipe(invalid);
    REQUIRE(invalidIssues.size() == 1);
    REQUIRE(invalidIssues[0].code == ErrorCode::invalid_encoding);
}

TEST_CASE("An unwritable destination fails without touching the source or old output",
          "[batch][batch-publish]")
{
    TestDirectory t;
    const auto source = floatWav(t.dir.getChildFile("Source/ro.wav"), { 0.2f, -0.2f });
    const auto hash = juce::SHA256(source).toHexString();
    const auto snapshot = snapshotFor(source);
    InputProtectionSet protection;
    protection.identities.push_back(snapshot.identity);
    protection.paths.push_back(source.getFullPathName().toLowerCase());
    const auto folder = t.dir.getChildFile("ReadOnly");
    folder.createDirectory();
    const auto target = folder.getChildFile("ro processed.wav");
    target.replaceWithText("old");
    const auto oldHash = juce::SHA256(target).toHexString();
#if JUCE_WINDOWS
    // Windows has no chmod. A folder's read-only attribute does not stop new files being written,
    // so this check cannot be expressed there; the guarded Unix case still proves the contract.
    SUCCEED("a read-only destination folder is a POSIX-only precondition");
#else
    REQUIRE(::chmod(folder.getFullPathName().toRawUTF8(), 0500) == 0);
    Recipe recipe;
    recipe.outputs = { OutputPreset { Format::wav, Encoding::int16, 5 } };
    auto plan = planFor(source, Format::wav, Encoding::int16, 16, false, target);
    auto result = renderAndPublish(preparedFor(source, recipe), plan, snapshot, recipe,
                                   ConflictPolicy::rename, protection);
    REQUIRE(::chmod(folder.getFullPathName().toRawUTF8(), 0700) == 0);
    REQUIRE(result.state == OutputState::failed);
    REQUIRE(juce::SHA256(source).toHexString() == hash);
    REQUIRE(juce::SHA256(target).toHexString() == oldHash);
#endif
}
