#include <catch2/catch_test_macros.hpp>
#include "core/dsp/AnalysisAudio.h"
#include "core/dsp/TonalAnalyzer.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <map>
#include <optional>
using namespace sonicator;
namespace {
struct ReferenceKey
{
    int tonic = -1; // 0 = C .. 11 = B
    juce::String mode;
};

int letterTonic(juce::juce_wchar c)
{
    switch (juce::CharacterFunctions::toUpperCase(c))
    {
        case 'C': return 0;
        case 'D': return 2;
        case 'E': return 4;
        case 'F': return 5;
        case 'G': return 7;
        case 'A': return 9;
        case 'B': return 11;
        default: return -1;
    }
}

std::optional<ReferenceKey> referenceKey(const juce::File& file)
{
    auto tokens = juce::StringArray::fromTokens(file.getFileNameWithoutExtension(), "_- ", "");
    if (tokens.isEmpty()) return std::nullopt;
    auto token = tokens[tokens.size() - 1].trim();
    token = token.trimCharactersAtStart("(").trimCharactersAtEnd(")");
    if (token.length() < 2 || token.length() > 6) return std::nullopt;
    int index = 0;
    const int letter = letterTonic(token[0]);
    if (letter < 0) return std::nullopt;
    ++index;
    int accidental = 0;
    if (index < token.length() && (token[index] == '#' || token[index] == 'b'))
    {
        accidental = token[index] == '#' ? 1 : -1;
        ++index;
    }
    const auto modeText = token.substring(index).toLowerCase();
    juce::String mode;
    if (modeText == "m" || modeText == "min" || modeText == "minor") mode = "minor";
    else if (modeText == "maj" || modeText == "major") mode = "major";
    else return std::nullopt;
    return ReferenceKey { (letter + accidental + 12) % 12, mode };
}

juce::String categoryOf(const juce::File& file)
{
    const auto path = file.getFullPathName().toLowerCase();
    if (path.contains("one_shot") || path.contains("oneshot") || path.contains("one shot"))
        return "one_shot";
    if (path.contains("drum") || path.contains("percussion") || path.contains("kick")
        || path.contains("snare") || path.contains("hihat") || path.contains("hi_hat"))
        return "drum";
    if (path.contains("loop") || path.contains("phrase") || path.contains("song_starter")
        || path.contains("songstarter"))
        return "loop";
    return "other";
}

uint64_t fnv1a(const juce::String& text)
{
    uint64_t hash = 1469598103934665603ull;
    const char* bytes = text.toRawUTF8();
    while (*bytes != '\0')
    {
        hash ^= (unsigned char) *bytes++;
        hash *= 1099511628211ull;
    }
    return hash;
}

bool relativeKeys(int tonicA, const juce::String& modeA, int tonicB, const juce::String& modeB)
{
    if (modeA == modeB || modeA.isEmpty() || modeB.isEmpty()) return false;
    const bool aMajor = modeA == "major", bMajor = modeB == "major";
    if (aMajor == bMajor) return false;
    if (aMajor) return ((tonicA + 9) % 12) == tonicB;
    return ((tonicB + 9) % 12) == tonicA;
}

struct Tally
{
    int files = 0, unreadable = 0;
    int labelled = 0, published = 0, correct = 0, bareTonic = 0;
    int wrongMode = 0, relative = 0, wrong = 0, unknown = 0;
    int unlabelled = 0, publishedUnlabelled = 0;

    void addReference(bool found)
    {
        ++labelled;
        if (found) ++published; else ++unknown;
    }
};

juce::String percent(int numerator, int denominator)
{
    if (denominator <= 0) return "-";
    return juce::String(100.0 * numerator / denominator, 1) + "%";
}

void printRow(std::ostream& out, const juce::String& name, const Tally& t)
{
    out << name << ": files=" << t.files << " unreadable=" << t.unreadable
        << " labelled=" << t.labelled << " published=" << t.published
        << " correct=" << t.correct << " (" << percent(t.correct, t.published)
        << " of published, " << percent(t.correct, t.labelled) << " of labelled)"
        << " bare_tonic=" << t.bareTonic << " wrong_mode=" << t.wrongMode
        << " relative=" << t.relative << " wrong=" << t.wrong
        << " unknown=" << t.unknown
        << " unlabelled=" << t.unlabelled << " published_unlabelled=" << t.publishedUnlabelled
        << "\n";
}

Tally& bucket(std::map<juce::String, Tally>& tallies,
              const juce::String& split, const juce::String& category)
{
    return tallies[split + "/" + category];
}
} // namespace

