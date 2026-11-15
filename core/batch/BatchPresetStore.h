#pragma once
#include "BatchTypes.h"

namespace sonicator { class SettingsStore; }
namespace sonicator::batch
{
struct BatchPreset
{
    juce::String id;
    juce::String name;
    Recipe recipe;
    ConflictPolicy conflicts = ConflictPolicy::rename;
    bool builtIn = false;
};

struct PresetLoadResult
{
    bool ok = false;
    juce::String error;
    std::vector<BatchPreset> presets;
    juce::var lastUsed;
    bool readOnly = false;
};

// schemaVersion 1 JSON beside the settings file. A preset carries the ordered steps, output
// formats, rate/channels/dither/clipping/conflict policy; it never carries a selection, a
// destination or a replacement confirmation.
class BatchPresetStore
{
public:
    explicit BatchPresetStore(juce::File file);
    static juce::File defaultFileFor(const SettingsStore&);
    static std::vector<BatchPreset> builtIns();
    PresetLoadResult load() const;
    struct SaveResult
    {
        bool ok = false;
        juce::String error;
        bool needsReplace = false;
    };
    SaveResult save(const BatchPreset&, bool replace = false);
    SaveResult remove(const juce::String& id);
    juce::var lastUsed() const;
    void setLastUsed(const juce::var&);
    const juce::File& file() const { return path; }
    static juce::var recipeToVar(const Recipe&);
    static bool recipeFromVar(const juce::var&, Recipe&, juce::String& error);
private:
    struct Document
    {
        enum class State { missing, ok, corrupt, future } state = State::missing;
        juce::String error;
        juce::var lastUsed;
        std::vector<BatchPreset> custom;
    };
    Document readDocument() const;
    bool writeDocument(const Document&, juce::String& error);
    juce::File path;
};
} // namespace sonicator::batch
