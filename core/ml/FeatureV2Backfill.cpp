#include "FeatureV2Backfill.h"
#include "core/dsp/AnalysisAudio.h"

namespace sonicator {
namespace {
constexpr double maxAnalysisSeconds = 10.0;
} // namespace

FeatureV2Result backfillFeatureV2(SampleRepository& repository, const CancelCheck& cancel)
{
    FeatureV2Result result;
    const auto rows = repository.allFeedback();
    for (const auto& row : rows)
    {
        if (cancel && cancel()) break;
        ++result.scanned;
        if (row.featureSchema >= 2) continue;

        const auto record = repository.findById(row.sampleId);
        if (! record)
        {
            ++result.missing;
            continue;
        }
        const juce::File file(record->filePath);
        if (! file.existsAsFile())
        {
            ++result.missing;
            continue;
        }

        AnalysisAudioOptions options;
        options.maxSeconds = maxAnalysisSeconds;
        const auto audio = readAnalysisAudio(file, options, cancel);
        if (! audio.ok())
        {
            ++result.failed;
            continue;
        }
        const auto vector = FeatureExtractorV2::extract(audio.buffer, audio.sampleRate);
        if (! vector.ok())
        {
            ++result.failed;
            continue;
        }
        repository.setFeedbackV2(row.sampleId, vector.values);
        ++result.updated;
    }
    return result;
}

} // namespace sonicator
