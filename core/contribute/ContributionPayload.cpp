#include "ContributionPayload.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace sonicator::contribute {
namespace {
// The intake rejects a body above 256 KB. Keep a margin for the batch envelope and for JSON
// escaping, so a full 500-record fetch is trimmed to what the server accepts instead of failing
// the whole cycle with 413.
constexpr int maxBatchBodyBytes = 240 * 1024;

juce::String cleanLabel(const juce::String& text)
{
    const auto trimmed = text.trim();
    if (trimmed.isEmpty() || trimmed.length() > maxTextLength) return {};
    if (! isSafeLabel(trimmed)) return {};
    return trimmed;
}

float cleanFeature(float value)
{
    if (! std::isfinite(value)) return 0.0f;
    return quantize(juce::jlimit(0.0f, 1.0f, value));
}
} // namespace

float quantize(float value)
{
    if (! std::isfinite(value)) return 0.0f;
    const double scaled = std::round((double) value * 1000.0) / 1000.0;
    return (float) juce::jlimit(0.0, 1.0, scaled);
}

bool isSafeLabel(const juce::String& text)
{
    if (text.isEmpty()) return false;
    if (text.containsAnyOf("/\\@")) return false;
    if (text.contains("://")) return false;
    return true;
}

Batch buildBatch(const std::vector<Record>& records, const Vocabulary& vocabulary,
                 const juce::String& installId, const juce::String& batchId,
                 const juce::String& appVersion, const juce::String& os, juce::Time sentAt)
{
    juce::Array<juce::var> out;
    std::vector<int64_t> keptWatermarks;
    int64_t watermark = 0;
    size_t bodyBytes = 0;
    for (const auto& record : records)
    {
        if (out.size() >= maxRecordsPerBatch) break;
        if (record.recordId.isEmpty()) continue;
        const int expected = record.featureSchema == 2 ? 84 : featureCount;
        if ((int) record.features.size() != expected) continue;
        const auto type = record.typeConfirmed ? cleanLabel(record.type) : juce::String();

        juce::Array<juce::var> tags;
        int emitted = 0;
        for (const auto& raw : record.tags)
        {
            if (emitted >= 12) break;
            const auto name = cleanLabel(raw);
            if (name.isEmpty()) continue;
            auto* tag = new juce::DynamicObject();
            tag->setProperty("name", name);
            tag->setProperty("custom", ! vocabulary.tags.contains(name, true));
            tags.add(juce::var(tag));
            ++emitted;
        }
        // A tag action confirms tags, not the type the app resolved; that record carries tags only.
        if (type.isEmpty() && emitted == 0) continue;

        auto* entry = new juce::DynamicObject();
        entry->setProperty("recordId", record.recordId.toLowerCase());

        juce::Array<juce::var> features;
        for (const auto value : record.features) features.add((double) cleanFeature(value));
        entry->setProperty("features", features);
        entry->setProperty("featureSchema", record.featureSchema == 2 ? 2 : 1);

        if (type.isNotEmpty())
        {
            entry->setProperty("type", type);
            entry->setProperty("typeIsCustom", ! vocabulary.types.contains(type, true));
        }
        entry->setProperty("tags", tags);

        const auto key = cleanLabel(record.key);
        if (key.isNotEmpty()) entry->setProperty("key", key);
        if (record.hasBpm && record.bpm > 0.0 && record.bpm <= 1000.0)
            entry->setProperty("bpm", std::round(record.bpm * 10.0) / 10.0);

        const juce::var entryVar(entry);
        const auto entryBytes = (size_t) juce::JSON::toString(entryVar, false).length();
        if (! out.isEmpty() && bodyBytes + entryBytes + 1 > (size_t) maxBatchBodyBytes)
            break;
        out.add(entryVar);
        keptWatermarks.push_back(record.updatedAt);
        bodyBytes += entryBytes + 1;
        watermark = juce::jmax(watermark, record.updatedAt);
    }

    const auto makeBatch = [&]
    {
        auto* batch = new juce::DynamicObject();
        batch->setProperty("schema", "contribute.v1");
        batch->setProperty("installId", installId.toLowerCase());
        batch->setProperty("batchId", batchId.toLowerCase());
        batch->setProperty("appVersion", appVersion);
        batch->setProperty("os", os);
        batch->setProperty("sentAt", sentAt.toISO8601(true));
        batch->setProperty("records", out);
        Batch result;
        result.json = juce::JSON::toString(juce::var(batch), false);
        result.recordCount = out.size();
        result.watermark = keptWatermarks.empty() ? 0 : keptWatermarks.back();
        return result;
    };
    auto result = makeBatch();
    // The per-record estimate can drift from the pretty-printed body; trim until it really fits.
    while (out.size() > 1 && result.json.length() > maxBatchBodyBytes)
    {
        const auto ratio = (double) maxBatchBodyBytes / (double) result.json.length();
        auto remove = juce::jmax(1, (int) std::ceil((double) out.size() * (1.0 - ratio)));
        remove = juce::jmin(remove, out.size() - 1);
        for (int i = 0; i < remove; ++i)
        {
            out.removeLast();
            keptWatermarks.pop_back();
        }
        result = makeBatch();
    }
    juce::ignoreUnused(watermark);
    return result;
}

Response WebTransport::post(const juce::String& url, const juce::String& body)
{
    return send(juce::URL(url).withPOSTData(body), "POST");
}

Response WebTransport::remove(const juce::String& url)
{
    return send(juce::URL(url), "DELETE");
}

Response WebTransport::send(const juce::URL& url, const juce::String& method)
{
    auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                       .withHttpRequestCmd(method)
                       .withConnectionTimeoutMs(20000)
                       .withNumRedirectsToFollow(2)
                       .withExtraHeaders("Content-Type: application/json\r\nAccept: text/plain");
    auto stream = url.createInputStream(options);
    if (stream == nullptr) return { 0, "no connection" };
    Response response;
    // URL::createInputStream returns the web stream behind an InputStream, so ask it for the code.
    if (auto* web = dynamic_cast<juce::WebInputStream*>(stream.get()))
        response.status = web->getStatusCode();
    if (response.status < 200 || response.status >= 300)
        response.error = stream->readEntireStreamAsString().substring(0, 200);
    return response;
}

} // namespace sonicator::contribute
