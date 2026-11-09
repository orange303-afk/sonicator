#include <catch2/catch_test_macros.hpp>
#include "core/dsp/RhythmAnalyzer.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <map>
#include <optional>
#include <set>
using namespace sonicator;
namespace {
// Evaluation-only reference parser. It is intentionally separate from the app's heuristics:
// this library writes the tempo as a bare token (`PACK_120_name.wav`) or explicitly (`128bpm`),
// and the app deliberately does not treat a bare number as a musical BPM.
constexpr double minReferenceBpm = 40.0, maxReferenceBpm = 240.0;

juce::StringArray tokensOf(const juce::String& text)
{
    return juce::StringArray::fromTokens(text.toLowerCase(), "_- ()[]", "");
}

std::set<double> explicitBpmValues(const juce::String& text)
{
    std::set<double> values;
    const auto tokens = tokensOf(text);
    for (int i = 0; i < tokens.size(); ++i)
    {
        const auto token = tokens[i];
        auto accept = [&](const juce::String& digits)
        {
            if (! digits.containsOnly("0123456789")) return;
            const auto value = digits.getDoubleValue();
            if (value >= minReferenceBpm && value <= maxReferenceBpm) values.insert(value);
        };
        if (token == "bpm" && i + 1 < tokens.size()) accept(tokens[i + 1]);
        if (token.startsWith("bpm")) accept(token.substring(3));
        if (token.endsWith("bpm")) accept(token.dropLastCharacters(3));
    }
    return values;
}

std::set<double> bareBpmValues(const juce::String& text)
{
    std::set<double> values;
    for (const auto& token : tokensOf(text))
    {
        if (! token.containsOnly("0123456789")) continue;
        const auto value = token.getDoubleValue();
        if (value >= minReferenceBpm && value <= maxReferenceBpm) values.insert(value);
    }
    return values;
}

struct Reference
{
    std::optional<double> bpm;
    juce::String source; // explicit, name, folder, ambiguous
};

Reference referenceBpm(const juce::File& file)
{
    const auto explicitValues = explicitBpmValues(file.getFileNameWithoutExtension());
    if (explicitValues.size() == 1) return { *explicitValues.begin(), "explicit" };
    if (explicitValues.size() > 1) return { std::nullopt, "ambiguous" };
    const auto nameValues = bareBpmValues(file.getFileNameWithoutExtension());
    if (nameValues.size() == 1) return { *nameValues.begin(), "name" };
    if (nameValues.size() > 1) return { std::nullopt, "ambiguous" };
    const auto folderValues = bareBpmValues(file.getParentDirectory().getFileName());
    if (folderValues.size() == 1) return { *folderValues.begin(), "folder" };
    return { std::nullopt, {} };
}

juce::String categoryOf(const juce::File& file)
{
    const auto path = file.getFullPathName().toLowerCase();
    if (path.contains("one_shot") || path.contains("oneshot") || path.contains("one shot"))
        return "one_shot";
    if (path.contains("loop")) return "loop";
    return "other";
}

bool nearHalfOrDouble(double a, double b)
{
    if (a <= 0 || b <= 0) return false;
    const double ratio = a > b ? a / b : b / a;
    return std::abs(ratio - 2.0) <= 0.05 * 2.0;
}

// A loop library cuts phrases to whole bars. A mislabelled sample number almost never lands on a
// whole bar, so this rejects bad reference labels before they can make the analysis look wrong.
double barDistance(double seconds, double bpm)
{
    if (seconds <= 0 || bpm <= 0) return 1.0;
    double distance = 1.0;
    for (double beatsPerBar : { 4.0, 3.0 })
    {
        const double bars = seconds * bpm / (60.0 * beatsPerBar);
        distance = std::min(distance, std::abs(bars - std::round(bars)));
    }
    return distance;
}

struct Tally
{
    int files = 0, labelled = 0, consistentLabels = 0, inconsistentLabels = 0;
    int loop = 0, oneShot = 0, undetermined = 0;
    int determined = 0, ambiguous = 0, unknown = 0;
    int correct = 0, halfDouble = 0, wrong = 0;
    int falseBpm = 0;
};

void row(std::ostream& out, const juce::String& name, const Tally& t)
{
    const double accuracy = t.determined > 0 ? 100.0 * t.correct / t.determined : 0.0;
    out << name << ": files=" << t.files;
    if (t.labelled > 0)
        out << " labels=" << t.labelled << " consistent=" << t.consistentLabels
            << " rejected=" << t.inconsistentLabels;
    out << " loop=" << t.loop << " one_shot=" << t.oneShot << " undetermined=" << t.undetermined
        << " published=" << t.determined << " ambiguous=" << t.ambiguous << " unknown=" << t.unknown;
    if (t.consistentLabels > 0)
        out << " correct=" << t.correct << " (" << juce::String(accuracy, 1) << "% of published)"
            << " x2/div2=" << t.halfDouble << " wrong=" << t.wrong
            << " recall=" << juce::String(100.0 * t.correct / t.consistentLabels, 1) << "%";
    if (t.falseBpm > 0) out << " false_bpm=" << t.falseBpm;
    out << "\n";
}
} // namespace