TEST_CASE("Evaluate key analysis on a real loop library", "[.][tonal][real]")
{
    const char* directory = std::getenv("SONICATOR_TONAL_EVAL_DIR");
    if (directory == nullptr || ! juce::File(directory).isDirectory())
    {
        SUCCEED("set SONICATOR_TONAL_EVAL_DIR to a folder of key-labelled samples");
        return;
    }
    const char* limitText = std::getenv("SONICATOR_TONAL_EVAL_LIMIT");
    const int limit = limitText != nullptr ? juce::String(limitText).getIntValue() : 0;
    const char* splitText = std::getenv("SONICATOR_TONAL_EVAL_SPLIT");
    const juce::String onlySplit = splitText != nullptr ? juce::String(splitText) : juce::String();
    TonalOptions options;
    auto readDouble = [](const char* name, double& target)
    {
        if (const char* value = std::getenv(name)) target = juce::String(value).getDoubleValue();
    };
    auto readInt = [](const char* name, int& target)
    {
        if (const char* value = std::getenv(name)) target = juce::String(value).getIntValue();
    };
    auto readBool = [](const char* name, bool& target)
    {
        if (const char* value = std::getenv(name)) target = juce::String(value).getIntValue() != 0;
    };
    readDouble("SONICATOR_TONAL_MIN_CORR", options.minKeyCorrelation);
    readDouble("SONICATOR_TONAL_BASS_WEIGHT", options.bassWeight);
    readDouble("SONICATOR_TONAL_REL_MARGIN", options.relativeMargin);
    readDouble("SONICATOR_TONAL_TONIC_MARGIN", options.minTonicMargin);
    readDouble("SONICATOR_TONAL_MODE_MARGIN", options.modeMargin);
    readDouble("SONICATOR_TONAL_MIN_TONALITY", options.minTonality);
    readDouble("SONICATOR_TONAL_PEAK", options.peakThreshold);
    readDouble("SONICATOR_TONAL_MAG_POWER", options.magnitudePower);
    readInt("SONICATOR_TONAL_MEDIAN", options.medianFrames);
    readBool("SONICATOR_TONAL_NORMALIZE", options.normalizeFrames);
    const auto csvFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getChildFile("sonicator-tonal-eval.csv");
    csvFile.deleteFile();
    std::unique_ptr<juce::FileOutputStream> csv(csvFile.createOutputStream());
    if (csv != nullptr)
        csv->writeText("path,split,category,reference_tonic,reference_mode,found,tonic,mode,"
                       "classification,confidence,best_correlation,runner_up_correlation,"
                       "tuning_cents,tonality,error,chroma,bass\n", false, false, "\n");

    const juce::File root(directory);
    std::map<juce::String, Tally> tallies;
    int processed = 0, skipped = 0;
    const auto started = juce::Time::getMillisecondCounterHiRes();
    for (juce::DirectoryIterator iterator(root, true, "*.wav;*.aif;*.aiff;*.mp3;*.flac;*.ogg");
         iterator.next();)
    {
        const auto file = iterator.getFile();
        if (file.getFileName().startsWith("._")) continue;
        if (limit > 0 && processed >= limit) break;
        const auto reference = referenceKey(file);
        const auto split = (fnv1a(file.getFullPathName()) % 2) == 0 ? "calibration" : "report";
        if (! onlySplit.isEmpty() && onlySplit != split) continue;
        const auto category = categoryOf(file);
        const auto audio = readAnalysisAudio(file);
        if (! audio.ok()) { ++skipped; continue; }
        const auto key = TonalAnalyzer::analyzeKey(audio.buffer, audio.sampleRate, options);
        if (key.error.isNotEmpty()) { ++skipped; continue; }
        ++processed;

        if (onlySplit.isEmpty() || onlySplit == split)
        {
            auto& all = tallies["all"];
            auto& splitAll = bucket(tallies, split, "all");
            auto& splitCategory = bucket(tallies, split, category);
            for (Tally* tally : { &all, &splitAll, &splitCategory }) ++tally->files;
        }
        juce::String classification = "unlabelled";
        if (reference.has_value())
        {
            if (onlySplit.isEmpty() || onlySplit == split)
            {
                auto& all = tallies["all"];
                auto& splitAll = bucket(tallies, split, "all");
                auto& splitCategory = bucket(tallies, split, category);
                for (Tally* tally : { &all, &splitAll, &splitCategory })
                    tally->addReference(key.found);
                if (! key.found) classification = "unknown";
                if (key.found)
                {
                    if (key.tonic == reference->tonic && key.mode == reference->mode)
                    {
                        classification = "correct";
                        for (Tally* tally : { &all, &splitAll, &splitCategory }) ++tally->correct;
                    }
                    else if (key.tonic == reference->tonic && key.mode.isEmpty())
                    {
                        classification = "bare_tonic";
                        for (Tally* tally : { &all, &splitAll, &splitCategory }) ++tally->bareTonic;
                    }
                    else if (key.tonic == reference->tonic)
                    {
                        classification = "wrong_mode";
                        for (Tally* tally : { &all, &splitAll, &splitCategory }) ++tally->wrongMode;
                    }
                    else if (relativeKeys(key.tonic, key.mode, reference->tonic, reference->mode))
                    {
                        classification = "relative";
                        for (Tally* tally : { &all, &splitAll, &splitCategory }) ++tally->relative;
                    }
                    else
                    {
                        classification = "wrong";
                        for (Tally* tally : { &all, &splitAll, &splitCategory }) ++tally->wrong;
                    }
                }
            }
        }
        else if (onlySplit.isEmpty() || onlySplit == split)
        {
            auto& all = tallies["all"];
            auto& splitAll = bucket(tallies, split, "all");
            auto& splitCategory = bucket(tallies, split, category);
            for (Tally* tally : { &all, &splitAll, &splitCategory })
            {
                ++tally->unlabelled;
                if (key.found) ++tally->publishedUnlabelled;
            }
        }
        if (csv != nullptr)
        {
            juce::StringArray chromaValues, bassValues;
            for (double value : key.chroma) chromaValues.add(juce::String(value, 3));
            for (double value : key.bassChroma) bassValues.add(juce::String(value, 3));
            csv->writeText(juce::String("\"") + file.getFullPathName().replace("\"", "\"\"") + "\","
                               + split + "," + category + ","
                               + (reference.has_value() ? juce::String(reference->tonic) : juce::String())
                               + "," + (reference.has_value() ? reference->mode : juce::String())
                               + "," + (key.found ? "1" : "0") + ","
                               + (key.found ? juce::String(key.tonic) : juce::String()) + ","
                               + (key.found ? key.mode : juce::String()) + ","
                               + classification + ","
                               + juce::String(key.confidence, 3) + ","
                               + juce::String(key.bestCorrelation, 3) + ","
                               + juce::String(key.runnerUpCorrelation, 3) + ","
                               + juce::String(key.tuningCents, 1) + ","
                               + juce::String(key.tonality, 3) + ","
                               + key.error + "," + chromaValues.joinIntoString(";")
                               + "," + bassValues.joinIntoString(";") + "\n",
                           false, false, "\n");
        }
        if (processed % 500 == 0)
            std::cout << "progress " << processed << " files, "
                      << juce::String((juce::Time::getMillisecondCounterHiRes() - started) / 1000.0, 1)
                      << " s\n";
    }
    const auto elapsed = (juce::Time::getMillisecondCounterHiRes() - started) / 1000.0;
    std::cout << "\n=== tonal key evaluation ===\n";
    printRow(std::cout, "ALL", tallies["all"]);
    for (const auto& entry : tallies)
        if (entry.first != "all") printRow(std::cout, entry.first, entry.second);
    std::cout << "unreadable files: " << skipped << "\n";
    std::cout << "elapsed seconds: " << juce::String(elapsed, 1) << "\n";
    std::cout << "CSV: " << csvFile.getFullPathName() << "\n";
    REQUIRE(processed > 0);
}
