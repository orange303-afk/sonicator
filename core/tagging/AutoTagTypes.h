#pragma once
#include "core/smart/SmartTypes.h"
#include <map>
#include <optional>
#include <vector>
namespace sonicator {
// Bump when a token, compound rule or evidence rule changes. A proposal prepared by an older
// rule set must not be applied without preparing it again.
inline constexpr int autoTagRulesVersion = 2;

enum class AutoTagRule { None, FilenameToken, FilenameCompound, MetadataType, SmartType };
juce::String autoTagRuleKey(AutoTagRule);

struct AutoTagProposal
{
    int64_t sampleId = 0;
    juce::String filePath, fileName;
    juce::String canonicalType;  // whitelist class, also the existing tag name
    int64_t tagId = 0;
    AutoTagRule rule = AutoTagRule::None;
    juce::String reason;         // human-readable evidence, e.g. filename token "snare"
    juce::String source;         // filename, wav_smpl, asd_warp, smart …
    bool alreadyAssigned = false;
    bool included = true;       // review control; already-assigned rows are never applied
    int64_t fileSize = 0, fileModified = 0; // revalidated before applying
};

struct AutoTagSkip
{
    int64_t sampleId = 0;
    juce::String filePath, fileName;
    juce::String code;           // stable reason code
    juce::String reason;         // readable explanation
    std::vector<juce::String> candidates;
};

struct AutoTagInput
{
    int64_t sampleId = 0;
    juce::String filePath;
    std::vector<SmartCandidate> candidates;
    std::optional<SmartResult> result;
    std::optional<SmartOverride> typeOverride; // nullopt means no manual override
    bool inputCurrent = true;                  // file size/mtime still match the stored record
};

struct AutoTagOptions
{
    int extractorVersion = smartExtractorsVersion;
    int resolverVersion = smartResolverVersion;
    int dspVersion = smartDspVersion;
    std::map<juce::String, int64_t> tagIds; // canonical type -> existing tag id
};

struct AutoTagDecision
{
    std::optional<AutoTagProposal> proposal;
    std::optional<AutoTagSkip> skip;
};
} // namespace sonicator