TEST_CASE("Evaluate rhythm analysis on a real loop library", "[.][rhythm][real]")
{
    const char* directory = std::getenv("SONICATOR_RHYTHM_EVAL_DIR");
    if (directory == nullptr || ! juce::File(directory).isDirectory())
    {
        SUCCEED("set SONICATOR_RHYTHM_EVAL_DIR to a folder of labelled samples");
        return;
    }
    const char* limitText = std::getenv("SONICATOR_RHYTHM_EVAL_LIMIT");
    const int limit = limitText != nullptr ? juce::String(limitText).getIntValue() : 0;
    const auto csvFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getChildFile("sonicator-rhythm-eval.csv");
    csvFile.deleteFile();
    std::unique_ptr<juce::FileOutputStream> csv(csvFile.createOutputStream());
    if (csv != nullptr)
        csv->writeText("path,category,reference_source,reference,structure,kind_confidence,"
                       "tempo,bpm,bpm_confidence,grid_strength,grid_coverage,grid_fill,"
                       "grid_beats,duration,error\n",
                       false, false, "\n");

    const juce::File root(directory);
    std::map<juce::String, Tally> byCategory;
    Tally all;
    int processed = 0, skipped = 0, reported = 0;
    const auto started = juce::Time::getMillisecondCounterHiRes();
    for (juce::DirectoryIterator iterator(root, true, "*.wav;*.aif;*.aiff;*.mp3;*.flac;*.ogg");
         iterator.next();)
    {
        const auto file = iterator.getFile();
        if (file.getFileName().startsWith("._")) continue;
        if (limit > 0 && processed >= limit) break;
        const auto reference = referenceBpm(file);
        const auto analysis = RhythmAnalyzer::analyzeFile(file);
        if (analysis.error.isNotEmpty()) { ++skipped; continue; }
        ++processed;
        const auto category = categoryOf(file);
        auto& tally = byCategory[category];
        for (Tally* target : { &all, &tally })
        {
            ++target->files;
            if (analysis.kind == RhythmKind::RhythmicLoop) ++target->loop;
            else if (analysis.kind == RhythmKind::OneShot) ++target->oneShot;
            else ++target->undetermined;
            if (analysis.tempo == RhythmTempo::Determined) ++target->determined;
            else if (analysis.tempo == RhythmTempo::Ambiguous) ++target->ambiguous;
            else ++target->unknown;
        }
        // A reference only means something for a loop: a one-shot carries its pack's tempo in the
        // name, not a tempo of its own.
        const bool useReference = reference.bpm.has_value() && category == "loop";
        juce::String error;
        if (useReference)
        {
            const bool consistent = barDistance(analysis.durationSeconds, *reference.bpm) <= 0.06
                                    || reference.source == "explicit";
            for (Tally* target : { &all, &tally })
            {
                ++target->labelled;
                if (consistent) ++target->consistentLabels; else ++target->inconsistentLabels;
            }
            if (! consistent)
            {
                error = "reference_inconsistent";
            }
            else if (analysis.tempo != RhythmTempo::Determined)
            {
                error = "not_published";
            }
            else
            {
                const double relative = std::abs(analysis.bpm - *reference.bpm) / *reference.bpm;
                if (relative <= 0.02)
                {
                    error = "correct";
                    for (Tally* target : { &all, &tally }) ++target->correct;
                }
                else if (nearHalfOrDouble(analysis.bpm, *reference.bpm))
                {
                    error = "half_double";
                    for (Tally* target : { &all, &tally }) ++target->halfDouble;
                }
                else
                {
                    error = "wrong";
                    for (Tally* target : { &all, &tally }) ++target->wrong;
                }
            }
        }
        else
        {
            error = reference.bpm.has_value() ? "reference_not_applicable" : "no_reference";
            if (category == "one_shot" && analysis.tempo == RhythmTempo::Determined)
                for (Tally* target : { &all, &tally }) ++target->falseBpm;
        }
        if (csv != nullptr)
        {
            csv->writeText(juce::String("\"") + file.getFullPathName().replace("\"", "\"\"") + "\","
                               + category + "," + reference.source + ","
                               + (reference.bpm.has_value() ? juce::String(*reference.bpm) : juce::String())
                               + "," + (analysis.kind == RhythmKind::RhythmicLoop ? "loop"
                                        : analysis.kind == RhythmKind::OneShot ? "one-shot" : "undetermined")
                               + "," + juce::String(analysis.kindConfidence, 3)
                               + "," + (analysis.tempo == RhythmTempo::Determined ? "determined"
                                        : analysis.tempo == RhythmTempo::Ambiguous ? "ambiguous" : "unknown")
                               + "," + juce::String(analysis.bpm, 3)
                               + "," + juce::String(analysis.bpmConfidence, 3)
                               + "," + juce::String(analysis.gridStrength, 3)
                               + "," + juce::String(analysis.gridCoverage, 3)
                               + "," + juce::String(analysis.gridFill, 3)
                               + "," + juce::String(analysis.gridBeats, 1)
                               + "," + juce::String(analysis.durationSeconds, 3)
                               + "," + error + "\n",
                           false, false, "\n");
        }
        if ((error == "wrong" || error == "half_double") && reported < 40)
        {
            ++reported;
            std::cout << "mismatch " << error << " ref=" << *reference.bpm
                      << " got=" << analysis.bpm << " s=" << analysis.durationSeconds
                      << " " << file.getFileName() << "\n";
        }
        if (processed % 500 == 0)
            std::cout << "progress " << processed << " files, "
                      << juce::String((juce::Time::getMillisecondCounterHiRes() - started) / 1000.0, 1)
                      << " s\n";
    }
    const auto elapsed = (juce::Time::getMillisecondCounterHiRes() - started) / 1000.0;
    std::cout << "\n=== rhythm evaluation ===\n";
    row(std::cout, "ALL", all);
    for (const auto& entry : byCategory) row(std::cout, entry.first, entry.second);
    std::cout << "unreadable files: " << skipped << "\n";
    std::cout << "elapsed seconds: " << juce::String(elapsed, 1) << "\n";
    std::cout << "CSV: " << csvFile.getFullPathName() << "\n";
    REQUIRE(processed > 0);
}
