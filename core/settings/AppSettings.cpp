#include "AppSettings.h"
#include <stdexcept>
namespace sonicator {
juce::File AppSettings::defaultSettingsFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Sonicator/settings.json");
}
juce::String AppSettings::themeToString(ThemeMode theme)
{
    switch (theme)
    {
        case ThemeMode::Dark: return "dark";
        case ThemeMode::Light: return "light";
        case ThemeMode::System:
        default: return "system";
    }
}
ThemeMode AppSettings::themeFromString(const juce::String& value)
{
    if (value.equalsIgnoreCase("dark")) return ThemeMode::Dark;
    if (value.equalsIgnoreCase("light")) return ThemeMode::Light;
    return ThemeMode::System;
}
int AppSettings::clampScale(int value)
{
    return value >= 75 && value <= 200 ? value : 100;
}
SettingsStore::SettingsStore(juce::File file) : path(std::move(file)) {}
AppSettings SettingsStore::load() const
{
    AppSettings settings;
    if (! path.existsAsFile()) return settings;
    const auto parsed = juce::JSON::parse(path.loadFileAsString());
    const auto* object = parsed.getDynamicObject();
    if (object == nullptr) return settings;
    settings.theme = AppSettings::themeFromString(object->getProperty("theme").toString());
    settings.scalePercent = AppSettings::clampScale((int) object->getProperty("scale"));
    const auto language = object->getProperty("language").toString();
    if (language.isNotEmpty()) settings.language = language;
    const int pageSize = (int) object->getProperty("pageSize");
    settings.pageSize = (pageSize == 100 || pageSize == 1000 || pageSize == 10000 || pageSize == 0)
        ? pageSize : 10000;
    settings.databasePath = object->getProperty("database").toString();
    const auto hide = object->getProperty("hideEmpty");
    settings.hideEmpty = hide.isBool() ? (bool) hide : true;
    settings.lastFolder = object->getProperty("lastFolder").toString();
    settings.lastSamplePath = object->getProperty("lastSamplePath").toString();
    auto readBool = [object](const char* key, bool fallback)
    {
        const auto value = object->getProperty(key);
        return value.isBool() ? (bool) value : fallback;
    };
    settings.midiPreview = readBool("midiPreview", settings.midiPreview);
    settings.showIgnored = readBool("showIgnored", settings.showIgnored);
    settings.reasonPacksAdded = readBool("reasonPacksAdded", settings.reasonPacksAdded);
    settings.abletonPacksAdded = readBool("abletonPacksAdded", settings.abletonPacksAdded);
    auto readNumber = [object](const char* key, double fallback)
    {
        const auto value = object->getProperty(key);
        return (value.isDouble() || value.isInt() || value.isInt64()) ? (double) value : fallback;
    };
    settings.splitThresholdDb = juce::jlimit(-80.0, -20.0,
        readNumber("splitThreshold", settings.splitThresholdDb));
    settings.splitMinSilenceMs = (int) juce::jlimit(50.0, 2000.0,
        readNumber("splitMinSilence", (double) settings.splitMinSilenceMs));
    settings.splitMinPartMs = (int) juce::jlimit(20.0, 2000.0,
        readNumber("splitMinPart", (double) settings.splitMinPartMs));
    settings.splitSameFolder = readBool("splitSameFolder", settings.splitSameFolder);
    settings.splitMoveToTrash = readBool("splitMoveToTrash", settings.splitMoveToTrash);
    settings.extractSameFolder = readBool("extractSameFolder", settings.extractSameFolder);
    settings.extractNormalize = readBool("extractNormalize", settings.extractNormalize);
    settings.extractTuneToSong = readBool("extractTuneToSong", settings.extractTuneToSong);
    settings.extractOutputFolder = object->getProperty("extractOutputFolder").toString();
    settings.skipSilence = readBool("skipSilence", settings.skipSilence);
    settings.autoPreview = readBool("autoPreview", settings.autoPreview);
    settings.loopPreview = readBool("loopPreview", settings.loopPreview);
    settings.rexGate = readBool("rexGate", settings.rexGate);
    settings.contributeTrainingData = readBool("contributeTrainingData", settings.contributeTrainingData);
    if (const auto* history = object->getProperty("searchHistory").getArray())
        for (const auto& entry : *history)
            if (settings.searchHistory.size() < 10)
            {
                const auto text = entry.toString();
                if (text.isNotEmpty()) settings.searchHistory.add(text);
            }
    if (const auto* favorites = object->getProperty("favoriteFolders").getArray())
        for (const auto& entry : *favorites)
        {
            const auto text = entry.toString().trim();
            if (text.isNotEmpty()) settings.favoriteFolders.addIfNotAlreadyThere(text);
        }
    // A path cannot be a JSON object key (it contains slashes), so aliases are [path, name] pairs.
    if (const auto* aliases = object->getProperty("folderAliases").getArray())
        for (const auto& entry : *aliases)
        {
            const auto* pair = entry.getArray();
            if (pair == nullptr || pair->size() < 2) continue;
            const auto path = (*pair)[0].toString().trim();
            const auto name = (*pair)[1].toString().trim();
            if (path.isNotEmpty() && name.isNotEmpty()) settings.folderAliases.set(path, name);
        }
    const int favoritesHeight = (int) object->getProperty("favoritesPanelHeight");
    if (favoritesHeight >= 60 && favoritesHeight <= 2000)
        settings.favoritesPanelHeight = favoritesHeight;
    settings.windowX = (int) object->getProperty("windowX");
    settings.windowY = (int) object->getProperty("windowY");
    settings.windowWidth = juce::jmax(0, (int) object->getProperty("windowWidth"));
    settings.windowHeight = juce::jmax(0, (int) object->getProperty("windowHeight"));
    return settings;
}
void SettingsStore::save(const AppSettings& settings) const
{
    if (! path.getParentDirectory().createDirectory())
        throw std::runtime_error("Cannot create settings directory");
    juce::DynamicObject::Ptr object(new juce::DynamicObject());
    object->setProperty("theme", AppSettings::themeToString(settings.theme));
    object->setProperty("scale", AppSettings::clampScale(settings.scalePercent));
    object->setProperty("language", settings.language);
    object->setProperty("pageSize", settings.pageSize);
    object->setProperty("database", settings.databasePath);
    object->setProperty("hideEmpty", settings.hideEmpty);
    object->setProperty("showIgnored", settings.showIgnored);
    object->setProperty("reasonPacksAdded", settings.reasonPacksAdded);
    object->setProperty("abletonPacksAdded", settings.abletonPacksAdded);
    object->setProperty("splitThreshold", settings.splitThresholdDb);
    object->setProperty("splitMinSilence", settings.splitMinSilenceMs);
    object->setProperty("splitMinPart", settings.splitMinPartMs);
    object->setProperty("splitSameFolder", settings.splitSameFolder);
    object->setProperty("splitMoveToTrash", settings.splitMoveToTrash);
    object->setProperty("extractSameFolder", settings.extractSameFolder);
    object->setProperty("extractNormalize", settings.extractNormalize);
    object->setProperty("extractTuneToSong", settings.extractTuneToSong);
    object->setProperty("extractOutputFolder", settings.extractOutputFolder);
    object->setProperty("midiPreview", settings.midiPreview);
    object->setProperty("skipSilence", settings.skipSilence);
    object->setProperty("autoPreview", settings.autoPreview);
    object->setProperty("loopPreview", settings.loopPreview);
    object->setProperty("rexGate", settings.rexGate);
    object->setProperty("contributeTrainingData", settings.contributeTrainingData);
    juce::Array<juce::var> history;
    for (int i = 0; i < juce::jmin(10, settings.searchHistory.size()); ++i)
        history.add(settings.searchHistory[i]);
    object->setProperty("searchHistory", history);
    juce::Array<juce::var> favorites;
    for (const auto& folder : settings.favoriteFolders)
        if (folder.trim().isNotEmpty()) favorites.add(folder);
    object->setProperty("favoriteFolders", favorites);
    juce::Array<juce::var> aliases;
    const auto aliasKeys = settings.folderAliases.getAllKeys();
    for (int i = 0; i < settings.folderAliases.size(); ++i)
    {
        const auto name = settings.folderAliases.getAllValues()[i].trim();
        if (aliasKeys[i].trim().isEmpty() || name.isEmpty()) continue;
        juce::Array<juce::var> pair;
        pair.add(aliasKeys[i]);
        pair.add(name);
        aliases.add(pair);
    }
    object->setProperty("folderAliases", aliases);
    object->setProperty("favoritesPanelHeight", settings.favoritesPanelHeight);
    object->setProperty("lastFolder", settings.lastFolder);
    object->setProperty("lastSamplePath", settings.lastSamplePath);
    object->setProperty("windowX", settings.windowX);
    object->setProperty("windowY", settings.windowY);
    object->setProperty("windowWidth", settings.windowWidth);
    object->setProperty("windowHeight", settings.windowHeight);
    const auto text = juce::JSON::toString(juce::var(object.get()), false);
    juce::TemporaryFile temporary(path);
    if (! temporary.getFile().replaceWithText(text) || ! temporary.overwriteTargetFileWithTemporary())
        throw std::runtime_error("Cannot save settings");
}
void SettingsStore::update(const std::function<void(AppSettings&)>& change) const
{
    auto settings = load();
    change(settings);
    save(settings);
}
} // namespace sonicator
