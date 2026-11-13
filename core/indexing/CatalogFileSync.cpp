#include "CatalogFileSync.h"
#include "core/dsp/AnalysisAudio.h"
#include "core/dsp/BasicAnalyzer.h"
#include "core/dsp/FeatureExtractorV2.h"
#include "core/ml/AdaptiveClassifier.h"

namespace sonicator
{
namespace
{
bool cancelled(const CancelCheck& cancel) { return cancel && cancel(); }
} // namespace

CatalogSyncResult refreshCatalogFile(SampleRepository& repo, const juce::File& file,
                                     const CancelCheck& cancel)
{
    if (cancelled(cancel)) return { false, 0, "Cancelled" };
    const auto previous = repo.findByPath(file.getFullPathName());
    if (! previous.has_value()) return { false, 0, "File is not in the catalog" };
    if (! file.existsAsFile()) return { false, previous->id, "File not found" };

    auto record = BasicAnalyzer().analyzeAndHash(file, cancel);
    if (record.error.isNotEmpty()) return { false, previous->id, record.error };
    repo.upsert(record);
    const auto refreshed = repo.findByPath(file.getFullPathName());
    if (! refreshed.has_value()) return { false, previous->id, "The catalog row disappeared" };

    if (const auto feedback = repo.feedbackForSample(refreshed->id))
    {
        AnalysisAudioOptions options;
        options.maxSeconds = 30.0;
        const auto audio = readAnalysisAudio(file, options, cancel);
        auto featuresV1 = feedback->features;
        auto featuresV2 = feedback->featuresV2;
        if (audio.ok())
        {
            const auto extracted = AdaptiveClassifier::extractFeatures(audio.buffer, audio.sampleRate);
            if (! extracted.empty()) featuresV1 = extracted;
            const auto vector = FeatureExtractorV2::extract(audio.buffer, audio.sampleRate);
            if (vector.ok()) featuresV2 = vector.values;
        }
        repo.refreshFeedbackAudio(refreshed->id, record.fileHash, featuresV1, featuresV2);
    }
    return { true, refreshed->id, {} };
}

CatalogSyncResult addCatalogCopy(SampleRepository& repo, const juce::File& file,
                                 int64_t templateSampleId, const CancelCheck& cancel)
{
    if (cancelled(cancel)) return { false, 0, "Cancelled" };
    if (! file.existsAsFile()) return { false, 0, "File not found" };
    auto record = BasicAnalyzer().analyzeAndHash(file, cancel);
    if (record.error.isNotEmpty()) return { false, 0, record.error };
    repo.upsert(record);
    const auto added = repo.findByPath(file.getFullPathName());
    if (! added.has_value()) return { false, 0, "The copy was not added to the catalog" };

    if (templateSampleId > 0)
    {
        if (const auto source = repo.findById(templateSampleId))
        {
            for (const auto& tag : repo.tagsForSample(templateSampleId))
                repo.setSampleTag(added->id, tag.id, true);
            if (source->starred) repo.setStarred({ added->id }, true);
        }
    }
    return { true, added->id, {} };
}
} // namespace sonicator
