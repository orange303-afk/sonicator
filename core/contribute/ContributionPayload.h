#pragma once
#include <juce_core/juce_core.h>
#include <functional>
#include <memory>
#include <vector>

// Opt-in contribution of confirmed feature-and-label pairs to the Sonicator training dataset.
// The payload never carries a path, a file name, a file hash, a sample id or anything else that
// points at the person or the files on disk; see docs/PRIVACY.md.
namespace sonicator::contribute {

// Bump when AdaptiveClassifier::extractFeatures changes shape or meaning. The intake rejects a
// vector of another length and the training stage keeps schemas apart.
inline constexpr int featureSchemaVersion = 1;
inline constexpr int featureCount = 14;
inline constexpr int maxRecordsPerBatch = 500;
inline constexpr int maxTextLength = 32;

struct Record
{
    juce::String recordId;     // stable id of the row in ml_feedback_samples
    juce::String type;         // empty when the person never confirmed a type
    bool typeConfirmed = true; // false when `type` is just what the app resolved
    juce::StringArray tags;
    std::vector<float> features;
    int featureSchema = 1;     // 1: 14 values, 2: 84 values
    juce::String key;          // empty when unknown
    double bpm = 0.0;
    bool hasBpm = false;
    int64_t updatedAt = 0;
};

// Names that ship with the app. Anything else is reported as a custom label, which is what the
// backlog wants to harvest.
struct Vocabulary
{
    juce::StringArray types;
    juce::StringArray tags;
};

struct Batch
{
    juce::String json;
    int recordCount = 0;
    int64_t watermark = 0;     // highest updatedAt included; commit only after a 2xx
};

// Three decimals is coarse enough for the model and keeps the vector from recomputing the exact
// audio measurements.
float quantize(float value);

// A label is dropped when it looks like a path, an address or a URL, since a custom tag is free
// text typed by the user and the only field that could carry something personal by accident.
bool isSafeLabel(const juce::String& text);

// Builds the request body. Records that cannot be sent safely are left out, so a batch can hold
// fewer records than it was given.
Batch buildBatch(const std::vector<Record>& records, const Vocabulary& vocabulary,
                 const juce::String& installId, const juce::String& batchId,
                 const juce::String& appVersion, const juce::String& os, juce::Time sentAt);

struct Response
{
    int status = 0;            // HTTP status, 0 when no response arrived
    juce::String error;        // transport error or response text
};

class Transport
{
public:
    virtual ~Transport() = default;
    virtual Response post(const juce::String& url, const juce::String& body) = 0;
    virtual Response remove(const juce::String& url) = 0;
};

// Real transport: one short request on a background thread, no cookies, no retries here.
class WebTransport final : public Transport
{
public:
    Response post(const juce::String& url, const juce::String& body) override;
    Response remove(const juce::String& url) override;
private:
    Response send(const juce::URL& url, const juce::String& method);
};

} // namespace sonicator::contribute
