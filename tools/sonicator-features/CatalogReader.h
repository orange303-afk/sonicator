#pragma once
#include "core/dsp/FeatureExtractorV2.h"
#include <juce_core/juce_core.h>
#include <vector>

namespace sonicator {

// One confirmed row of the owner's catalog, ready to be turned into a gold feature line. The path is
// kept only in memory: it is used to read the audio and never written to the gold file.
struct GoldRow
{
    juce::String recordId;
    juce::String type;
    juce::String tags;
    juce::String origin;
    bool typeIsCustom = false;
    juce::File file;
};

struct CatalogResult
{
    int written = 0;
    int missingFile = 0;
    int unreadable = 0;
    juce::String error;
};

// Reads ml_feedback_samples rows whose origin is a person's confirmation, joined with the catalog
// path of the sample. The database is opened read-only: no migration, no journal rewrite, safe while
// the application is running. On failure returns an empty vector and fills `error`.
std::vector<GoldRow> readGoldRows(const juce::File& database, juce::String& error);

// One JSON line without the path. The tags text of the catalog is a comma-joined list, emitted as a
// JSON array here.
juce::String goldJsonLine(const GoldRow& row, const FeatureVectorV2& vector);

// Computes v2 for every confirmed row and writes the gold file. `paths` is optional (empty file
// disables it) and stays local: it is the only place a path is written to.
CatalogResult writeCatalogGold(const juce::File& database, const juce::File& out, const juce::File& paths);

} // namespace sonicator
