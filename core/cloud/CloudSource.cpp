#include "CloudSource.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <set>

namespace sonicator::cloud
{
namespace
{
constexpr int layoutAlgorithmVersion = 1;

void hashByte(uint64_t& h, uint8_t b)
{
    h ^= b;
    h *= 1099511628211ull;
}
void hashInt(uint64_t& h, int64_t v)
{
    for (int i = 0; i < 8; ++i) hashByte(h, (uint8_t) ((uint64_t) v >> (8 * i)));
}
void hashFloat(uint64_t& h, float f)
{
    uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(f));
    std::memcpy(&bits, &f, sizeof(bits));
    for (int i = 0; i < 4; ++i) hashByte(h, (uint8_t) (bits >> (8 * i)));
}

uint32_t blendColour(const std::vector<Tag>& tags)
{
    if (tags.empty()) return 0xff8a8f9cu;
    double r = 0, g = 0, b = 0;
    for (const auto& t : tags)
    {
        r += (double) ((t.colour >> 16) & 0xff);
        g += (double) ((t.colour >> 8) & 0xff);
        b += (double) (t.colour & 0xff);
    }
    const auto n = (double) tags.size();
    auto channel = [n](double sum) { return (uint32_t) std::lround(sum / n); };
    return 0xff000000u | (channel(r) << 16) | (channel(g) << 8) | channel(b);
}
double db(double value) { return 20.0 * std::log10(std::max(value, 1e-9)); }
} // namespace

std::array<float, 8> basicFeaturesOf(const SampleRecord& r)
{
    return { (float) std::log1p(std::max(0.0, r.duration)),
             (float) db(r.rmsLevel),
             (float) db(r.peakLevel),
             (float) std::log1p(std::max(0.0, r.spectralCentroid)),
             (float) std::log1p(std::max(0.0, r.spectralRolloff)),
             (float) std::max(0.0, r.spectralFlatness),
             (float) std::log2(std::max(1.0, r.sampleRate) / 44100.0),
             (float) std::log2((double) std::max(1, r.numChannels)) };
}

CloudInput loadCloudInput(SampleRepository& repository)
{
    CloudInput input;
    auto samples = repository.taggedSamples();
    std::sort(samples.begin(), samples.end(),
              [](const SampleRecord& a, const SampleRecord& b) { return a.id < b.id; });

    auto tags = repository.tags();
    std::sort(tags.begin(), tags.end(), [](const Tag& a, const Tag& b) { return a.id < b.id; });

    std::vector<int64_t> ids;
    ids.reserve(samples.size());
    for (const auto& s : samples) ids.push_back(s.id);
    auto bySample = repository.tagsForSamples(ids);
    auto vectors = repository.feedbackV2();

    std::map<int64_t, int> tagUse;
    std::map<std::pair<int64_t, int64_t>, int> pairUse;
    input.nodes.reserve(samples.size());
    input.v2.reserve(samples.size());
    for (const auto& s : samples)
    {
        CloudNode node;
        node.sampleId = s.id;
        node.name = s.fileName;
        node.path = s.filePath;
        node.format = s.format;
        node.duration = s.duration;
        node.rate = s.sampleRate;
        node.channels = s.numChannels;
        node.dateAnalyzed = s.dateAnalyzed;
        auto found = bySample.find(s.id);
        if (found != bySample.end())
        {
            for (const auto& t : found->second) node.tagIds.push_back(t.id);
            std::sort(node.tagIds.begin(), node.tagIds.end());
            node.tagIds.erase(std::unique(node.tagIds.begin(), node.tagIds.end()), node.tagIds.end());
            node.colour = blendColour(found->second);
        }
        node.hasBasic = s.version >= 1 && s.error.isEmpty() && s.duration > 0 && s.sampleRate > 0;
        if (node.hasBasic) node.basic = basicFeaturesOf(s);
        auto vec = vectors.find(s.id);
        if (vec != vectors.end() && vec->second.size() == 84)
        {
            node.hasV2 = true;
            input.v2.push_back(vec->second);
        }
        else input.v2.emplace_back();
        for (auto tagId : node.tagIds) ++tagUse[tagId];
        for (size_t i = 0; i < node.tagIds.size(); ++i)
            for (size_t j = i + 1; j < node.tagIds.size(); ++j)
                ++pairUse[{ node.tagIds[i], node.tagIds[j] }];
        input.nodes.push_back(std::move(node));
    }
    for (const auto& t : tags)
        if (tagUse.count(t.id) > 0) input.tags.push_back(t);
    for (const auto& [pair, count] : pairUse)
        input.cooccurrence.push_back({ pair.first, pair.second, count });
    input.fingerprint = fingerprintCloudInput(input);
    return input;
}

uint64_t fingerprintCloudInput(const CloudInput& input)
{
    uint64_t h = 1469598103934665603ull; // FNV-1a offset basis
    hashInt(h, layoutAlgorithmVersion);
    hashInt(h, (int64_t) input.tags.size());
    for (const auto& t : input.tags)
    {
        hashInt(h, t.id);
        hashInt(h, (int64_t) (uint32_t) t.colour);
        hashInt(h, (int64_t) t.sampleCount);
    }
    hashInt(h, (int64_t) input.nodes.size());
    for (size_t i = 0; i < input.nodes.size(); ++i)
    {
        const auto& n = input.nodes[i];
        hashInt(h, n.sampleId);
        hashInt(h, n.dateAnalyzed);
        hashInt(h, (int64_t) n.tagIds.size());
        for (auto id : n.tagIds) hashInt(h, id);
        hashByte(h, (uint8_t) (n.hasBasic ? 1 : 0));
        hashByte(h, (uint8_t) (n.hasV2 ? 1 : 0));
        if (n.hasV2 && i < input.v2.size())
            for (auto value : input.v2[i]) hashFloat(h, value);
    }
    for (const auto& c : input.cooccurrence)
    {
        hashInt(h, c.tagA);
        hashInt(h, c.tagB);
        hashInt(h, c.count);
    }
    return h;
}
} // namespace sonicator::cloud
