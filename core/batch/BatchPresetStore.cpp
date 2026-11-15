#include "BatchPresetStore.h"
#include "core/settings/AppSettings.h"
#include <algorithm>

namespace sonicator::batch
{
namespace
{
constexpr int schemaVersion = 1;

juce::var stepToVar(const ProcessStep& step)
{
    auto object = std::make_unique<juce::DynamicObject>();
    object->setProperty("enabled", step.enabled);
    auto parameters = std::make_unique<juce::DynamicObject>();
    if (std::holds_alternative<RemoveDc>(step.parameters))
        parameters->setProperty("type", "removeDc");
    else if (auto* trim = std::get_if<TrimSilence>(&step.parameters))
    {
        parameters->setProperty("type", "trimSilence");
        parameters->setProperty("thresholdDb", trim->thresholdDb);
        parameters->setProperty("paddingMs", trim->paddingMs);
    }
    else if (auto* normalize = std::get_if<Normalize>(&step.parameters))
    {
        parameters->setProperty("type", "normalize");
        parameters->setProperty("peakDb", normalize->peakDb);
    }
    else if (auto* gain = std::get_if<Gain>(&step.parameters))
    {
        parameters->setProperty("type", "gain");
        parameters->setProperty("gainDb", gain->gainDb);
    }
    else if (auto* fade = std::get_if<FadeIn>(&step.parameters))
    {
        parameters->setProperty("type", "fadeIn");
        parameters->setProperty("ms", fade->ms);
        parameters->setProperty("cosine", fade->cosine);
    }
    else if (auto* fade = std::get_if<FadeOut>(&step.parameters))
    {
        parameters->setProperty("type", "fadeOut");
        parameters->setProperty("ms", fade->ms);
        parameters->setProperty("cosine", fade->cosine);
    }
    object->setProperty("parameters", juce::var(parameters.release()));
    return juce::var(object.release());
}

bool stepFromVar(const juce::var& value, ProcessStep& step, juce::String& error)
{
    if (! value.isObject()) { error = "A step is not an object"; return false; }
    step.enabled = (bool) value.getProperty("enabled", true);
    const auto parameters = value.getProperty("parameters", {});
    const auto type = parameters.getProperty("type", {}).toString();
    if (type == "removeDc") step.parameters = RemoveDc {};
    else if (type == "trimSilence")
    {
        TrimSilence trim;
        trim.thresholdDb = (double) parameters.getProperty("thresholdDb", -60.0);
        trim.paddingMs = (double) parameters.getProperty("paddingMs", 5.0);
        step.parameters = trim;
    }
    else if (type == "normalize")
    {
        Normalize normalize;
        normalize.peakDb = (double) parameters.getProperty("peakDb", -0.1);
        step.parameters = normalize;
    }
    else if (type == "gain")
    {
        Gain gain;
        gain.gainDb = (double) parameters.getProperty("gainDb", 0.0);
        step.parameters = gain;
    }
    else if (type == "fadeIn" || type == "fadeOut")
    {
        const double ms = (double) parameters.getProperty("ms", 5.0);
        const bool cosine = (bool) parameters.getProperty("cosine", false);
        if (type == "fadeIn") step.parameters = FadeIn { ms, cosine };
        else step.parameters = FadeOut { ms, cosine };
    }
    else
    {
        error = "Unknown step type: " + type;
        return false;
    }
    return true;
}

juce::String lowerName(const juce::String& name) { return name.trim().toLowerCase(); }
} // namespace

BatchPresetStore::BatchPresetStore(juce::File file) : path(std::move(file)) {}

juce::File BatchPresetStore::defaultFileFor(const SettingsStore& settings)
{
    return settings.file().getParentDirectory().getChildFile("batch-presets.json");
}

std::vector<BatchPreset> BatchPresetStore::builtIns()
{
    BatchPreset neutral;
    neutral.id = "neutral";
    neutral.name = "No processing";
    neutral.builtIn = true;

    BatchPreset cleanup;
    cleanup.id = "sample-cleanup";
    cleanup.name = "Sample cleanup";
    cleanup.builtIn = true;
    cleanup.recipe.steps = {
        { true, RemoveDc {} },
        { true, TrimSilence { -60.0, 5.0 } },
        { true, Normalize { -0.1 } },
        { true, FadeOut { 2.0, false } },
    };
    return { neutral, cleanup };
}

juce::var BatchPresetStore::recipeToVar(const Recipe& recipe)
{
    auto object = std::make_unique<juce::DynamicObject>();
    juce::Array<juce::var> steps;
    for (const auto& step : recipe.steps) steps.add(stepToVar(step));
    object->setProperty("steps", steps);
    juce::Array<juce::var> outputs;
    for (const auto& output : recipe.outputs)
    {
        auto entry = std::make_unique<juce::DynamicObject>();
        entry->setProperty("format", formatLabel(output.format));
        entry->setProperty("encoding", (int) output.encoding);
        entry->setProperty("flacCompressionLevel", output.flacCompressionLevel);
        outputs.add(juce::var(entry.release()));
    }
    object->setProperty("outputs", outputs);
    object->setProperty("sampleRate", recipe.sampleRate);
    object->setProperty("channels", (int) recipe.channels);
    object->setProperty("dither", (int) recipe.dither);
    object->setProperty("clipping", (int) recipe.clipping);
    object->setProperty("allowIntegerClipping", recipe.allowIntegerClipping);
    return juce::var(object.release());
}

bool BatchPresetStore::recipeFromVar(const juce::var& value, Recipe& recipe, juce::String& error)
{
    if (! value.isObject()) { error = "The recipe is not an object"; return false; }
    Recipe parsed;
    if (const auto* steps = value.getProperty("steps", {}).getArray())
        for (const auto& stepValue : *steps)
        {
            ProcessStep step;
            if (! stepFromVar(stepValue, step, error)) return false;
            parsed.steps.push_back(step);
        }
    parsed.outputs.clear();
    if (const auto* outputs = value.getProperty("outputs", {}).getArray())
    for (const auto& outputValue : *outputs)
    {
        OutputPreset output;
        if (! parseFormat(outputValue.getProperty("format", "wav").toString(), output.format))
        {
            error = "Unknown output format";
            return false;
        }
        const int encoding = (int) outputValue.getProperty("encoding", (int) Encoding::int24);
        if (encoding < 0 || encoding > (int) Encoding::float32)
        {
            error = "Unknown output encoding";
            return false;
        }
        output.encoding = (Encoding) encoding;
        output.flacCompressionLevel = (int) outputValue.getProperty("flacCompressionLevel", 5);
        parsed.outputs.push_back(output);
    }
    parsed.sampleRate = (int) value.getProperty("sampleRate", 0);
    const int channels = (int) value.getProperty("channels", 0);
    if (channels < 0 || channels > (int) ChannelMode::stereo)
    {
        error = "Unknown channel mode";
        return false;
    }
    parsed.channels = (ChannelMode) channels;
    const int dither = (int) value.getProperty("dither", 0);
    if (dither < 0 || dither > (int) DitherMode::off)
    {
        error = "Unknown dither mode";
        return false;
    }
    parsed.dither = (DitherMode) dither;
    const int clipping = (int) value.getProperty("clipping", 0);
    if (clipping < 0 || clipping > (int) ClippingPolicy::clipInteger)
    {
        error = "Unknown clipping policy";
        return false;
    }
    parsed.clipping = (ClippingPolicy) clipping;
    parsed.allowIntegerClipping = (bool) value.getProperty("allowIntegerClipping", false);
    const auto issues = validateRecipe(parsed);
    for (const auto& issue : issues)
        if (! issue.warning) { error = issue.message; return false; }
    recipe = std::move(parsed);
    return true;
}

BatchPresetStore::Document BatchPresetStore::readDocument() const
{
    Document document;
    if (! path.existsAsFile())
    {
        document.state = Document::State::missing;
        return document;
    }
    const auto parsed = juce::JSON::parse(path.loadFileAsString());
    if (parsed.isVoid())
    {
        document.state = Document::State::corrupt;
        document.error = "The preset file is not valid JSON";
        return document;
    }
    const int version = (int) parsed.getProperty("schemaVersion", 0);
    if (version > schemaVersion)
    {
        document.state = Document::State::future;
        document.error = "The preset file was written by a newer version";
        return document;
    }
    if (version != schemaVersion)
    {
        document.state = Document::State::corrupt;
        document.error = "The preset file has no supported schema version";
        return document;
    }
    document.lastUsed = parsed.getProperty("lastUsed", {});
    const auto presets = parsed.getProperty("presets", {});
    if (const auto* array = presets.getArray())
        for (const auto& value : *array)
        {
            BatchPreset preset;
            preset.id = value.getProperty("id", {}).toString();
            preset.name = value.getProperty("name", {}).toString().trim();
            if (preset.id.isEmpty() || preset.name.isEmpty())
            {
                document.state = Document::State::corrupt;
                document.error = "A preset has no id or name";
                return document;
            }
            if (! recipeFromVar(value.getProperty("recipe", {}), preset.recipe, document.error))
            {
                // One unusable preset never hides the healthy ones and the file stays untouched
                // until a later explicit save rewrites it.
                document.error = "Preset " + preset.name + ": " + document.error;
                continue;
            }
            const int conflicts = (int) value.getProperty("conflicts", 0);
            preset.conflicts = conflicts >= 0 && conflicts <= (int) ConflictPolicy::replaceOutput
                ? (ConflictPolicy) conflicts
                : ConflictPolicy::rename;
            document.custom.push_back(std::move(preset));
        }
    document.state = Document::State::ok;
    return document;
}

bool BatchPresetStore::writeDocument(const Document& document, juce::String& error)
{
    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty("schemaVersion", schemaVersion);
    juce::Array<juce::var> presets;
    for (const auto& preset : document.custom)
    {
        auto object = std::make_unique<juce::DynamicObject>();
        object->setProperty("id", preset.id);
        object->setProperty("name", preset.name);
        object->setProperty("recipe", recipeToVar(preset.recipe));
        object->setProperty("conflicts", (int) preset.conflicts);
        presets.add(juce::var(object.release()));
    }
    root->setProperty("presets", presets);
    root->setProperty("lastUsed", document.lastUsed);
    const auto text = juce::JSON::toString(juce::var(root.release()), false);
    path.getParentDirectory().createDirectory();
    juce::TemporaryFile temporary(path);
    if (! temporary.getFile().replaceWithText(text, false, false, "\n"))
    {
        error = "The preset file cannot be written";
        return false;
    }
    if (! temporary.overwriteTargetFileWithTemporary())
    {
        error = "The preset file cannot be replaced";
        return false;
    }
    return true;
}

PresetLoadResult BatchPresetStore::load() const
{
    PresetLoadResult result;
    const auto document = readDocument();
    result.presets = builtIns();
    result.lastUsed = document.lastUsed;
    if (document.state == Document::State::corrupt || document.state == Document::State::future)
    {
        result.ok = false;
        result.error = document.error;
        result.readOnly = document.state == Document::State::future;
        return result;
    }
    result.ok = true;
    result.error = document.error;
    result.presets.insert(result.presets.end(), document.custom.begin(), document.custom.end());
    return result;
}

BatchPresetStore::SaveResult BatchPresetStore::save(const BatchPreset& preset, bool replace)
{
    SaveResult result;
    auto name = preset.name.trim();
    if (name.isEmpty()) { result.error = "The preset needs a name"; return result; }
    for (const auto& issue : validateRecipe(preset.recipe))
        if (! issue.warning) { result.error = issue.message; return result; }
    for (const auto& builtIn : builtIns())
        if (lowerName(builtIn.name) == lowerName(name) || builtIn.id == preset.id)
        {
            result.error = "A built-in preset cannot be replaced";
            return result;
        }
    auto document = readDocument();
    if (document.state == Document::State::corrupt || document.state == Document::State::future)
    {
        result.error = document.error + "; the existing file was not changed";
        return result;
    }
    BatchPreset stored = preset;
    stored.name = name;
    stored.builtIn = false;
    if (stored.id.isEmpty()) stored.id = juce::Uuid().toDashedString();
    int existing = -1;
    for (size_t i = 0; i < document.custom.size(); ++i)
        if (document.custom[i].id == stored.id || lowerName(document.custom[i].name) == lowerName(name))
            existing = (int) i;
    if (existing >= 0 && ! replace)
    {
        result.needsReplace = true;
        result.error = "A preset with this name already exists";
        return result;
    }
    if (existing >= 0) document.custom[(size_t) existing] = stored;
    else document.custom.push_back(stored);
    juce::String error;
    if (! writeDocument(document, error)) { result.error = error; return result; }
    result.ok = true;
    return result;
}

BatchPresetStore::SaveResult BatchPresetStore::remove(const juce::String& id)
{
    SaveResult result;
    for (const auto& builtIn : builtIns())
        if (builtIn.id == id) { result.error = "A built-in preset cannot be removed"; return result; }
    auto document = readDocument();
    if (document.state != Document::State::ok && document.state != Document::State::missing)
    {
        result.error = document.error;
        return result;
    }
    const auto before = document.custom.size();
    document.custom.erase(std::remove_if(document.custom.begin(), document.custom.end(),
                                         [&id](const BatchPreset& preset) { return preset.id == id; }),
                          document.custom.end());
    if (document.custom.size() == before) { result.ok = true; return result; }
    juce::String error;
    if (! writeDocument(document, error)) { result.error = error; return result; }
    result.ok = true;
    return result;
}

juce::var BatchPresetStore::lastUsed() const { return readDocument().lastUsed; }

void BatchPresetStore::setLastUsed(const juce::var& value)
{
    auto document = readDocument();
    if (document.state == Document::State::corrupt || document.state == Document::State::future)
        return;
    document.lastUsed = value;
    juce::String error;
    writeDocument(document, error);
}
} // namespace sonicator::batch
