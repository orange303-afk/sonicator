#pragma once
#include "core/database/SampleRecord.h"
#include "core/smart/SmartTypes.h"
#include "core/scanner/FileScanner.h"
#include "core/ml/OnnxAudioClassifier.h"
#include "core/ml/AdaptiveClassifier.h"
#include <juce_core/juce_core.h>
#include <vector>

namespace sonicator {

class AcousticClassifierService {
public:
    static std::vector<SmartCandidate> analyzeAudioAndContext(
        const juce::File& file,
        const SampleRecord& record,
        CancelCheck cancel = {}
    );

    static std::vector<SmartCandidate> analyzeAudioAndContext(
        const juce::File& file,
        const SampleRecord& record,
        const std::vector<SmartCandidate>& existingCandidates,
        CancelCheck cancel = {}
    );

    // Overload when audio buffer is already decoded
    static std::vector<SmartCandidate> analyzeBuffer(
        const juce::AudioBuffer<float>& buffer,
        double sampleRate,
        const juce::File& file,
        const std::vector<SmartCandidate>& existingCandidates = {}
    );

    static OnnxAudioClassifier& getSharedClassifier();
    static AdaptiveClassifier& getSharedAdaptiveClassifier();
};

} // namespace sonicator
