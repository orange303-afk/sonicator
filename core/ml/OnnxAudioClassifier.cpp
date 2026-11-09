#include "OnnxAudioClassifier.h"
#include "MelSpectrogram.h"
#include <onnxruntime_cxx_api.h>
#include <algorithm>
#include <cmath>
#include <mutex>
#include <string>
#include <vector>

namespace sonicator {

struct OnnxAudioClassifier::Impl {
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "SonicatorClassifier"};
    std::unique_ptr<Ort::Session> session;
    std::mutex mutex;

    std::string inputName;
    std::string outputName;
    std::vector<int64_t> inputDims;
    std::vector<int64_t> outputDims;
};

OnnxAudioClassifier::OnnxAudioClassifier()
    : impl(std::make_unique<Impl>())
{
}

OnnxAudioClassifier::~OnnxAudioClassifier() = default;

bool OnnxAudioClassifier::loadModelFromMemory(const void* data, size_t size)
{
    if (data == nullptr || size == 0)
        return false;

    std::lock_guard<std::mutex> lock(impl->mutex);
    try {
        Ort::SessionOptions sessionOptions;
        sessionOptions.SetIntraOpNumThreads(1);
        impl->session = std::make_unique<Ort::Session>(impl->env, data, size, sessionOptions);

        Ort::AllocatorWithDefaultOptions allocator;
        auto inAlloc = impl->session->GetInputNameAllocated(0, allocator);
        impl->inputName = inAlloc.get();

        auto outAlloc = impl->session->GetOutputNameAllocated(0, allocator);
        impl->outputName = outAlloc.get();

        impl->inputDims = impl->session->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
        impl->outputDims = impl->session->GetOutputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
        return true;
    }
    catch (...) {
        impl->session.reset();
        impl->inputName.clear();
        impl->outputName.clear();
        impl->inputDims.clear();
        impl->outputDims.clear();
        return false;
    }
}

bool OnnxAudioClassifier::loadModel(const juce::File& modelFile)
{
    if (!modelFile.existsAsFile())
        return false;

    juce::MemoryBlock block;
    if (!modelFile.loadFileAsData(block) || block.getSize() == 0)
        return false;

    return loadModelFromMemory(block.getData(), block.getSize());
}

bool OnnxAudioClassifier::isLoaded() const
{
    std::lock_guard<std::mutex> lock(impl->mutex);
    return impl->session != nullptr;
}

std::vector<MlPrediction> OnnxAudioClassifier::classify(const juce::AudioBuffer<float>& buffer,
                                                       double sampleRate,
                                                       int topK)
{
    if (topK <= 0 || buffer.getNumSamples() == 0 || buffer.getNumChannels() == 0 || sampleRate <= 40.0)
        return {};

    std::lock_guard<std::mutex> lock(impl->mutex);
    if (!impl->session)
        return {};

    const int melBands = 64;
    const int fftSize = 1024;
    const int hopSize = 512;

    int numFrames = MelSpectrogram::calculateNumFrames(buffer.getNumSamples(), fftSize, hopSize);
    if (numFrames <= 0)
        return {};

    auto melData = MelSpectrogram::computeLogMel(buffer, sampleRate, melBands, fftSize, hopSize);
    if (melData.empty())
        return {};

    try {
        // Resolve target tensor dimensions based on inputDims
        std::vector<int64_t> targetDims;
        if (impl->inputDims.empty())
        {
            targetDims = { 1, 1, static_cast<int64_t>(melBands), static_cast<int64_t>(numFrames) };
        }
        else
        {
            targetDims = impl->inputDims;
            if (targetDims.size() == 4)
            {
                if (targetDims[0] <= 0) targetDims[0] = 1;
                if (targetDims[1] <= 0) targetDims[1] = 1;
                if (targetDims[2] <= 0) targetDims[2] = melBands;
                if (targetDims[3] <= 0) targetDims[3] = numFrames;
            }
            else if (targetDims.size() == 3)
            {
                if (targetDims[0] <= 0) targetDims[0] = 1;
                if (targetDims[1] <= 0) targetDims[1] = melBands;
                if (targetDims[2] <= 0) targetDims[2] = numFrames;
            }
            else if (targetDims.size() == 2)
            {
                if (targetDims[0] <= 0) targetDims[0] = 1;
                if (targetDims[1] <= 0) targetDims[1] = static_cast<int64_t>(melData.size());
            }
            else if (targetDims.size() == 1)
            {
                if (targetDims[0] <= 0) targetDims[0] = static_cast<int64_t>(melData.size());
            }
        }

        int64_t totalElements = 1;
        for (auto d : targetDims)
            totalElements *= (d > 0 ? d : 1);

        std::vector<float> tensorInput(static_cast<size_t>(totalElements), 0.0f);
        size_t copyCount = std::min(melData.size(), static_cast<size_t>(totalElements));
        std::copy_n(melData.begin(), copyCount, tensorInput.begin());

        auto memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
            memoryInfo,
            tensorInput.data(),
            tensorInput.size(),
            targetDims.data(),
            targetDims.size()
        );

        const char* inputNames[] = { impl->inputName.c_str() };
        const char* outputNames[] = { impl->outputName.c_str() };

        auto outputTensors = impl->session->Run(
            Ort::RunOptions{nullptr},
            inputNames,
            &inputTensor,
            1,
            outputNames,
            1
        );

        if (outputTensors.empty())
            return {};

        float* outData = outputTensors[0].GetTensorMutableData<float>();
        size_t numOutputElements = outputTensors[0].GetTensorTypeAndShapeInfo().GetElementCount();
        if (numOutputElements == 0)
            return {};

        // Softmax over logits
        float maxVal = *std::max_element(outData, outData + numOutputElements);
        std::vector<float> probs(numOutputElements, 0.0f);
        float sumExp = 0.0f;
        for (size_t i = 0; i < numOutputElements; ++i)
        {
            probs[i] = std::exp(outData[i] - maxVal);
            sumExp += probs[i];
        }
        if (sumExp > 0.0f)
        {
            for (size_t i = 0; i < numOutputElements; ++i)
                probs[i] /= sumExp;
        }

        auto classes = supportedClasses();
        std::vector<MlPrediction> predictions;
        predictions.reserve(numOutputElements);

        for (size_t i = 0; i < numOutputElements; ++i)
        {
            MlPrediction pred;
            pred.className = (static_cast<int>(i) < classes.size())
                                 ? classes[static_cast<int>(i)]
                                 : ("Class_" + juce::String(static_cast<int>(i)));
            pred.probability = probs[i];
            predictions.push_back(pred);
        }

        std::sort(predictions.begin(), predictions.end(), [](const MlPrediction& a, const MlPrediction& b) {
            return a.probability > b.probability;
        });

        if (topK > 0 && static_cast<size_t>(topK) < predictions.size())
            predictions.resize(static_cast<size_t>(topK));

        return predictions;
    }
    catch (...) {
        return {};
    }
}

juce::StringArray OnnxAudioClassifier::supportedClasses()
{
    return {
        "Guitar", "Piano", "Pad", "Lead", "Strings",
        "Brass", "Vocal", "Bass", "Kick", "Snare", "Hat"
    };
}

} // namespace sonicator
