#include "MainComponent.h"
#include "core/audio/SonicatorFormats.h"
#include "core/match/SourceMetadata.h"
#include "core/match/MatchedDrag.h"
#include "core/export/ExternalFileDrag.h"
#include "core/export/AudioExporter.h"
#include "core/files/FileDisposal.h"
#include "core/files/DuplicateCleanup.h"
#include "ui/library/DuplicateCleanupDialog.h"
#include <unordered_set>
#include "core/dsp/FeatureExtractorV2.h"
#include "core/ml/FeatureV2Backfill.h"
#include "core/ableton/AbletonPacks.h"
#include "core/reason/ReasonPatch.h"
#include "ui/tagging/AutoTagDialog.h"
#include "ui/split/SplitDialog.h"
#include "ui/batch/BatchProcessingDialog.h"
#include "core/batch/BatchPresetStore.h"
#include "core/batch/BatchPublisher.h"
#include "ui/extract/ExtractDialog.h"
#include "core/extract/EmbeddedSamples.h"
#include "core/extract/ExtractFollowUp.h"
#include "core/scanner/FileScanner.h"
#include "BinaryData.h"
#include "core/database/PathRelocation.h"
#include "ui/settings/RelocateFoldersPanel.h"
#include "ui/settings/SettingsPanel.h"
#include "ui/shell/ThemedDialogs.h"
#include "ui/updates/UpdateDialog.h"
#include "ui/platform/FileClipboard.h"
#include "core/dsp/AnalysisAudio.h"
#include "core/smart/AcousticClassifierService.h"
#include "core/smart/NameHeuristics.h"
#include "core/smart/GenreContextExtractor.h"
#include "core/Version.h"
#include "core/updates/UpdateCheck.h"
#include "core/ml/AdaptiveClassifier.h"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <map>
#include <set>
#include "core/indexing/CatalogFileSync.h"
#include "core/edit/AudioEditWriter.h"
#include "ui/library/TapTempoDialog.h"
#include "ui/library/RoundTagMenu.h"
#include "ui/library/TagsMenuPill.h"
#include <thread>
namespace sonicator {
namespace {
// A launch check must not fight the first-run consent or any other modal. If one is open, the
// dialog waits a few seconds and tries again; on the last try the launch check is simply skipped
// (Settings still offers Check for updates).
void presentUpdateDialog(juce::Component::SafePointer<MainComponent> safe, updates::Release release,
                         int attemptsLeft)
{
    if (safe == nullptr) return;
    if (juce::Component::getCurrentlyModalComponent() != nullptr)
    {
        if (attemptsLeft <= 0) return;
        juce::Timer::callAfterDelay(3000, [safe, release, attemptsLeft]() mutable
        {
            presentUpdateDialog(safe, release, attemptsLeft - 1);
        });
        return;
    }
    ui::updates::showUpdateDialog(*safe, release);
}
// The file ships in the bundle (an empty label list when there is not enough data), so a broken
// resource degrades to today's local-only behaviour instead of stopping startup.
void loadShippedGlobalPrototypes()
{
    auto& classifier = AcousticClassifierService::getSharedAdaptiveClassifier();
    const auto json = juce::String::fromUTF8(BinaryData::global_prototypes_json,
                                             BinaryData::global_prototypes_jsonSize);
    if (! classifier.loadGlobalPrototypes(json))
        juce::Logger::writeToLog("Global prototypes were not loaded; using local feedback only");
    const auto model = juce::String::fromUTF8(BinaryData::global_model_json,
                                              BinaryData::global_model_jsonSize);
    if (! classifier.loadGlobalModel(model))
        juce::Logger::writeToLog("Global model was not loaded; schema-2 queries use local prototypes only");
}
bool isDiscImageFormat(const juce::String& format)
{
    return format == "ISO" || format == "AKAI" || format == "CUE" || format == "MDX" || format == "BIN" || format == "IMG";
}
}
namespace {
struct PaletteColour { const char* name; uint32_t colour; };
constexpr PaletteColour tagPalette[] = {
    { "Red", 0xffe05a5a }, { "Orange", 0xffe08a45 }, { "Yellow", 0xffe0b84a },
    { "Lime", 0xff9ad06a }, { "Green", 0xff6ad0a0 }, { "Teal", 0xff4fb8c8 },
    { "Blue", 0xff4f8fd0 }, { "Violet", 0xff7a6aea }, { "Purple", 0xffa96ae0 },
    { "Pink", 0xffd06ab8 }, { "Rose", 0xffd06a7a }, { "Grey", 0xff8a8f9c }
};
constexpr int pageSize = 10000;
// Shared by the row and folder popups, above their tag/type/action ID ranges. The name list is
// captured when each popup opens, so another owner's vocabulary update cannot shift the mapping.
constexpr int genreMenuFirstId = 1000000;
// Catalog rows for formats this build no longer indexes: archive metadata rows and older error
// rows for protected Ableton AIFC samples. Source files are never opened beyond their headers.
int purgeUnsupportedRows(SampleRepository& repo)
{
    int removed = repo.removeSamplesWithFormats({ "CMB", "RFL" });
    juce::StringArray protectedRows;
    for (const auto& path : repo.errorPathsWithFormats({ "AIF", "AIFF", "AIFC" }))
        if (isAbletonProtectedSample(juce::File(path))) protectedRows.add(path);
    if (! protectedRows.isEmpty())
    {
        removed += protectedRows.size();
        repo.removeByPaths(protectedRows);
    }
    return removed;
}
juce::String canonicalFolderPath(const juce::String& path)
{
    return path.isEmpty() ? juce::String() : juce::File(path).getFullPathName();
}
juce::Image tagDotImage(uint32_t colour)
{
    juce::Image image(juce::Image::ARGB, 14, 14, true);
    juce::Graphics g(image);
    g.setColour(juce::Colour(colour)); g.fillEllipse(2.0f, 2.0f, 10.0f, 10.0f);
    return image;
}
juce::Image starMenuImage()
{
    juce::Image image(juce::Image::ARGB, 14, 14, true);
    juce::Graphics g(image);
    juce::Path path;
    const auto centre = juce::Point<float>(7.0f, 7.0f);
    for (int i = 0; i < 10; ++i)
    {
        const float angle = -juce::MathConstants<float>::halfPi + i * juce::MathConstants<float>::pi / 5.0f;
        const float radius = (i % 2 == 0) ? 6.0f : 2.7f;
        const juce::Point<float> point(centre.x + std::cos(angle) * radius, centre.y + std::sin(angle) * radius);
        if (i == 0) path.startNewSubPath(point); else path.lineTo(point);
    }
    path.closeSubPath();
    g.setColour(theme::text); g.fillPath(path);
    return image;
}
// Renders a dialog inside the editor instead of as a separate window. Plugin hosts do not
// reliably keep top-level modal windows from a plugin, and a modal window that dismisses on
// "input outside" closes on the first click inside a DAW.
class EmbeddedDialogOverlay final : public juce::Component
{
public:
    explicit EmbeddedDialogOverlay(std::unique_ptr<juce::Component> contentToOwn)
        : content(std::move(contentToOwn))
    {
        addAndMakeVisible(*content);
        setWantsKeyboardFocus(true);
    }
    std::function<void()> onDismiss;
    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colours::black.withAlpha(0.6f));
    }
    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey && onDismiss != nullptr) { onDismiss(); return true; }
        return false;
    }
    void resized() override
    {
        if (content == nullptr || getWidth() <= 0 || getHeight() <= 0) return;
        const auto available = getLocalBounds().reduced(24);
        const auto width = (float) content->getWidth();
        const auto height = (float) content->getHeight();
        if (width <= 0 || height <= 0) return;
        const auto scale = juce::jmin(1.0f, juce::jmin((float) available.getWidth() / width,
                                                       (float) available.getHeight() / height));
        content->setTransform(juce::AffineTransform::scale(scale));
        const auto target = available.withSizeKeepingCentre(juce::roundToInt(width * scale),
                                                            juce::roundToInt(height * scale));
        content->setBounds(target.getX(), target.getY(), juce::roundToInt(width), juce::roundToInt(height));
    }
private:
    std::unique_ptr<juce::Component> content;
};
// A modal Settings window that dismisses itself when the user clicks anywhere outside it.
class SettingsDialogWindow final : public juce::DialogWindow
{
public:
    SettingsDialogWindow() : juce::DialogWindow("Settings", theme::panel, true) {}
    void closeButtonPressed() override { exitModalState(0); }
    void inputAttemptWhenModal() override { exitModalState(0); }
};
class RelocateDialogWindow final : public juce::DialogWindow
{
public:
    RelocateDialogWindow() : juce::DialogWindow("Relocate folders", theme::panel, true) {}
    void closeButtonPressed() override { exitModalState(0); }
    void inputAttemptWhenModal() override { exitModalState(0); }
};
// The split window owns its content and lets the owner decide what the close button means while
// a job is running: it cancels the job and leaves the window open with its result.
class SplitDialogWindow final : public juce::DialogWindow
{
public:
    SplitDialogWindow() : juce::DialogWindow("Split by silence", theme::panel, true) {}
    void closeButtonPressed() override { if (onClose) onClose(); }
    void inputAttemptWhenModal() override {}
    std::function<void()> onClose;
};
class DuplicateDialogWindow final : public juce::DialogWindow
{
public:
    DuplicateDialogWindow() : juce::DialogWindow("Duplicate files", theme::panel, true) {}
    void closeButtonPressed() override { if (onClose) onClose(); }
    void inputAttemptWhenModal() override {}
    std::function<void()> onClose;
};
class ExtractDialogWindow final : public juce::DialogWindow
{
public:
    ExtractDialogWindow() : juce::DialogWindow("Extract samples", theme::panel, true) {}
    void closeButtonPressed() override { if (onClose) onClose(); }
    void inputAttemptWhenModal() override {}
    std::function<void()> onClose;
};
class BatchDialogWindow final : public juce::DialogWindow
{
public:
    BatchDialogWindow() : juce::DialogWindow("Batch Processing", theme::panel, true) {}
    void closeButtonPressed() override { if (onClose) onClose(); }
    void inputAttemptWhenModal() override {}
    std::function<void()> onClose;
};
}
ActivityDot::ActivityDot()
{
    setTooltip("Background job running");
    startTimerHz(2);
}
void ActivityDot::setActive(bool running)
{
    if (active == running) return;
    active = running;
    lit = true;
    setTooltip(active ? "Background job running" : "Idle");
    repaint();
}
void ActivityDot::timerCallback()
{
    if (! active) return;
    lit = ! lit;
    repaint();
}
void ActivityDot::paint(juce::Graphics& g)
{
    const auto green = juce::Colour(0xff3fb950);
    const auto red = juce::Colour(0xffe5534b);
    auto colour = active ? red : green;
    if (active && ! lit) colour = colour.withAlpha(0.25f);
    g.setColour(colour);
    g.fillEllipse(getLocalBounds().toFloat());
}
FavoritesSplitter::FavoritesSplitter()
{
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
}
void FavoritesSplitter::paint(juce::Graphics& g)
{
    g.setColour(theme::isDark ? theme::line : theme::selection);
    const auto area = getLocalBounds().reduced(6, 3).toFloat();
    g.fillRoundedRectangle(area, 2.0f);
    if (isMouseOver())
    {
        g.setColour(theme::accent.withAlpha(0.9f));
        g.fillRoundedRectangle(area.withSizeKeepingCentre(area.getWidth(), 2.0f), 1.0f);
    }
}
void FavoritesSplitter::mouseDown(const juce::MouseEvent&)
{
    dragStartHeight = getHeight != nullptr ? getHeight() : 0;
}
void FavoritesSplitter::mouseDrag(const juce::MouseEvent& e)
{
    if (setHeight != nullptr)
        setHeight(dragStartHeight - e.getDistanceFromDragStartY());
}
void FavoritesSplitter::mouseUp(const juce::MouseEvent&)
{
    if (onRelease != nullptr) onRelease();
}
void FavoritesSplitter::mouseEnter(const juce::MouseEvent&) { repaint(); }
void FavoritesSplitter::mouseExit(const juce::MouseEvent&) { repaint(); }
juce::File MainComponent::defaultDatabaseFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Sonicator/sonicator.sqlite");
}
juce::File MainComponent::settingsFile() const
{
    return settingsStore.file();
}

void MainComponent::persistLayout()
{
    layoutPreferences.favoritesHeight = favoritePanelHeight;
    const auto layout = layoutPreferences;
    const int favoritesHeight = favoritePanelHeight;
    try
    {
        settingsStore.update([this, layout, favoritesHeight](AppSettings& saved)
        {
            if (embeddedUi) saved.pluginLayout = layout;
            else saved.standaloneLayout = layout;
            // The legacy field is what older builds read; keep both in step.
            saved.favoritesPanelHeight = favoritesHeight;
        });
    }
    catch (const std::exception& e)
    {
        status.setText("Panel size not saved: " + juce::String::fromUTF8(e.what()), juce::dontSendNotification);
        return;
    }
    if (embeddedUi) appSettings.pluginLayout = layout;
    else appSettings.standaloneLayout = layout;
}

void MainComponent::resetPanelSize(const juce::String& panelId)
{
    const auto defaults = defaultPanelLayout(getWidth());
    if (panelId == "folders") layoutPreferences.foldersWidth = defaults.foldersWidth;
    else if (panelId == "analysis") layoutPreferences.analysisWidth = defaults.analysisWidth;
    else if (panelId == "preview") layoutPreferences.previewHeight = defaults.previewHeight;
    resized();
    persistLayout();
}

void MainComponent::resetLayout()
{
    const auto defaults = defaultPanelLayout(getWidth());
    layoutPreferences = defaults;
    favoritePanelHeight = defaults.favoritesHeight;
    resized();
    persistLayout();
}

namespace
{
juce::String commandCategoryFor(CommandScope scope)
{
    switch (scope)
    {
        case CommandScope::waveform: return "Waveform edit";
        case CommandScope::library: return "Library";
        case CommandScope::folders: return "Folders";
        case CommandScope::discBrowser: return "Container";
        case CommandScope::settings: return "Settings";
        case CommandScope::app:
        default: return "Application";
    }
}
}

void MainComponent::registerCommands()
{
    commands.clear();
    auto addCommand = [this](CommandSpec spec, std::function<CommandState()> state,
                             std::function<void()> run)
    {
        commands.add(std::move(spec), std::move(state), std::move(run));
    };

    // Waveform commands keep the ids and defaults from the single EditCommands table.
    for (const auto& legacy : editCommandTable())
    {
        CommandSpec spec;
        spec.id = editCommandId(legacy.action);
        spec.name = legacy.label;
        spec.description = "Waveform editor: " + juce::String(legacy.label);
        spec.scope = legacy.context == CommandContext::library ? CommandScope::library : CommandScope::waveform;
        spec.category = commandCategoryFor(spec.scope);
        spec.legacyMenuId = legacy.id;
        if (legacy.shortcut.isValid()) spec.defaults.push_back(legacy.shortcut);
        if (legacy.alternateShortcut.isValid()) spec.defaults.push_back(legacy.alternateShortcut);
        spec.destructive = legacy.action == EditAction::normalize || legacy.action == EditAction::trimSilence
            || legacy.action == EditAction::deleteRegion || legacy.action == EditAction::save
            || legacy.action == EditAction::normalizeSelected;
        const auto action = legacy.action;
        auto state = [this, action]() -> CommandState
        {
            if (action == EditAction::normalizeSelected)
            {
                if (embeddedUi)
                    return CommandState::unavailable("Normalize selection runs in the SONICATOR app");
                return (! library.getSelectedSamples().empty() && ! isNormalizeRunning())
                    ? CommandState::ready()
                    : CommandState::unavailable("Select one or more ordinary files");
            }
            return preview.sourceIsEditable() ? CommandState::ready()
                                              : CommandState::unavailable("No editable audio in the preview");
        };
        auto run = [this, action]()
        {
            if (action == EditAction::normalizeSelected)
            {
                const auto selected = library.getSelectedSamples();
                if (! selected.empty()) startNormalizeSelected(selected);
                return;
            }
            preview.applyEditCommand(action);
        };
        addCommand(std::move(spec), std::move(state), std::move(run));
    }

    // Transport. These were handled in PreviewPanel::keyPressed before; now the same ids drive
    // the buttons, the keys and the Hotkeys page.
    auto addTransport = [this, &addCommand](juce::String id, juce::String name, std::vector<juce::KeyPress> defaults,
                                            std::function<void()> run)
    {
        CommandSpec spec;
        spec.id = std::move(id);
        spec.name = name;
        spec.description = "Preview transport";
        // Transport keys worked from anywhere in the window before, so they stay application
        // scoped; the router already keeps text fields and dialogs out.
        spec.scope = CommandScope::app;
        spec.category = "Preview";
        spec.defaults = std::move(defaults);
        addCommand(std::move(spec), [] { return CommandState::ready(); }, std::move(run));
    };
    addTransport("preview.playPause", "Play / pause",
                 { juce::KeyPress(juce::KeyPress::spaceKey) }, [this] { preview.playPause(); });
    addTransport("preview.stop", "Stop", {}, [this] { preview.stopPlayback(); });
    addTransport("preview.seekBack", "Seek back one second",
                 { juce::KeyPress(juce::KeyPress::leftKey) }, [this] { preview.seekBy(-1.0); });
    addTransport("preview.seekForward", "Seek forward one second",
                 { juce::KeyPress(juce::KeyPress::rightKey) }, [this] { preview.seekBy(1.0); });
    addTransport("preview.clearSelection", "Clear selection / stop",
                 { juce::KeyPress(juce::KeyPress::escapeKey) }, [this] { preview.stopAndClearSelection(); });
    {
        // The card keeps every gesture in both modes; this only swaps what is drawn.
        CommandSpec spec;
        spec.id = "preview.toggleSpectrum";
        spec.name = "Waveform / spectrum view";
        spec.description = "Switch the preview card between the waveform and a spectrogram";
        spec.scope = CommandScope::waveform;
        spec.category = "Preview";
        spec.defaults = { juce::KeyPress('v') };
        addCommand(std::move(spec),
                   [this]() -> CommandState
                   {
                       return preview.waveformLength() > 0
                           ? CommandState::ready()
                           : CommandState::unavailable("No source in the preview");
                   },
                   [this] { preview.toggleSpectrumView(); });
    }

    {
        CommandSpec spec;
        spec.id = "app.settings";
        spec.name = "Settings";
        spec.description = "Open Settings on the last used page";
        spec.scope = CommandScope::app;
        spec.category = "Application";
        spec.defaults = { juce::KeyPress(',', juce::ModifierKeys::commandModifier, 0) };
        addCommand(std::move(spec), [] { return CommandState::ready(); }, [this] { showSettings(); });
    }
    {
        CommandSpec spec;
        spec.id = "app.settings.hotkeys";
        spec.name = "Settings - Hotkeys";
        spec.description = "Opens Settings on the Hotkeys page";
        spec.scope = CommandScope::app;
        spec.category = "Application";
        addCommand(std::move(spec), [] { return CommandState::ready(); },
                   [this] { showSettings(SettingsSection::hotkeys); });
    }
    {
        CommandSpec spec;
        spec.id = "app.focusSearch";
        spec.name = "Focus search";
        spec.description = "Move the keyboard focus to the library search field";
        spec.scope = CommandScope::app;
        spec.category = "Application";
        spec.defaults = { juce::KeyPress('f', juce::ModifierKeys::commandModifier, 0) };
        addCommand(std::move(spec), [] { return CommandState::ready(); },
                   [this] { search.grabKeyboardFocus(); });
    }
    {
        CommandSpec spec;
        spec.id = "library.batchProcess";
        spec.name = "Batch Processing...";
        spec.description = "Converts and processes a snapshot of the selected files";
        spec.scope = CommandScope::library;
        spec.category = "Library";
        auto state = [this]() -> CommandState
        {
            if (embeddedUi)
                return CommandState::unavailable("Batch Processing runs in the SONICATOR app");
            if (library.getSelectedSamples().empty())
                return CommandState::unavailable("Select one or more files");
            if (isBatchRunning())
                return CommandState::unavailable("Batch Processing is running");
            if (isSplitRunning() || isExtractRunning() || isNormalizeRunning())
                return CommandState::unavailable("Wait for the running job");
            return CommandState::ready();
        };
        addCommand(std::move(spec), std::move(state), [this]
        {
            batch::SourceSelection selection;
            selection.kind = batch::SourceSelection::Kind::rows;
            for (const auto& record : library.getSelectedSamples()) selection.rows.push_back(record.id);
            openBatchProcessing(selection);
        });
    }
    {
        CommandSpec spec;
        spec.id = "folders.batchProcess";
        spec.name = "Batch Processing folder...";
        spec.description = "Converts and processes every catalogued file below the selected folder";
        spec.scope = CommandScope::folders;
        spec.category = "Folders";
        auto state = [this]() -> CommandState
        {
            if (embeddedUi)
                return CommandState::unavailable("Batch Processing runs in the SONICATOR app");
            if (selectedFolder.isEmpty())
                return CommandState::unavailable("Select a catalogued folder");
            if (isBatchRunning())
                return CommandState::unavailable("Batch Processing is running");
            if (isSplitRunning() || isExtractRunning() || isNormalizeRunning())
                return CommandState::unavailable("Wait for the running job");
            return CommandState::ready();
        };
        addCommand(std::move(spec), std::move(state), [this]
        {
            batch::SourceSelection selection;
            selection.kind = batch::SourceSelection::Kind::folder;
            selection.folder = selectedFolder;
            openBatchProcessing(selection);
        });
    }

    {
        CommandSpec spec;
        spec.id = "library.addLastTag";
        spec.name = "Assign last used tag";
        spec.description = "Applies the last tag you assigned in this catalog to the selection";
        spec.scope = CommandScope::library;
        spec.category = "Tags";
        spec.defaults = { juce::KeyPress('t', juce::ModifierKeys::commandModifier
                                               | juce::ModifierKeys::shiftModifier, 0) };
        auto state = [this]() -> CommandState
        {
            if (library.getSelectedSamples().empty())
                return CommandState::unavailable("Select one or more files");
            if (readRepo == nullptr || readRepo->recentTags().empty())
                return CommandState::unavailable("No tag has been applied yet");
            return CommandState::ready();
        };
        auto run = [this]()
        {
            if (readRepo == nullptr) return;
            const auto recent = readRepo->recentTags();
            if (recent.empty()) return;
            std::vector<int64_t> ids;
            for (const auto& sample : library.getSelectedSamples()) ids.push_back(sample.id);
            if (ids.empty()) return;
            const auto added = assignTagAndRemember(ids, recent.front().id);
            status.setText("Tag " + recent.front().name + (added > 0 ? " added to " + juce::String(added)
                                                                  : juce::String(" already present")),
                           juce::dontSendNotification);
        };
        addCommand(std::move(spec), std::move(state), std::move(run));
    }

    {
        CommandSpec spec;
        spec.id = "layout.reset";
        spec.name = "Reset panel layout";
        spec.description = "Restores the folder width, analysis width, preview height and favorites height";
        spec.scope = CommandScope::app;
        spec.category = "Workspace";
        addCommand(std::move(spec), [] { return CommandState::ready(); }, [this] { resetLayout(); });
    }
    const std::array<std::pair<juce::String, juce::String>, 3> panelResets { {
        { "folders", "Reset folder panel width" }, { "analysis", "Reset analysis panel width" },
        { "preview", "Reset preview height" } } };
    for (const auto& reset : panelResets)
    {
        CommandSpec spec;
        spec.id = "layout.reset." + reset.first;
        spec.name = reset.second;
        spec.description = "Restores one panel size to its default";
        spec.scope = CommandScope::app;
        spec.category = "Workspace";
        const auto panelId = reset.first;
        addCommand(std::move(spec), [] { return CommandState::ready(); },
                   [this, panelId] { resetPanelSize(panelId); });
    }

    {
        CommandSpec spec;
        spec.id = "library.roundTagMenu";
        spec.name = "Round tag menu";
        spec.description = "Opens the radial tag menu for the selected files";
        spec.scope = CommandScope::library;
        spec.category = "Tags";
        spec.defaults = { juce::KeyPress('t', juce::ModifierKeys::commandModifier, 0) };
        auto state = [this]() -> CommandState
        {
            if (! appSettings.roundTagMenu)
                return CommandState::unavailable("Turn on Round tag menu in Settings");
            if (library.getSelectedSamples().empty())
                return CommandState::unavailable("Select one or more files");
            return CommandState::ready();
        };
        addCommand(std::move(spec), std::move(state),
                   [this] { showRoundTagMenuAt(commandAnchor(false), false); });
    }
    {
        CommandSpec spec;
        spec.id = "folders.roundTagMenu";
        spec.name = "Round tag menu for the folder";
        spec.description = "Opens the radial tag menu for every catalogued file below the selected folder";
        spec.scope = CommandScope::folders;
        spec.category = "Tags";
        spec.defaults = { juce::KeyPress('t', juce::ModifierKeys::commandModifier, 0) };
        auto state = [this]() -> CommandState
        {
            if (! appSettings.roundTagMenu)
                return CommandState::unavailable("Turn on Round tag menu in Settings");
            if (selectedFolder.isEmpty())
                return CommandState::unavailable("Select a catalogued folder");
            return CommandState::ready();
        };
        addCommand(std::move(spec), std::move(state),
                   [this] { showRoundTagMenuAt(commandAnchor(true), true); });
    }

    reapplyShortcutProfile();
    setEditCommandProfileProvider([safe = juce::Component::SafePointer<MainComponent>(this)]
    {
        return safe != nullptr ? safe->shortcutProfile : ShortcutProfile {};
    });
    preview.onCommandKey = [this](const juce::KeyPress& key) { return dispatchCommandKey(key); };
}

void MainComponent::reapplyShortcutProfile()
{
    shortcutDiagnostics.clear();
    storedShortcutProfile = juce::SystemStats::getOperatingSystemType() == juce::SystemStats::Windows
        ? appSettings.shortcuts.windows : appSettings.shortcuts.mac;
    shortcutProfile = sanitizedProfile(commands, storedShortcutProfile, &shortcutDiagnostics);
    commandRouter = std::make_unique<CommandRouter>(commands, [this] { return shortcutProfile; });
}

bool MainComponent::saveShortcutProfile(const ShortcutProfile& candidate)
{
    try
    {
        settingsStore.update([&candidate](AppSettings& saved)
        {
            if (juce::SystemStats::getOperatingSystemType() == juce::SystemStats::Windows)
                saved.shortcuts.windows = candidate;
            else
                saved.shortcuts.mac = candidate;
        });
    }
    catch (const std::exception& e)
    {
        status.setText("Shortcut not saved: " + juce::String::fromUTF8(e.what()), juce::dontSendNotification);
        return false;
    }
    if (juce::SystemStats::getOperatingSystemType() == juce::SystemStats::Windows)
        appSettings.shortcuts.windows = candidate;
    else
        appSettings.shortcuts.mac = candidate;
    reapplyShortcutProfile();
    if (settingsPanel != nullptr) settingsPanel->setShortcutDiagnostics(shortcutDiagnostics);
    if (embeddedSettingsPanel != nullptr) embeddedSettingsPanel->setShortcutDiagnostics(shortcutDiagnostics);
    return true;
}

CommandFocus MainComponent::commandFocus() const
{
    CommandFocus focus;
    focus.modal = juce::Component::getCurrentlyModalComponent() != nullptr
        || embeddedSettingsHost != nullptr;
    const auto* focused = juce::Component::getCurrentlyFocusedComponent();
    if (focused != nullptr && dynamic_cast<const juce::TextEditor*>(focused) != nullptr)
        focus.textInput = true;
    if (focused != nullptr && (focused == &preview || preview.isParentOf(focused)))
        focus.panel = CommandScope::waveform;
    else if (focused != nullptr && (focused == &folders || folders.isParentOf(focused)
                                    || focused == &favorites || favorites.isParentOf(focused)))
        focus.panel = CommandScope::folders;
    else
        focus.panel = CommandScope::library;
    return focus;
}

bool MainComponent::dispatchCommandKey(const juce::KeyPress& key)
{
    if (commandRouter == nullptr) return false;
    return commandRouter->dispatch(key, commandFocus());
}
MainComponent::MainComponent(const juce::File& databaseFile,bool openDevice,AppSettings initialSettings,
                             juce::File settingsFile,AudioEngine* hostEngine,bool embeddedUiMode,
                             EmbeddedSession sessionState)
    : dbPath(databaseFile), appSettings(std::move(initialSettings)),
      settingsStore(settingsFile != juce::File() ? std::move(settingsFile) : AppSettings::nextUiSettingsFile()),
      ownedEngine(openDevice && hostEngine == nullptr),
      engine(hostEngine != nullptr ? *hostEngine : ownedEngine),
      embeddedUi(embeddedUiMode),
      embeddedSession(std::move(sessionState)),
      preview(engine,dbPath.getParentDirectory().getChildFile("cache"))
{
    theme::apply(appSettings.theme);
    // A save interrupted between its two renames leaves the previous file beside its path; finish
    // or undo that before anything reads the library.
    if (const auto recovered = edit::recoverInterruptedReplacement(); recovered.isNotEmpty())
        juce::Logger::writeToLog(recovered);
    // The same for a duplicate link replacement that stopped between its staging rename and link.
    if (const auto notes = files::DuplicateCleanupService::recoverInterrupted(
            files::DuplicateCleanupService::defaultJournalFile());
        ! notes.isEmpty())
        juce::Logger::writeToLog(notes.joinIntoString("; "));
    // A DAW project owns its own session; only the standalone application may write the shared
    // settings file, register system packs or contact the contribution endpoint.
    persistSessionState = ! embeddedUi && juce::JUCEApplicationBase::getInstance() != nullptr;
    setLookAndFeel(&look); look.refreshColours(); setWantsKeyboardFocus(true);
    roundTagMenu = std::make_unique<RoundTagMenu>();
    roundTagMenu->setComponentID("roundTagMenu");
    addChildComponent(*roundTagMenu);
    roundTagMenu->setAlwaysOnTop(true);
    logoWordmark = juce::ImageFileFormat::loadFrom(BinaryData::logosilver_png, BinaryData::logosilver_pngSize);
    for (auto* c : std::array<juce::Component*,38>{&preview,&library,&search,&addFolder,&rescan,&reanalyze,&newFiles,&smartButton,
         &pause,&cancel,&settings,&tagFilter,&starredButton,&typeFilter,&hideEmpty,&showLoops,&showOneShots,&ignoreSymlinks,&clearFilters,&editTags,&previousPage,&nextPage,&pageSizeBox,&heading,&subtitle,
         &libraryHeading,&rootsLabel,&status,&activityDot,&countLabel,&analysisHeading,&analysisCard,&keyBpm,&tagsLabel,
         &selectedTags,&analysisViewport,&stageLabel,&selectionStatus})
        if (c != nullptr) addAndMakeVisible(c);
    heading.setText("SONICATOR",juce::dontSendNotification);
    heading.setFont(fonts::font(24, fonts::Weight::bold));
    subtitle.setText("INTELLIGENT SAMPLE EXPLORER",juce::dontSendNotification);
    subtitle.setFont(fonts::font(11)); subtitle.setColour(juce::Label::textColourId,theme::muted);
    brandedLogo = logoWordmark.isValid();
    heading.setVisible(! brandedLogo); subtitle.setVisible(! brandedLogo);
    libraryHeading.setText("LIBRARY",juce::dontSendNotification);
    libraryHeading.setColour(juce::Label::textColourId,theme::muted);
    rootsLabel.setText("Add your sample folders.\nOriginal files stay untouched.",juce::dontSendNotification);
    rootsLabel.setJustificationType(juce::Justification::topLeft);
    rootsLabel.setColour(juce::Label::textColourId,theme::muted); rootsLabel.setFont(fonts::font(12));
    analysisHeading.setText("Sample Analysis",juce::dontSendNotification);
    analysisHeading.setFont(fonts::font(17, fonts::Weight::bold));
    showAnalysisPlaceholder();
    analysisViewport.setViewedComponent(&analysisCard,false);
    analysisViewport.setScrollBarsShown(true,false);
    addChildComponent(preview.containerSourceList());
    preview.onSourcesChanged = [this] { resized(); };
    stageLabel.setText("SMART METADATA\n\nType, BPM and Key are in the\nlibrary columns. Smart metadata\nanalyzes new or changed files.",juce::dontSendNotification);
    stageLabel.setFont(fonts::font(11)); stageLabel.setColour(juce::Label::textColourId,theme::muted);
    stageLabel.setJustificationType(juce::Justification::topLeft);
    status.setFont(fonts::font(12)); status.setColour(juce::Label::textColourId,theme::muted);
    countLabel.setFont(fonts::font(12));
    tagsLabel.setText("TAGS",juce::dontSendNotification);
    tagsLabel.setFont(fonts::font(11)); tagsLabel.setColour(juce::Label::textColourId,theme::muted);
    search.setTextToShowWhenEmpty("Search: *.rx2, ext:wav, type:pad, key:Am, C4",theme::muted);
    search.setJustification(juce::Justification::centredLeft);
    // A click types; the history has its own control next to Clear unless the preference asks for
    // the earlier click-to-open behaviour.
    search.setHistoryOnClick(appSettings.searchHistoryOnClick);
    search.setHistoryAvailable(! appSettings.searchHistory.isEmpty());
    search.onTextChanged = [this]
    {
        pendingSearch = search.getText();
        lastSearchChange = juce::Time::getMillisecondCounter();
    };
    search.onReturnKey = [this]
    {
        pendingSearch = search.getText();
        filter.text = pendingSearch.trim(); pageOffset = 0; reloadLibrary();
        recordSearchHistory(filter.text);
    };
    search.onFocusLost = [this] { recordSearchHistory(search.getText().trim()); };
    search.setTooltip(appSettings.searchHistoryOnClick
        ? "Click for the last 10 searches; Escape dismisses history"
        : "Click to type; the clock button opens the last 10 searches");
    search.onHistoryRequest = [this] { showSearchHistory(); };
    search.onEscapeKey = [this]
    {
        search.clear(); pendingSearch.clear(); filter.text.clear(); pageOffset = 0; reloadLibrary();
    };
    search.onClear = search.onEscapeKey;
    search.setComponentID("search");
    selectedTags.setComponentID("selectedTags");
    preview.setComponentID("preview");
    library.setComponentID("library"); status.setComponentID("importStatus"); analysisCard.setComponentID("analysisDetails");
    selectionStatus.setComponentID("selectionStatus");
    selectionStatus.setFont(fonts::font(11));
    selectionStatus.setColour(juce::Label::textColourId,theme::muted);
    selectionStatus.setJustificationType(juce::Justification::centredLeft);
    selectionStatus.setText("No sample selected",juce::dontSendNotification);
    library.onSelected = [this](const SampleRecord& r) { selectSample(r); };
    addAndMakeVisible(listTab); addAndMakeVisible(cloudTab);
    listTab.onClick = [this] { setCloudMode(false); };
    cloudTab.onClick = [this] { setCloudMode(true); };
    cloudPanel = std::make_unique<cloud::CloudView>();
    cloudPanel->setComponentID("cloudView");
    addChildComponent(*cloudPanel);
    cloudPanel->onActivate = [this](int64_t id) { cloudActivate(id); };
    cloudPanel->onHover = [this](int64_t id) { cloudHover(id); };
    cloudPanel->onHoverEnd = [this] { cloudHoverEnd(); };
    cloudPanel->onDragOut = [this](int64_t sampleId, juce::Component* source)
    {
        if (readRepo == nullptr) return false;
        auto record = readRepo->findById(sampleId);
        if (! record) return false;
        if (beginMatchedDrag({ *record }, source)) return true;
        juce::StringArray files;
        files.add(record->filePath);
        return ExternalFileDrag::start(files, source);
    };
    cloudPanel->onContextMenu = [this](int64_t sampleId, juce::Point<int> screenPosition)
    {
        if (readRepo == nullptr) return;
        auto record = readRepo->findById(sampleId);
        if (! record) return;
        showSampleContextMenu({ *record }, *record, screenPosition);
    };
    cloudBuilder = std::make_unique<cloud::CloudBuildService>();
    hoverPool = std::make_unique<juce::ThreadPool>(1); // one decode at a time; stale ones are skipped
    cloudTagSnapshot = readRepo != nullptr ? readRepo->tags() : std::vector<Tag> {};
    library.onTagAltClicked = [this](int64_t sampleId, int64_t tagId)
    {
        std::vector<int64_t> ids { sampleId };
        if (library.isRowSelected(library.getSelectedRow()))
        {
            ids.clear();
            for (const auto& sample : library.getSelectedSamples()) ids.push_back(sample.id);
        }
        removeTagFromSamples(ids, tagId);
    };
    selectedTags.onTagClicked = [this](const Tag& tag, const juce::ModifierKeys& mods)
    {
        if (! mods.isAltDown()) return;
        std::vector<int64_t> ids;
        for (const auto& sample : library.getSelectedSamples()) ids.push_back(sample.id);
        if (ids.empty() && selectedSampleId > 0) ids.push_back(selectedSampleId);
        removeTagFromSamples(ids, tag.id);
    };
    library.onStarClicked = [this](const SampleRecord& r)
    {
        if (writeRepo == nullptr) return;
        const bool next = ! r.starred;
        writeRepo->setStarred({ r.id }, next);
        status.setText(next ? "Starred " + r.fileName
                            : "Removed " + r.fileName + " from favorites",
                        juce::dontSendNotification);
        if (filter.starredOnly)
        {
            reloadLibrary();
            return;
        }
        library.setStarredInPlace({ r.id }, next);
        if (selectedSampleId == r.id && readRepo != nullptr)
            if (auto selected = readRepo->findById(selectedSampleId))
                updateSelectionStatus(*selected);
    };
    library.onOpen = [this](const SampleRecord& r) { openDiscImage(r); };
    library.onDragMessage = [this](const juce::String& message) { status.setText(message,juce::dontSendNotification); };
    library.onMatchedDrag = [this](const std::vector<SampleRecord>& records, juce::Component* source)
    {
        return beginMatchedDrag(records, source);
    };
    library.onContextMenu = [this](const std::vector<SampleRecord>& selection, const SampleRecord& lead, juce::Point<int> position)
    {
        showSampleContextMenu(selection, lead, position);
    };
    library.onSortChanged = [this](int column, bool ascending)
    {
        switch (column)
        {
            case 2: filter.sort = LibrarySort::Format; break;
            case 3: filter.sort = LibrarySort::Duration; break;
            case 4: filter.sort = LibrarySort::Rate; break;
            case 5: filter.sort = LibrarySort::Channels; break;
            case 6: filter.sort = LibrarySort::Status; break;
            case 10: filter.sort = LibrarySort::Type; break;
            case 11: filter.sort = LibrarySort::Bpm; break;
            case 12: filter.sort = LibrarySort::Key; break;
            case 13: filter.sort = LibrarySort::Genre; break;
            default: filter.sort = LibrarySort::Name; break;
        }
        filter.ascending = ascending; pageOffset = 0; reloadLibrary();
    };
    addAndMakeVisible(folders); rootsLabel.setVisible(false);
    folders.setComponentID("folderTree");
    folders.setFolderAliases(appSettings.folderAliases);
    addAndMakeVisible(favoritesLabel); addAndMakeVisible(favorites); addAndMakeVisible(favoritesSplitter);
    favorites.setFolderAliases(appSettings.folderAliases);
    favoritesLabel.setText("FAVORITES",juce::dontSendNotification);
    favoritesLabel.setFont(fonts::font(11)); favoritesLabel.setColour(juce::Label::textColourId,theme::muted);
    favoritesLabel.setComponentID("favoritesLabel"); favorites.setComponentID("favoritesTree");
    favoritesSplitter.setComponentID("favoritesSplitter");
    favoritesLabel.setVisible(false); favorites.setVisible(false); favoritesSplitter.setVisible(false);
    favoritePanelHeight = embeddedUi ? appSettings.pluginLayout.favoritesHeight
                                    : appSettings.favoritesPanelHeight;
    favoritesSplitter.getHeight = [this] { return favoritePanelHeight; };
    favoritesSplitter.setHeight = [this](int height)
    {
        favoritePanelHeight = juce::jlimit(60, 2000, height);
        resized();
    };
    layoutPreferences = embeddedUi ? appSettings.pluginLayout : appSettings.standaloneLayout;
    layoutPreferences.previewHeight = layoutPreferences.previewHeight > 0 ? layoutPreferences.previewHeight
                                                                          : minPreviewHeight;
    layoutPreferences.favoritesHeight = favoritePanelHeight;
    for (auto* divider : { &foldersDivider, &analysisDivider, &previewDivider })
        addAndMakeVisible(divider);
    foldersDivider.setComponentID("foldersDivider");
    analysisDivider.setComponentID("analysisDivider");
    previewDivider.setComponentID("previewDivider");
    foldersDivider.getPreferred = [this] { return layoutBounds.folders.getWidth(); };
    foldersDivider.onDragBegin = [this](int base) { dragBaseFolders = base; };
    foldersDivider.onDrag = [this](int delta)
    {
        layoutPreferences.foldersWidth = juce::jmax(minFoldersWidth, dragBaseFolders + delta);
        resized();
    };
    foldersDivider.onCommit = [this] { persistLayout(); };
    foldersDivider.onReset = [this] { resetPanelSize("folders"); };
    analysisDivider.getPreferred = [this] { return layoutBounds.analysis.getWidth(); };
    analysisDivider.onDragBegin = [this](int base) { dragBaseAnalysis = base; };
    analysisDivider.onDrag = [this](int delta)
    {
        // Dragging the right handle left makes the analysis panel wider.
        layoutPreferences.analysisWidth = juce::jmax(minAnalysisWidth, dragBaseAnalysis - delta);
        resized();
    };
    analysisDivider.onCommit = [this] { persistLayout(); };
    analysisDivider.onReset = [this] { resetPanelSize("analysis"); };
    previewDivider.getPreferred = [this] { return layoutBounds.preview.getHeight(); };
    previewDivider.onDragBegin = [this](int base) { dragBasePreview = base; };
    previewDivider.onDrag = [this](int delta)
    {
        // Dragging the top edge up makes the preview taller.
        layoutPreferences.previewHeight = juce::jmax(minPreviewHeight, dragBasePreview - delta);
        resized();
    };
    previewDivider.onCommit = [this] { persistLayout(); };
    previewDivider.onReset = [this] { resetPanelSize("preview"); };
    favoritesSplitter.onRelease = [this]
    {
        persistLayout();
    };
    folders.onFolder = [this](juce::String path)
    {
        favorites.clearSelection();
        if (juce::File::isAbsolutePath(path) && juce::File(path).hasFileExtension("iso;bin;mdx;cue;akai;img"))
        {
            SampleRecord image;
            image.filePath = path;
            image.format = juce::File(path).getFileExtension().substring(1).toUpperCase();
            openDiscImage(image);
            return;
        }
        if (discBrowser) { discBrowser->closeImage(); discBrowser->setVisible(false); }
        // The folder tree re-selects the restored folder on its own after a background index
        // arrives. That is not a folder change and must not drop the project's sample.
        if (path == selectedFolder) return;
        selectedFolder = std::move(path); pageOffset = 0;
        sessionSamplePath.clear(); markSessionStateDirty();
        reloadLibrary();
    };
    folders.onDiscFolder = [this](const juce::String& image, DiscEntryId directory)
    {
        if (! discBrowser || ! discBrowser->canReuseImage(juce::File(image)))
        {
            SampleRecord record;
            record.filePath = image;
            record.format = juce::File(image).getFileExtension().substring(1).toUpperCase();
            openDiscImage(record);
        }
        if (discBrowser)
        {
            discBrowser->setVisible(true);
            discBrowser->showDirectory(directory);
        }
    };
    folders.onContextMenu = [this](const juce::String& path, bool allSamples, juce::Point<int> position)
    {
        showFolderContextMenu(path, allSamples, position);
    };
    favorites.onFolder = [this](juce::String path)
    {
        if (path.isEmpty()) return;
        // As above: a programmatic re-selection of the folder already shown keeps the sample and
        // the tree selection.
        if (path == selectedFolder) return;
        if (discBrowser) { discBrowser->closeImage(); discBrowser->setVisible(false); }
        folders.clearSelection();
        selectedFolder = std::move(path); pageOffset = 0;
        sessionSamplePath.clear(); markSessionStateDirty();
        reloadLibrary();
    };
    favorites.onContextMenu = [this](const juce::String& path, bool, juce::Point<int> position)
    {
        showFavoriteContextMenu(path, position);
    };
    addFolder.onClick = [this] { chooseFolder(); };
    rescan.onClick = [this] { requestScan(false); };
    newFiles.onClick = [this] { scanRoots(false, true); };
    newFiles.setButtonText("Rescan and analyze new files");
    reanalyze.onClick = [this] { requestScan(true); };
    smartButton.onClick = [this]
    {
        if (! smart || ! readRepo) return;
        if (isSplitRunning())
        {
            status.setText("Wait for Split to finish.",juce::dontSendNotification);
            return;
        }
        if (indexer && indexer->progress().running)
        {
            status.setText("Wait for the library scan to finish before smart analysis.",juce::dontSendNotification);
            return;
        }
        if (smart->progress().running)
        {
            status.setText("Smart metadata analysis is already running.", juce::dontSendNotification);
            return;
        }
        if (! smart->start())
            status.setText("Smart metadata analysis could not start.",juce::dontSendNotification);
    };
    smartButton.setTooltip("Read Type / BPM / Key metadata for missing or changed files in the background");
    addAndMakeVisible(libraryMenu);
    libraryMenu.setComponentID("libraryMenu");
    libraryMenu.setTrailingIcon(ActionIcon::chevronDown);
    libraryMenu.setWantsKeyboardFocus(false);
    libraryMenu.setMouseClickGrabsKeyboardFocus(false);
    for (auto* c : { &addFolder, &rescan, &newFiles, &reanalyze, &smartButton }) c->setVisible(false);
    libraryHeading.setVisible(false);
    libraryMenu.onClick = [this]
    {
        juce::PopupMenu menu;
        menu.setLookAndFeel(&look);
        // Keep every item clickable even while a background job runs. The actions report what to
        // wait for instead of leaving the whole menu grey and apparently broken.
        if (embeddedUi)
        {
            menu.addSectionHeader("Scanning and file processing run in the SONICATOR app");
        }
        else
        {
            menu.addItem(1, "Add folder...");
            menu.addItem(2, "Rescan");
            menu.addItem(3, "Rescan and analyze new files");
            menu.addSeparator();
            menu.addItem(4, "Reanalyze all");
            menu.addItem(5, "Smart metadata");
            menu.addSeparator();
        }
        menu.addItem(6, "Show ignored", true, filter.showIgnored);
        menu.addItem(8, "Ignore symlinks", true, filter.ignoreSymlinks);
        menu.addItem(9, "Show loops", true, filter.showLoops);
        menu.addItem(10, "Show one-shots", true, filter.showOneShots);
        if (! embeddedUi)
        {
            menu.addSeparator();
            menu.addItem(7, "Find duplicates...");
        }
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&libraryMenu),
            [safe = juce::Component::SafePointer<MainComponent>(this)](int result)
            {
                if (! safe) return;
                switch (result)
                {
                    case 1: safe->chooseFolder(); break;
                    case 2: safe->requestScan(false); break;
                    case 3: safe->scanRoots(false, true); break;
                    case 4: safe->requestScan(true); break;
                    case 5: safe->smartButton.onClick(); break;
                    case 7: safe->openDuplicateCleanup(); break;
                    case 8: safe->setIgnoreSymlinks(! safe->filter.ignoreSymlinks); break;
                    case 9:
                        safe->filter.showLoops = ! safe->filter.showLoops;
                        safe->showLoops.setToggleState(safe->filter.showLoops, juce::dontSendNotification);
                        safe->persistStructureFilters();
                        safe->pageOffset = 0;
                        safe->reloadLibrary();
                        break;
                    case 10:
                        safe->filter.showOneShots = ! safe->filter.showOneShots;
                        safe->showOneShots.setToggleState(safe->filter.showOneShots, juce::dontSendNotification);
                        safe->persistStructureFilters();
                        safe->pageOffset = 0;
                        safe->reloadLibrary();
                        break;
                    case 6:
                        if (safe->readRepo == nullptr) break;
                        safe->filter.showIgnored = ! safe->filter.showIgnored;
                        safe->persistShowIgnored();
                        safe->pageOffset = 0;
                        safe->ensureSelectedFolderVisible();
                        safe->reloadLibrary();
                        safe->requestFolderTreeRefresh();
                        safe->refreshFavorites();
                        break;
                    default: break;
                }
            });
    };
    pause.onClick = [this]
    {
        // Pause and Cancel act on whichever background job is running.
        if (indexer && indexer->progress().running)
            indexer->setPaused(! indexer->progress().paused);
        else if (smart && smart->progress().running)
            smart->setPaused(! smart->progress().paused);
    };
    cancel.onClick = [this]
    {
        if (splitter && splitter->progress().running) splitter->cancel();
        else if (indexer && indexer->progress().running) indexer->cancel();
        else if (smart && smart->progress().running) smart->cancel();
        else if (tagging && tagging->progress().running) tagging->cancel();
        else if (autoTag) autoTag->cancel();
        else if (normalizer && normalizer->progress().running) normalizer->cancel();
    };
    settings.onClick = [this] { showSettings(); };
    tagFilter.onClick = [this] { showTagFilterMenu(); };
    starredButton.onClick = [this]
    {
        filter.starredOnly = ! filter.starredOnly; pageOffset = 0; reloadLibrary();
    };
    hideEmpty.setComponentID("hideEmpty");
    filter.hideEmpty = appSettings.hideEmpty;
    filter.showIgnored = appSettings.showIgnored;
    hideEmpty.setToggleState(filter.hideEmpty,juce::dontSendNotification);
    hideEmpty.setTooltip("Hide empty and unsupported files: silence below -60 dBFS, or a format this build cannot read");
    hideEmpty.onClick = [this]
    {
        filter.hideEmpty = hideEmpty.getToggleState();
        persistHideEmpty(); pageOffset = 0; reloadLibrary();
    };
    ignoreSymlinks.setComponentID("ignoreSymlinks");
    filter.ignoreSymlinks = appSettings.ignoreSymlinks;
    ignoreSymlinks.setToggleState(filter.ignoreSymlinks, juce::dontSendNotification);
    ignoreSymlinks.setTooltip("Hide files whose path is a symbolic link, such as copies replaced "
                              "by links in duplicate cleanup");
    ignoreSymlinks.onClick = [this] { setIgnoreSymlinks(ignoreSymlinks.getToggleState()); };
    showLoops.setComponentID("showLoops");
    filter.showLoops = appSettings.showLoops;
    showLoops.setToggleState(filter.showLoops, juce::dontSendNotification);
    showLoops.setTooltip("Show files the analysis confirmed as loops. With Show one-shots off, "
                         "only loops remain");
    showLoops.onClick = [this]
    {
        filter.showLoops = showLoops.getToggleState();
        persistStructureFilters(); pageOffset = 0; reloadLibrary();
    };
    showOneShots.setComponentID("showOneShots");
    filter.showOneShots = appSettings.showOneShots;
    showOneShots.setToggleState(filter.showOneShots, juce::dontSendNotification);
    showOneShots.setTooltip("Show files the analysis confirmed as one-shots. With Show loops off, "
                            "only one-shots remain");
    showOneShots.onClick = [this]
    {
        filter.showOneShots = showOneShots.getToggleState();
        persistStructureFilters(); pageOffset = 0; reloadLibrary();
    };
    typeFilter.setComponentID("typeFilter");
    typeFilter.setTooltip("Show one sample type, such as snare. Combines with tags, search and the "
                          "other filters");
    typeFilter.onClick = [this] { showTypeFilterMenu(); };
    updateTypeFilterChip();
    clearFilters.onClick = [this]
    {
        search.clear(); pendingSearch.clear();
        filter.text.clear(); filter.includeTags.clear(); filter.excludeTags.clear();
        filter.matchAnyTag = false; filter.starredOnly = false;
        filter.hideDuplicates = false; filter.hideEmpty = false; filter.duplicateHash.clear();
        filter.types.clear();
        filter.showLoops = true; filter.showOneShots = true;
        showLoops.setToggleState(true, juce::dontSendNotification);
        showOneShots.setToggleState(true, juce::dontSendNotification);
        updateTypeFilterChip();
        persistHideEmpty();
        persistStructureFilters();
        pageOffset = 0;
        setIgnoreSymlinks(false);
    };
    editTags.onClick = [this] { showEditTagsMenu(); };
    pageSize = appSettings.pageSize;
    pageSizeBox.setComponentID("pageSize");
    pageSizeBox.addItem("100",100); pageSizeBox.addItem("1 000",1000);
    pageSizeBox.addItem("10 000",10000); pageSizeBox.addItem("All",1000000);
    pageSizeBox.setSelectedId(pageSize == 0 ? 1000000 : pageSize,juce::dontSendNotification);
    pageSizeBox.setTooltip("Rows shown on one page");
    pageSizeBox.onChange = [this]
    {
        const auto selected = pageSizeBox.getSelectedId();
        pageSize = selected == 1000000 ? 0 : selected;
        pageOffset = 0;
        try { settingsStore.update([this](AppSettings& saved) { saved.pageSize = pageSize; }); } catch (...) {}
        reloadLibrary();
        if (isShowing()) library.focusForNavigation();
    };
    previousPage.onClick = [this] { pageOffset = juce::jmax(0,pageOffset-pageSize); reloadLibrary(); };
    nextPage.onClick = [this] { pageOffset += pageSize; reloadLibrary(); };
    preview.onFileSavedInPlace = [this](const juce::File& target, int64_t sampleId)
    { startCatalogSync(target, sampleId, false); };
    preview.onCopySaved = [this](const juce::File& copy, int64_t templateId)
    { startCatalogSync(copy, templateId, true); };
    preview.onAction = [this] { if (isShowing()) library.focusForNavigation(); };
    preview.onSettingsChanged = [this] { persistPreviewSettings(); };
    preview.applyPersistedSettings(appSettings);
    for (auto* button : std::array<juce::Button*,18>{&addFolder,&rescan,&reanalyze,&newFiles,&smartButton,&pause,&cancel,&settings,
          &tagFilter,&starredButton,&typeFilter,&hideEmpty,&showLoops,&showOneShots,&clearFilters,&editTags,&previousPage,&nextPage})
    {
        button->setWantsKeyboardFocus(false); button->setMouseClickGrabsKeyboardFocus(false);
        if (button == &addFolder || button == &settings) continue; // Modal dialogs must retain their own focus.
        auto action = button->onClick;
        button->onClick = [this,action] { if (action) action(); if (isShowing()) library.focusForNavigation(); };
    }
    editTags.setEnabled(false);
    clearFilters.setEnabled(false);
    // Lay out before the first tree build: JUCE sizes a TreeView's scrollable content when items
    // are created, so a tree populated while it is still 0x0 keeps truncating its rows.
    setSize(1280,800);
    try
    {
        writeDb = std::make_unique<Database>(dbPath); writeRepo = std::make_unique<SampleRepository>(*writeDb);
        // Only the application owns the catalog's schema. A plug-in checks the version and
        // refuses to touch a catalog written by another build, so opening it in a DAW can never
        // make the library unreadable to the application.
        if (embeddedUi)
            writeRepo->requireCompatibleSchema();
        else
            writeRepo->createTables();
        if (! embeddedUi)
        {
            // Formats this build no longer indexes leave the catalog; the files stay on disk.
            if (purgeUnsupportedRows(*writeRepo) > 0)
                writeRepo->refreshDuplicates();
        }
        readDb = std::make_unique<Database>(dbPath); readRepo = std::make_unique<SampleRepository>(*readDb);
        AcousticClassifierService::getSharedAdaptiveClassifier().loadFromRepository(*readRepo);
        loadShippedGlobalPrototypes();
        folderIndexRunner = std::make_unique<FolderIndexRunner>(*readRepo);
        // A catalog written before the flag existed needs one pass before the view can hide links.
        if (! embeddedUi && filter.ignoreSymlinks && ! readRepo->symlinkFlagsComplete())
            startSymlinkFlagRefresh();
        indexer = std::make_unique<IndexingService>(*writeRepo);
        // Only a real application startup may register the user's system pack folders.
        if (persistSessionState)
        {
            const auto reasonPacks=juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                .getChildFile("Library/Application Support/Propellerhead Software/Packs");
            const auto abletonPacks=juce::File::getSpecialLocation(juce::File::userMusicDirectory)
                .getChildFile("Ableton/Factory Packs");
            juce::StringArray packRoots;
            if (registerReasonPacks(*writeRepo,settingsStore,reasonPacks))
                packRoots.add(reasonPacks.getFullPathName());
            if (registerAbletonPacks(*writeRepo,settingsStore,abletonPacks))
                packRoots.add(abletonPacks.getFullPathName());
            if (! packRoots.isEmpty() && indexer->start(packRoots)) wasRunning=true;
            const auto saved=settingsStore.load();
            appSettings.reasonPacksAdded=saved.reasonPacksAdded;
            appSettings.abletonPacksAdded=saved.abletonPacksAdded;
        }
        smart = std::make_unique<SmartAnalysisService>(*writeRepo);
        tagging = std::make_unique<TaggingService>(*writeRepo);
        tagging->onTagsChanged = [safe = juce::Component::SafePointer<MainComponent>(this)](std::vector<int64_t> ids)
        {
            juce::MessageManager::callAsync([safe, ids = std::move(ids)]
            {
                if (safe != nullptr) safe->triggerAdaptiveLearning(ids);
            });
        };
        autoTag = std::make_unique<AutoTagService>(*writeRepo);
        splitter = std::make_unique<SplitService>();
        extractor = std::make_unique<ExtractService>();
        normalizer = std::make_unique<NormalizeService>();
        refreshFavorites();   // the favorites panel follows its visibility at once; its content
                              // arrives with the background build
        // A plugin restores its own project, never the application's last session.
        const auto initialFolder = embeddedUi ? embeddedSession.lastFolder : appSettings.lastFolder;
        const auto initialSample = embeddedUi ? embeddedSession.lastSamplePath
                                              : appSettings.lastSamplePath;
        applySessionSelection(initialFolder, initialSample);
        sessionDirty = false;
        status.setText(catalogCount > 0
            ? juce::String(catalogCount) + " samples loaded"
            : "Ready. Add a folder to begin.",juce::dontSendNotification);
    }
    catch (const std::exception& e)
    {
        catalogOpenError = juce::String::fromUTF8(e.what());
        status.setText("Database error: " + catalogOpenError, juce::dontSendNotification);
    }
    const auto missing = missingRoots();
    if (! missing.isEmpty())
    {
        status.setText(juce::String(missing.size())
            + " catalog folder(s) not found on this computer. Settings > Relocate folders...",
            juce::dontSendNotification);
        status.setTooltip(missing.joinIntoString("\n"));
    }
    else if (engine.error().isNotEmpty())
    {
        status.setText("Audio device: " + engine.error(),juce::dontSendNotification);
    }
    pause.setEnabled(false); cancel.setEnabled(false);
    // A plugin browses the shared library; scanning, analysis and file processing stay in the
    // application, so two processes never run long writes against one catalog.
    addFolder.setEnabled(indexer != nullptr && ! embeddedUi);
    rescan.setEnabled(indexer != nullptr && ! embeddedUi);
    reanalyze.setEnabled(indexer != nullptr && ! embeddedUi);
    newFiles.setEnabled(indexer != nullptr && ! embeddedUi);
    smartButton.setEnabled(indexer != nullptr && ! embeddedUi);
    if (embeddedUi)
        for (auto* button : { &addFolder, &rescan, &reanalyze, &newFiles, &smartButton })
            button->setTooltip("Scanning and file processing run in the SONICATOR app");
    sendLookAndFeelChange();
    addKeyListener(&enterKeyListener);
    startTimerHz(10);
    if (writeRepo != nullptr && ! embeddedUi)
        featureV2Worker = std::make_unique<std::thread>([this, repo = writeRepo.get()]
        {
            backfillFeatureV2(*repo, [this] { return cancelFeatureV2.load(); });
            if (! cancelFeatureV2.load())
                AcousticClassifierService::getSharedAdaptiveClassifier().loadFromRepository(*repo);
        });
    juce::Desktop::getInstance().addDarkModeSettingListener(this);
    registerCommands();
    if (persistSessionState && ! embeddedUi)
    {
        startContribution();
        if (! contributionConsentCurrent())
        {
            juce::Component::SafePointer<MainComponent> safe(this);
            juce::MessageManager::callAsync([safe]() mutable
            {
                if (safe != nullptr) safe->askContributionConsent();
            });
        }
    }
    // From here on every selection change is the user's, and may be stored in the project.
    sessionLive = true;
    if (appSettings.cloudView) setCloudMode(true);
    if (persistSessionState && ! embeddedUi)
    {
        // One check per launch, off the message thread. A failed or equal release stays silent.
        juce::Component::SafePointer<MainComponent> safe(this);
        juce::Timer::callAfterDelay(3000, [safe]
        {
            if (safe == nullptr) return;
            juce::Thread::launch([safe]
            {
                const auto release = updates::checkForUpdate(version());
                juce::MessageManager::callAsync([safe, release]
                {
                    if (safe == nullptr || release.error.isNotEmpty() || ! release.published
                        || ! release.newer) return;
                    presentUpdateDialog(safe, release, 20);
                });
            });
        });
    }
}
MainComponent::~MainComponent()
{
    // The menu provider outlives this object; never let a later menu read a dead window.
    setEditCommandProfileProvider(nullptr);
    juce::PopupMenu::dismissAllActiveMenus();
    if (persistSessionState) saveSessionState();
    removeKeyListener(&enterKeyListener);
    juce::Desktop::getInstance().removeDarkModeSettingListener(this);
    stopTimer(); chooser.reset(); relocateDialog.deleteAndZero();
    engine.hover().stop(); // the plug-in's engine outlives this editor: never leave it sounding
    cancelSymlinkFlags = true;
    if (symlinkFlagWork.valid()) symlinkFlagWork.wait();
    if (cleanupCatalogWork.valid()) cleanupCatalogWork.wait();
    if (folderGenreWork.valid()) folderGenreWork.wait();
    if (cloudBuilder) cloudBuilder.reset(); // stops and waits for the layout worker
    if (hoverPool) { hoverPool->removeAllJobs(true, -1); hoverPool.reset(); }
    ++(*cloudHoverGeneration);
    embeddedSettingsPanel = nullptr;
    embeddedSettingsHost.deleteAndZero();
    embeddedDialogHost.deleteAndZero();
    settingsDialog.deleteAndZero(); audioDialog.deleteAndZero();
    // The duplicate window auditions through this component's preview; close it while that still
    // exists, so its destructor can restore the preview instead of touching a dying one.
    if (duplicateDialog != nullptr)
    {
        // Detach first: the window may be deleted after this destructor body, when the preview is
        // already gone. Nothing it reports may reach a half-destroyed component.
        if (auto* content = dynamic_cast<DuplicateCleanupDialog*>(
                duplicateDialog->getContentComponent()))
        {
            content->onAudition = nullptr;
            content->onStopAudition = nullptr;
            content->onFinished = nullptr;
            content->onCancel = nullptr;
            content->onRun = nullptr;
        }
        duplicateDialog->exitModalState(0);
        duplicateDialog = nullptr;
    }
    if (splitDialog != nullptr)
    {
        splitDialog->setLookAndFeel(nullptr);
        closeSplitDialog();
    }
    if (extractDialog != nullptr)
    {
        extractDialog->setLookAndFeel(nullptr);
        closeExtractDialog();
    }
    if (splitter) { splitter->cancel(); splitter->waitUntilFinished(-1); }
    if (extractor) { extractor->cancel(); extractor->waitUntilFinished(-1); }
    if (normalizer) { normalizer->cancel(); normalizer->waitUntilFinished(-1); }
    if (contributionScheduler != nullptr)
    {
        contributionScheduler->stop();
        contributionScheduler.reset();
    }
    contributionUploader.reset();
    cancelFeedback = true;
    {
        std::lock_guard lock(catalogSyncMutex);
        catalogSyncStop = true;
    }
    if (catalogSyncWorker && catalogSyncWorker->joinable()) catalogSyncWorker->join();
    catalogSyncRequests.clear();
    if (typeFeedbackWorker && typeFeedbackWorker->joinable()) typeFeedbackWorker->join();
    cancelFeatureV2 = true;
    if (featureV2Worker && featureV2Worker->joinable()) featureV2Worker->join();
    smart.reset(); indexer.reset(); tagging.reset(); autoTag.reset(); splitter.reset(); extractor.reset();
    normalizer.reset();
    setLookAndFeel(nullptr);
}
bool MainComponent::contains(const std::vector<int64_t>& values, int64_t id)
{
    return std::find(values.begin(), values.end(), id) != values.end();
}
void MainComponent::toggle(std::vector<int64_t>& values, int64_t id)
{
    const auto found = std::find(values.begin(), values.end(), id);
    if (found == values.end()) values.push_back(id); else values.erase(found);
}
void MainComponent::searchLibrary(const juce::String& text)
{
    pendingSearch = text;
    filter.text = text.trim();
    lastSearchChange = juce::Time::getMillisecondCounter();
    pageOffset = 0;
    reloadLibrary();
}
void MainComponent::recordSearchHistory(const juce::String& text)
{
    if (text.isEmpty()) return;
    if (! appSettings.searchHistory.contains(text))
    {
        appSettings.searchHistory.insert(0, text);
        while (appSettings.searchHistory.size() > 10) appSettings.searchHistory.remove(appSettings.searchHistory.size() - 1);
        try { settingsStore.update([this](AppSettings& saved) { saved.searchHistory = appSettings.searchHistory; }); } catch (...) {}
        search.setHistoryAvailable(true);
    }
}
void MainComponent::showSearchHistory()
{
    juce::PopupMenu menu;
    menu.setLookAndFeel(&look);
    const auto history = appSettings.searchHistory;
    if (history.isEmpty()) return;
    for (int i = 0; i < history.size(); ++i) menu.addItem(i + 1, history[i]);
    menu.addSeparator(); menu.addItem(100, "Clear history");
    juce::Component::SafePointer<MainComponent> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&search),
        [safe, history](int result)
        {
            if (safe == nullptr) return;
            if (result == 100)
            {
                safe->appSettings.searchHistory.clear();
                try { safe->settingsStore.update([](AppSettings& saved) { saved.searchHistory.clear(); }); } catch (...) {}
                if (safe->isShowing()) safe->search.grabKeyboardFocus();
                return;
            }
            if (result <= 0 || result > history.size())
            {
                if (safe->isShowing()) safe->search.grabKeyboardFocus();
                return;
            }
            safe->search.setText(history[result - 1], true);
            safe->searchLibrary(history[result - 1]);
            if (safe->isShowing()) safe->search.grabKeyboardFocus();
        });
}
void MainComponent::persistPreviewSettings()
{
    appSettings.midiPreview = preview.midiPreviewEnabled();
    appSettings.skipSilence = preview.skipSilenceEnabled();
    appSettings.autoPreview = preview.autoPreviewEnabled();
    appSettings.loopPreview = preview.loopEnabled();
    appSettings.rexGate = preview.rexGateEnabled();
    appSettings.spectrumView = preview.spectrumViewEnabled();
    appSettings.spectrumLogScale = preview.spectrumLogScale();
    try
    {
        settingsStore.update([this](AppSettings& saved)
        {
            saved.midiPreview = appSettings.midiPreview;
            saved.skipSilence = appSettings.skipSilence;
            saved.autoPreview = appSettings.autoPreview;
            saved.loopPreview = appSettings.loopPreview;
            saved.rexGate = appSettings.rexGate;
            saved.spectrumView = appSettings.spectrumView;
            saved.spectrumLogScale = appSettings.spectrumLogScale;
        });
    }
    catch (const std::exception& e)
    { status.setText("Cannot save preview settings: " + juce::String::fromUTF8(e.what()), juce::dontSendNotification); }
}

void MainComponent::showCloudView(bool show)
{
    setCloudMode(show);
}

void MainComponent::setCloudMode(bool show)
{
    if (cloudVisible == show && cloudPanel != nullptr && cloudPanel->isVisible() == show)
    {
        listTab.setToggleState(! show, juce::dontSendNotification);
        cloudTab.setToggleState(show, juce::dontSendNotification);
        return;
    }
    cloudVisible = show;
    if (cloudPanel != nullptr) cloudPanel->setVisible(show);
    library.setVisible(! show);
    if (discBrowser != nullptr) discBrowser->setVisible(! show);
    listTab.setToggleState(! show, juce::dontSendNotification);
    cloudTab.setToggleState(show, juce::dontSendNotification);
    previousPage.setVisible(! show);
    nextPage.setVisible(! show);
    pageSizeBox.setVisible(! show);
    if (show)
    {
        // Never build the first point layer inside the constructor: the graphics backend is not up
        // before the window is shown, and the map waits for the message loop instead.
        if (cloudPanel != nullptr && ! cloudPanel->hasLayout())
            juce::MessageManager::callAsync([safe = juce::Component::SafePointer<MainComponent>(this)]
            {
                if (safe != nullptr) safe->showLastCachedCloudMap();
            });
        updateCloudMatches();
        requestCloudBuild();
    }
    else cloudHoverEnd();
    resized();
    persistCloudMode();
}

void MainComponent::persistCloudMode()
{
    appSettings.cloudView = cloudVisible;
    if (! persistSessionState) return;
    try { settingsStore.update([this](AppSettings& saved) { saved.cloudView = cloudVisible; }); }
    catch (const std::exception& e)
    { status.setText("Cannot save the view mode: " + juce::String::fromUTF8(e.what()), juce::dontSendNotification); }
}

void MainComponent::showLastCachedCloudMap()
{
    if (cloudPanel == nullptr || cloudPanel->hasLayout()) return;
    // The previous map goes on screen before the catalog is read: opening a retagged library no
    // longer starts with "Building the map" over an empty view. The build then checks whether the
    // catalog actually changed and replaces this map only when it did.
    if (auto last = cloud::readNewestCloudLayout(cloud::defaultCloudCacheFolder(), dbPath))
    {
        const auto points = last->points.size();
        cloudPanel->setLayout(std::move(*last));
        cloudPanel->setSelectedSample(selectedSampleId);
        updateCloudMatches();
        juce::Logger::writeToLog("[cloud] showing the last cached map, "
                                 + juce::String((int) points) + " points");
    }
}

void MainComponent::requestCloudBuild()
{
    if (! cloudVisible || cloudBuilder == nullptr) return;
    cloudBuildScheduled = true;
    cloudBuildAtMs = juce::Time::getMillisecondCounter() + 500; // AutoTag and bulk tagging arrive in bursts
}

void MainComponent::pollCloudBuild()
{
    if (cloudBuilder == nullptr || cloudPanel == nullptr) return;
    if (auto layout = cloudBuilder->poll())
    {
        cloudPanel->setLoading(false);
        if (layout->valid())
        {
            cloudPanel->setLayout(std::move(*layout));
            cloudPanel->setSelectedSample(selectedSampleId);
            updateCloudMatches();
        }
        return;
    }
    // A build started by a tag change keeps the previous map on screen; the caption says why it is
    // busy instead of leaving the view looking frozen for the thirty seconds a full catalog takes.
    if (cloudVisible && cloudBuilder->isRunning())
        cloudPanel->setLoading(true, "Building the map...");
    else if (cloudVisible && ! cloudBuildScheduled && ! cloudPanel->hasLayout())
        cloudPanel->setLoading(false); // the build ended without a map: show the empty message
    else if (cloudPanel->hasLayout())
        cloudPanel->setLoading(false); // the build ended and published: drop the caption
}

void MainComponent::updateCloudMatches()
{
    if (cloudPanel == nullptr || ! cloudPanel->hasLayout() || readRepo == nullptr) return;
    const auto& points = cloudPanel->layout().points;
    // The Cloud map follows the chosen tags and the Type filter: both are whole-catalog facts that
    // the map itself is built from. Search text, the folder, paging and the structure toggles stay
    // with the list, because they describe what the table shows rather than what a file is.
    LibraryFilter cloudFilter;
    cloudFilter.includeTags = filter.includeTags;
    cloudFilter.excludeTags = filter.excludeTags;
    cloudFilter.matchAnyTag = filter.matchAnyTag;
    cloudFilter.types = filter.types;
    const bool narrowed = ! cloudFilter.includeTags.empty() || ! cloudFilter.excludeTags.empty()
        || ! cloudFilter.types.empty();
    cloudMatches.clear();
    bool filtered = false;
    if (narrowed)
    {
        try
        {
            std::set<int64_t> keep;
            for (auto id : readRepo->queryIds(cloudFilter)) keep.insert(id);
            cloudMatches.assign(points.size(), false);
            for (size_t i = 0; i < points.size(); ++i)
                cloudMatches[i] = keep.count(points[i].sampleId) > 0;
            filtered = true;
        }
        catch (const std::exception& e)
        {
            // Showing the whole map is the honest answer when the filter cannot be evaluated; it
            // is better than hiding every point behind a failed query.
            cloudMatches.clear();
            status.setText("Cannot apply the filter to the map: " + juce::String::fromUTF8(e.what()),
                           juce::dontSendNotification);
        }
    }
    cloudPanel->setFilter(cloudMatches);
    int matching = 0;
    for (bool match : cloudMatches) matching += match ? 1 : 0;
    cloudPanel->setNotice(filtered && matching == 0 ? "Nothing matches the tags and type filter"
                                                    : juce::String());
    updateCloudCaption();
}

void MainComponent::updateCloudCaption()
{
    if (cloudPanel == nullptr || ! cloudPanel->hasLayout()) return;
    int matching = 0;
    for (bool match : cloudMatches) matching += match ? 1 : 0;
    auto text = juce::String((int) cloudPanel->layout().points.size()) + " tagged files on the map";
    if (! cloudMatches.empty()) text += "  |  " + juce::String(matching) + " matching";
    countLabel.setText(text, juce::dontSendNotification);
}

void MainComponent::cloudActivate(int64_t sampleId)
{
    if (readRepo == nullptr) return;
    auto record = readRepo->findById(sampleId);
    if (! record) return;
    cloudHoverEnd();
    selectSample(*record);
    // The list may be on another page: select the row when it is there, never a different file.
    library.selectSampleById(sampleId);
    if (cloudPanel != nullptr)
    {
        cloudPanel->setSelectedSample(sampleId);
        cloudPanel->focusOnSample(sampleId);
    }
}

void MainComponent::cloudHover(int64_t sampleId)
{
    if (! cloudVisible || readRepo == nullptr || cloudPanel == nullptr) return;
    cloudHoverSample = sampleId;
    const auto generation = ++(*cloudHoverGeneration);
    auto record = readRepo->findById(sampleId);
    if (! record) return;

    juce::StringArray tagNames;
    for (const auto& tag : readRepo->tagsForSample(sampleId)) tagNames.add(tag.name);
    const auto separator = juce::String::fromUTF8("  \xc2\xb7  ");
    auto caption = record->fileName;
    if (! tagNames.isEmpty()) caption += separator + tagNames.joinIntoString(", ");
    caption += separator + juce::String(record->duration, 2) + " s" + separator + record->format;
    const bool playable = record->version >= 1 && record->error.isEmpty();
    if (! playable) caption += separator + "no audio position";
    cloudPanel->setLabel(sampleId, caption);

    const juce::File file(record->filePath);
    if (! playable || ! file.existsAsFile() || engine.isPlaying())
    {
        engine.hover().clear();
        return;
    }
    auto current = cloudHoverGeneration;
    auto safe = juce::Component::SafePointer<MainComponent>(this);
    hoverPool->addJob([safe, current, generation, sampleId, file]
    {
        if (current->load() != generation) return; // the cursor already moved on
        auto clip = HoverAudition::load(file, 10.0);
        if (clip == nullptr) return;
        juce::MessageManager::callAsync([safe, current, generation, sampleId, clip]
        {
            auto* self = safe.getComponent();
            if (self == nullptr) return;
            if (current->load() != generation || self->cloudHoverSample != sampleId) return;
            if (self->engine.isPlaying()) return;
            self->engine.hover().setAudio(clip);
            self->engine.hover().play();
        });
    });
}

void MainComponent::cloudHoverEnd()
{
    ++(*cloudHoverGeneration);
    cloudHoverSample = 0;
    engine.hover().stop();
}

void MainComponent::persistHideEmpty()
{
    appSettings.hideEmpty = filter.hideEmpty;
    try { settingsStore.update([this](AppSettings& saved) { saved.hideEmpty = filter.hideEmpty; }); }
    catch (const std::exception& e)
    { status.setText("Cannot save Hide empty: " + juce::String::fromUTF8(e.what()), juce::dontSendNotification); }
}
void MainComponent::persistShowIgnored()
{
    appSettings.showIgnored = filter.showIgnored;
    try { settingsStore.update([this](AppSettings& saved) { saved.showIgnored = filter.showIgnored; }); }
    catch (const std::exception& e)
    { status.setText("Cannot save Show ignored: " + juce::String::fromUTF8(e.what()), juce::dontSendNotification); }
}
void MainComponent::persistStructureFilters()
{
    appSettings.showLoops = filter.showLoops;
    appSettings.showOneShots = filter.showOneShots;
    try
    {
        settingsStore.update([this](AppSettings& saved)
        {
            saved.showLoops = filter.showLoops;
            saved.showOneShots = filter.showOneShots;
        });
    }
    catch (const std::exception& e)
    { status.setText("Cannot save the structure filters: " + juce::String::fromUTF8(e.what()),
                     juce::dontSendNotification); }
}
void MainComponent::updateTypeFilterChip()
{
    typeFilter.setButtonText(filter.types.empty() ? juce::String("Type")
                                                  : "Type: " + filter.types.front());
    typeFilter.setToggleState(! filter.types.empty(), juce::dontSendNotification);
    // The chip measures its own text, so a longer "Type: Percussion" needs the bar laid out again.
    resized();
}
void MainComponent::clearTypeFilter()
{
    if (filter.types.empty()) return;
    filter.types.clear();
    updateTypeFilterChip();
    pageOffset = 0;
    reloadLibrary();
}
void MainComponent::showTypeFilterMenu()
{
    juce::PopupMenu menu;
    menu.setLookAndFeel(&look);
    menu.addItem(1, "All types", true, filter.types.empty());
    juce::StringArray entries;
    for (const auto& type : standardSampleTypeNames()) entries.addIfNotAlreadyThere(type);
    if (readRepo != nullptr)
    {
        try { for (const auto& type : readRepo->customTypes()) entries.addIfNotAlreadyThere(type); }
        catch (const std::exception&) {}
    }
    if (! entries.isEmpty()) menu.addSeparator();
    for (int i = 0; i < entries.size(); ++i)
    {
        const bool active = filter.types.size() == 1
            && filter.types.front().equalsIgnoreCase(entries[(size_t) i]);
        menu.addItem(100 + i, entries[(size_t) i], true, active);
    }
    juce::Component::SafePointer<MainComponent> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&typeFilter),
        [safe, entries](int result)
        {
            if (safe == nullptr || result <= 0) return;
            if (result == 1) { safe->clearTypeFilter(); return; }
            const int index = result - 100;
            if (index < 0 || index >= entries.size()) return;
            safe->filter.types = { entries[(size_t) index] };
            safe->updateTypeFilterChip();
            safe->pageOffset = 0;
            safe->reloadLibrary();
        });
}
bool MainComponent::isFolderVisible(const juce::String& path) const
{
    if (readRepo == nullptr) return false;
    if (filter.showIgnored) return true;
    try { return ! isPathIgnored(path, readRepo->ignoredPaths()); }
    catch (...) { return true; }
}
void MainComponent::ensureSelectedFolderVisible()
{
    if (selectedFolder.isEmpty() || isFolderVisible(selectedFolder)) return;
    selectedFolder.clear();
    filter.folder.clear();
    folders.clearSelection();
    appSettings.lastFolder.clear();
    // The embedded window's selection belongs to the DAW project; the application's own last
    // folder in the shared settings file must survive.
    if (embeddedUi) return;
    try { settingsStore.update([](AppSettings& saved) { saved.lastFolder.clear(); }); } catch (...) {}
}
void MainComponent::setTagFilters(std::vector<int64_t> include, std::vector<int64_t> exclude, bool matchAny)
{
    filter.includeTags = std::move(include);
    filter.excludeTags = std::move(exclude);
    filter.matchAnyTag = matchAny;
    pageOffset = 0;
    reloadLibrary();
}
void MainComponent::chooseFolder()
{
    if (embeddedUi)
    {
        status.setText("Adding folders runs in the SONICATOR app.", juce::dontSendNotification);
        return;
    }
    chooser = std::make_unique<juce::FileChooser>("Choose a sample folder");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [safe = juce::Component::SafePointer<MainComponent>(this)](const juce::FileChooser& fc)
        {
            auto folder = fc.getResult();
            if (safe && folder.isDirectory()) safe->beginImport(folder);
        });
}
void MainComponent::beginImport(const juce::File& folder)
{
    if (embeddedUi)
    {
        status.setText("Adding folders runs in the SONICATOR app.", juce::dontSendNotification);
        return;
    }
    if (isSplitRunning())
    {
        status.setText("Wait for Split to finish.", juce::dontSendNotification);
        return;
    }
    if (indexer == nullptr) return;
    if (indexer->progress().running)
    {
        status.setText("Wait for the library scan to finish before adding a folder.",
                       juce::dontSendNotification);
        return;
    }
    try
    {
        // Adding a folder is an explicit request: a previous Remove or Ignore of this folder or
        // any ancestor no longer keeps the chosen subtree out of the scan.
        writeRepo->unignoreCoveringFolders(folder.getFullPathName());
        writeRepo->addRoot(folder.getFullPathName()); requestFolderTreeRefresh(); refreshFavorites();
        if (indexer->start({folder.getFullPathName()})) { wasRunning = true; reloadLibrary(); }
    }
    catch (const std::exception& e) { status.setText(juce::String::fromUTF8(e.what()),juce::dontSendNotification); }
}
bool MainComponent::removeFolderFromLibrary(const juce::String& path)
{
    if (readRepo == nullptr || writeRepo == nullptr) return false;
    const auto target = canonicalFolderPath(path);
    if (target.isEmpty()) return false;
    if (folderGenreWork.valid() || isSplitRunning() || (indexer != nullptr && indexer->progress().running)
        || (smart != nullptr && smart->progress().running)
        || (tagging != nullptr && tagging->progress().running)
        || (autoTag != nullptr && autoTag->progress().running))
    {
        status.setText("Wait for background work to finish before removing a folder.", juce::dontSendNotification);
        return false;
    }
    try
    {
        const auto removal = writeRepo->removeFolder(target);
        auto favorites = appSettings.favoriteFolders;
        bool favoritesChanged = false;
        for (int i = favorites.size(); --i >= 0;)
        {
            const auto favorite = canonicalFolderPath(favorites[i]);
            if (favorite == target || juce::File(favorite).isAChildOf(juce::File(target)))
            { favorites.remove(i); favoritesChanged = true; }
        }
        if (favoritesChanged)
        {
            settingsStore.update([&favorites](AppSettings& saved) { saved.favoriteFolders = favorites; });
            appSettings.favoriteFolders = favorites;
        }
        auto aliases = appSettings.folderAliases;
        bool aliasesChanged = false;
        const auto aliasKeys = aliases.getAllKeys();
        for (int i = aliases.size(); --i >= 0;)
        {
            const auto aliasPath = canonicalFolderPath(aliasKeys[i]);
            if (aliasPath != target && ! juce::File(aliasPath).isAChildOf(juce::File(target))) continue;
            aliases.remove(i);
            aliasesChanged = true;
        }
        if (aliasesChanged)
        {
            settingsStore.update([&aliases](AppSettings& saved) { saved.folderAliases = aliases; });
            appSettings.folderAliases = aliases;
            folders.setFolderAliases(aliases);
            this->favorites.setFolderAliases(aliases);
        }
        if (! selectedFolder.isEmpty()
            && (canonicalFolderPath(selectedFolder) == target
                || juce::File(selectedFolder).isAChildOf(juce::File(target))))
        {
            selectedFolder.clear();
            filter.folder.clear();
        }
        if (! appSettings.lastFolder.isEmpty()
            && (canonicalFolderPath(appSettings.lastFolder) == target
                || juce::File(appSettings.lastFolder).isAChildOf(juce::File(target))))
        {
            appSettings.lastFolder.clear();
            settingsStore.update([](AppSettings& saved) { saved.lastFolder.clear(); });
        }
        if (! sessionSamplePath.isEmpty()
            && (sessionSamplePath == target || juce::File(sessionSamplePath).isAChildOf(juce::File(target))))
        {
            selectedSampleId = 0;
            sessionSamplePath.clear();
            markSessionStateDirty();
        }
        writeRepo->refreshDuplicates();
        reloadLibrary();
        requestFolderTreeRefresh();
        refreshFavorites();
        ensureSelectedFolderVisible();
        status.setText("Removed " + juce::String(removal.rowsRemoved) + " catalogued file(s) from " + target
            + ". Files on disk are unchanged."
            + (removal.wasRoot ? " The folder is no longer a library root."
                               : juce::String())
            + " It stays out of scanning until you add it again.",
            juce::dontSendNotification);
        return true;
    }
    catch (const std::exception& e)
    {
        status.setText("Could not remove the folder: " + juce::String::fromUTF8(e.what()), juce::dontSendNotification);
        return false;
    }
}
void MainComponent::requestScan(bool reanalyzeAll)
{
    if (embeddedUi)
    {
        status.setText("Scanning runs in the SONICATOR app.", juce::dontSendNotification);
        return;
    }
    if (isSplitRunning())
    {
        status.setText("Wait for Split to finish.", juce::dontSendNotification);
        return;
    }
    if (! indexer || ! readRepo) return;
    if (indexer->progress().running)
    {
        status.setText("Wait for the library scan to finish before starting another scan.",
                       juce::dontSendNotification);
        return;
    }
    juce::StringArray roots;
    try { roots = readRepo->roots(); }
    catch (const std::exception& e) { status.setText(juce::String::fromUTF8(e.what()),juce::dontSendNotification); return; }
    if (roots.isEmpty()) { status.setText("Add a folder first.",juce::dontSendNotification); return; }
    const auto safe = juce::Component::SafePointer<MainComponent>(this);
    if (! reanalyzeAll)
    {
        dialogs::showAlert(*this, "Rescan library",
            "Walk every registered folder and analyze files that are new, changed or previously failed. "
            "Unchanged analyzed files are skipped.\n\nA large library can take a long time.",
            { { "Rescan", 1 }, { "Cancel", 0 } },
            [safe](int choice) { if (safe != nullptr && choice == 1) safe->scanRoots(false); },
            juce::MessageBoxIconType::QuestionIcon);
        return;
    }
    dialogs::showAlert(*this, "Reanalyze all",
        "Recompute the measured features of every file in the catalog, including files that have not changed. "
        "\n\nOn a large library this can take a long time. Completed results are saved if you cancel.",
        { { "Reanalyze", 1 }, { "Cancel", 0 } },
        [safe](int choice) { if (safe != nullptr && choice == 1) safe->scanRoots(true); },
        juce::MessageBoxIconType::WarningIcon);
}
void MainComponent::scanRoots(bool force, bool newOnly)
{
    if (embeddedUi)
    {
        status.setText("Scanning runs in the SONICATOR app.", juce::dontSendNotification);
        return;
    }
    if (isSplitRunning())
    {
        status.setText("Wait for Split to finish.", juce::dontSendNotification);
        return;
    }
    if (! indexer) return;
    if (indexer->progress().running)
    {
        status.setText("Wait for the library scan to finish before starting another scan.",
                       juce::dontSendNotification);
        return;
    }
    try
    {
        auto roots = readRepo->roots();
        if (roots.isEmpty()) { status.setText("Add a folder first.",juce::dontSendNotification); return; }
        const bool started = newOnly ? indexer->startNewOnly(roots) : indexer->start(roots,force);
        if (started) { wasRunning = true; reloadLibrary(); }
    }
    catch (const std::exception& e) { status.setText(juce::String::fromUTF8(e.what()),juce::dontSendNotification); }
}
juce::File MainComponent::backupCatalog()
{
    auto* repo = writeRepo != nullptr ? writeRepo.get() : readRepo.get();
    if (repo == nullptr) throw std::runtime_error("Database is not open");
    auto folder = dbPath.getParentDirectory().getChildFile("backups");
    const auto stamp = juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S");
    auto target = folder.getChildFile("sonicator-" + stamp + ".sqlite").getNonexistentSibling();
    repo->backupTo(target);
    status.setText("Backup saved: " + target.getFullPathName(),juce::dontSendNotification);
    status.setTooltip(target.getFullPathName());
    return target;
}
juce::StringArray MainComponent::missingRoots() const
{
    juce::StringArray out;
    if (! readRepo) return out;
    try
    {
        for (const auto& root : readRepo->roots())
            if (! juce::File(root).isDirectory()) out.add(root);
    }
    catch (const std::exception&) {}
    return out;
}
void MainComponent::showRelocateDialog()
{
    if (indexer && indexer->progress().running)
    {
        status.setText("Stop indexing before relocating folders.",juce::dontSendNotification);
        return;
    }
    if (! readRepo || ! writeRepo) return;
    if (relocateDialog != nullptr) { relocateDialog->toFront(true); return; }

    const auto missing = missingRoots();
    if (missing.isEmpty())
    {
        dialogs::showAlert(*this, "Relocate folders",
            "All catalog folders are available on this computer.",
            { { "OK", 1 } }, [](int) {}, juce::MessageBoxIconType::InfoIcon);
        return;
    }

    std::vector<RelocateRootInfo> roots;
    roots.reserve((size_t) missing.size());
    for (const auto& root : missing)
    {
        int files = 0;
        try { files = readRepo->countUnderRoot(root); } catch (const std::exception&) {}
        roots.push_back({ root, files });
    }

    auto* panel = new RelocateFoldersPanel(std::move(roots));
    panel->setLookAndFeel(&look);
    panel->onProbe = [read = readRepo.get()](const juce::String& oldRoot, const juce::String& newRoot)
    {
        RelocationProbe probe;
        constexpr int limit = 200;
        int64_t lastId = 0;
        try
        {
            for (;;)
            {
                auto rows = read->pathsAfterId(lastId, 5000);
                if (rows.empty()) break;
                lastId = rows.back().id;
                for (const auto& row : rows)
                {
                    if (row.containerPath.isNotEmpty()) continue; // synthetic path, not a file
                    if (! pathrelocation::isUnder(row.path, oldRoot)) continue;
                    ++probe.sampled;
                    if (juce::File(pathrelocation::remap(row.path, oldRoot, newRoot)).existsAsFile())
                        ++probe.found;
                    if (probe.sampled >= limit) break;
                }
                if (probe.sampled >= limit) break;
            }
        }
        catch (const std::exception&) {}
        return probe;
    };
    panel->onApply = [this](const std::vector<RootRelocation>& relocations) -> juce::String
    {
        if (indexer && indexer->progress().running)
            return "Stop indexing before relocating folders.";
        if (! writeRepo || ! readRepo) return "The catalog is not open.";
        RelocationResult result;
        try
        {
            const auto stamp = juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S");
            auto backup = dbPath.getSiblingFile(dbPath.getFileName() + ".relocate-" + stamp + ".sqlite")
                              .getNonexistentSibling();
            writeRepo->backupTo(backup);
            result = writeRepo->relocateRoots(relocations);

            // The transaction has committed. A failure from here on must not be reported as a
            // failed relocation, because every stored path is already relocated.
            juce::String refreshWarning;
            try { writeRepo->refreshDuplicates(); }
            catch (const std::exception& e) { refreshWarning = juce::String::fromUTF8(e.what()); }
            selectedFolder.clear(); selectedSampleId = 0; pageOffset = 0;
            requestFolderTreeRefresh();
            refreshFavorites();
            reloadLibrary();

            juce::String message = "Relocated " + juce::String(result.movedFiles) + " file(s) in "
                + juce::String((int) relocations.size()) + " folder(s).";
            if (result.conflicts > 0)
                message += "  |  " + juce::String(result.conflicts) + " existing path collision(s) merged.";
            message += "  |  Backup: " + backup.getFullPathName();
            if (refreshWarning.isNotEmpty())
                message += "  |  Catalog refresh warning: " + refreshWarning;
            status.setText(message,juce::dontSendNotification);
            status.setTooltip(backup.getFullPathName());
            return {};
        }
        catch (const std::exception& e)
        {
            return "Relocation failed: " + juce::String::fromUTF8(e.what());
        }
    };

    auto* window = new RelocateDialogWindow();
    window->setContentOwned(panel, true);
    window->setLookAndFeel(&look);
    window->setUsingNativeTitleBar(false);
    window->setResizable(false, false);
    window->centreAroundComponent(this, window->getWidth(), window->getHeight());
    relocateDialog = window;
    window->enterModalState(true, nullptr, true);
}
bool MainComponent::refreshIfCatalogChanged()
{
    if (! readRepo) return false;
    if (pageSize == 0 && indexer != nullptr && indexer->progress().running) return false;
    try { if (readRepo->count() == totalCatalogCount) return false; }
    catch (const std::exception&) { return false; }
    reloadLibrary();
    return true;
}
void MainComponent::reloadLibrary()
{
    if (! readRepo) return;
    // A catalog reload can change which tags exist or which rows are visible; never leave a stale
    // ring over the table.
    if (roundTagMenu != nullptr) roundTagMenu->close();
    if (discBrowser) discBrowser->setHideEmpty(filter.hideEmpty);
    try
    {
        ensureSelectedFolderVisible();
        totalCatalogCount = readRepo->count(filter.showIgnored);
        library.setIgnoredPaths(readRepo->ignoredPaths());
        filter.folder = selectedFolder;
        int shown = 0;
        if (pageSize == 0)
        {
            auto ids = readRepo->queryIds(filter);
            catalogCount = (int) ids.size();
            shown = catalogCount;
            library.setVirtualSamples(std::move(ids), [this](const std::vector<int64_t>& pageIds)
            {
                FileListPanel::LoadedRows loaded;
                if (readRepo != nullptr)
                {
                    loaded.records = readRepo->recordsByIds(pageIds);
                    loaded.tags = readRepo->tagsForSamples(pageIds);
                    loaded.duplicates = readRepo->duplicateCounts(pageIds);
                    loaded.smart = readRepo->smartDisplays(pageIds);
                    loaded.stereoPairs = readRepo->stereoPairPartners(pageIds);
                }
                return loaded;
            });
            countLabel.setText(juce::String(catalogCount) + (filter.hasTextOrTagFilter() ? " matching" : " samples")
                + "  |  all on one page",juce::dontSendNotification);
            previousPage.setEnabled(false); nextPage.setEnabled(false);
        }
        else
        {
            auto page = readRepo->query(filter, pageSize, pageOffset);
            if (pageOffset > 0 && pageOffset >= page.total)
            {
                pageOffset = page.total > 0 ? ((page.total - 1) / pageSize) * pageSize : 0;
                page = readRepo->query(filter, pageSize, pageOffset);
            }
            catalogCount = page.total;
            shown = (int) page.samples.size();
            std::vector<int64_t> ids;
            ids.reserve(page.samples.size());
            for (const auto& sample : page.samples) ids.push_back(sample.id);
            library.setSamples(std::move(page.samples), true);
            library.setRowTags(readRepo->tagsForSamples(ids));
            library.setDuplicateCounts(readRepo->duplicateCounts(ids));
            library.setSmartDisplays(readRepo->smartDisplays(ids));
            library.setStereoPairs(readRepo->stereoPairPartners(ids));
            juce::String text = juce::String(catalogCount) + (filter.hasTextOrTagFilter() ? " matching" : " samples")
                + "  |  " + juce::String(shown) + " on page " + juce::String(pageOffset/pageSize+1);
            if (filter.matchAnyTag && ! filter.includeTags.empty()) text += "  |  any tag";
            countLabel.setText(text,juce::dontSendNotification);
            previousPage.setEnabled(pageOffset > 0); nextPage.setEnabled(pageOffset+pageSize < catalogCount);
        }
        allTags = readRepo->tags();
        updateTagControls();
        rootsLabel.setText(readRepo->roots().isEmpty() ? "No sample folders yet.\n\nOriginal files stay untouched."
            : "FOLDERS\n\n" + readRepo->roots().joinIntoString("\n\n"),juce::dontSendNotification);
        updateSelectedTags();
        if (selectedSampleId > 0)
        {
            if (auto selected = readRepo->findById(selectedSampleId)) updateSelectionStatus(*selected);
        }
        else selectionStatus.setText("No sample selected",juce::dontSendNotification);
        (void) shown;
        if (cloudVisible)
            updateCloudMatches();
    }
    catch (const std::exception& e) { status.setText(juce::String::fromUTF8(e.what()),juce::dontSendNotification); }
}
void MainComponent::updateTagControls()
{
    const auto active = (int) (filter.includeTags.size() + filter.excludeTags.size());
    tagFilter.setButtonText(active > 0 ? "Tags (" + juce::String(active) + ")" : "Tags");
    tagFilter.setToggleState(active > 0 || filter.matchAnyTag, juce::dontSendNotification);
    starredButton.setToggleState(filter.starredOnly,juce::dontSendNotification);
    hideEmpty.setToggleState(filter.hideEmpty,juce::dontSendNotification);
    showLoops.setToggleState(filter.showLoops, juce::dontSendNotification);
    showOneShots.setToggleState(filter.showOneShots, juce::dontSendNotification);
    ignoreSymlinks.setToggleState(filter.ignoreSymlinks,juce::dontSendNotification);
    clearFilters.setEnabled(filter.hasTextOrTagFilter());
    // Chips measure their own text, and "Tags (2)" is wider than "Tags".
    resized();
}
void MainComponent::updateSelectionStatus(const SampleRecord& r)
{
    juce::String text = r.filePath;
    if (readRepo != nullptr)
    {
        SmartDisplay d;
        try { d = readRepo->smartDisplays({ r.id })[r.id]; } catch (...) {}
        auto field = [&d](int column)
        {
            juce::String value;
            if (column == 13)
            {
                value = d.structure.isNotEmpty()
                            ? d.structure
                            : (d.pipelineState == SmartPipelineState::NotAnalyzed ? "--" : "?");
                if (d.structureOverride == OverrideState::Value) value += " (manual)";
                else if (d.structureOverride == OverrideState::Unknown) value = "manual: unknown";
                else if (d.structureOverride == OverrideState::NotApplicable) value = "manual: n/a";
                if (d.structureState == SmartState::Conflicting) value += " (conflict)";
                else if (d.structureState == SmartState::Ambiguous) value += " (ambiguous)";
            }
            else if (column == 10)
            {
                value = d.type.isNotEmpty() ? d.type : (d.pipelineState == SmartPipelineState::NotAnalyzed ? "--" : "?");
                if (d.typeOverride == OverrideState::Value) value += " (manual)";
                else if (d.typeOverride == OverrideState::Unknown) value = "manual: unknown";
                else if (d.typeOverride == OverrideState::NotApplicable) value = "manual: n/a";
                if (d.typeState == SmartState::Conflicting) value += " (conflict)";
                else if (d.typeState == SmartState::Ambiguous) value += " (ambiguous)";
            }
            else if (column == 11)
            {
                value = d.hasBpm ? (std::abs(d.bpm - std::round(d.bpm)) < 0.05
                                        ? juce::String((int) std::round(d.bpm))
                                        : juce::String(d.bpm, 1))
                                 : (d.pipelineState == SmartPipelineState::NotAnalyzed ? "--" : "?");
                if (d.bpmOverride == OverrideState::Value) value += " (manual)";
                else if (d.bpmOverride == OverrideState::Unknown) value = "manual: unknown";
                else if (d.bpmOverride == OverrideState::NotApplicable) value = "manual: n/a";
                if (d.bpmState == SmartState::Conflicting) value += " (conflict)";
                else if (d.bpmState == SmartState::Ambiguous) value += " (ambiguous)";
            }
            else
            {
                value = d.key.isNotEmpty() ? d.key : (d.pipelineState == SmartPipelineState::NotAnalyzed ? "--" : "?");
                if (d.keyOverride == OverrideState::Value) value += " (manual)";
                else if (d.keyOverride == OverrideState::Unknown) value = "manual: unknown";
                else if (d.keyOverride == OverrideState::NotApplicable) value = "manual: n/a";
                if (d.keyState == SmartState::Conflicting) value += " (conflict)";
                else if (d.keyState == SmartState::Ambiguous) value += " (ambiguous)";
            }
            if (d.pipelineState == SmartPipelineState::Stale) value += " (stale)";
            if (d.pipelineState == SmartPipelineState::Error) value += " (error)";
            return value;
        };
        text += "   |   Type: " + field(10) + "   Structure: " + field(13)
              + "   BPM: " + field(11) + "   Key: " + field(12)
              + "   Pitch: " + pitchDisplayText(d);
        if (const auto tags = readRepo->tagsForSample(r.id); ! tags.empty())
        {
            juce::StringArray names;
            for (const auto& tag : tags) names.add(tag.name);
            text += "   |   Tags: " + names.joinIntoString(", ");
        }
    }
    text += "   |   " + r.format
          + (r.sampleRate > 0 ? "  " + juce::String(r.sampleRate / 1000.0, 1) + " kHz" : juce::String())
          + (r.numChannels > 0 ? "  " + juce::String(r.numChannels) + " ch" : juce::String())
          + (r.bitsPerSample > 0 ? "  " + juce::String(r.bitsPerSample) + "-bit" : juce::String())
          + (r.duration > 0 ? "  " + juce::String(r.duration, 2) + " s" : juce::String());
    if (r.starred) text += "   |   Starred";
    if (r.missing) text += "   |   FILE MISSING";
    if (r.error.isNotEmpty()) text += "   |   " + r.error;
    selectionStatus.setText(text, juce::dontSendNotification);
    selectionStatus.setTooltip(r.filePath);
}
void MainComponent::updateSelectedTags()
{
    if (selectedSampleId <= 0 || ! readRepo)
    {
        selectedTags.setTags({});
        editTags.setEnabled(false);
        return;
    }
    selectedTags.setTags(readRepo->tagsForSample(selectedSampleId));
    editTags.setEnabled(true);
    resized();
}
void MainComponent::removeTagFromSamples(const std::vector<int64_t>& sampleIds, int64_t tagId)
{
    if (writeRepo == nullptr || sampleIds.empty() || tagId <= 0) return;
    const auto visible = writeRepo->visibleIds(sampleIds);
    if (visible.empty()) return;
    writeRepo->setSampleTag(visible, tagId, false);
    triggerAdaptiveLearning(visible);
    status.setText("Tag removed from " + juce::String(visible.size()) + " sample(s)",
                   juce::dontSendNotification);
    reloadLibrary();
}
void MainComponent::startCatalogSync(const juce::File& file, int64_t sampleId, bool isCopy)
{
    if (writeRepo == nullptr) return;
    status.setText(isCopy ? "Adding the copy to the catalog..." : "Updating the catalog...",
                   juce::dontSendNotification);
    enqueueCatalogSync(file, sampleId, isCopy);
}

void MainComponent::enqueueCatalogSync(const juce::File& file, int64_t sampleId, bool isCopy)
{
    auto* repo = writeRepo.get();
    if (repo == nullptr) return;
    {
        std::lock_guard lock(catalogSyncMutex);
        if (catalogSyncStop) return;
        catalogSyncRequests.push_back({ file, sampleId, isCopy });
        // A running drain loop takes the new request on its next round; nothing to start.
        if (catalogSyncQueued) return;
        catalogSyncQueued = true;
    }
    // The previous worker has already drained and is only posting its final message, so this join
    // is short. Long reanalysis never happens while the message thread waits here.
    if (catalogSyncWorker && catalogSyncWorker->joinable()) catalogSyncWorker->join();
    auto safe = juce::Component::SafePointer<MainComponent>(this);
    catalogSyncWorker = std::make_unique<std::thread>([this, repo, safe]()
    {
        runCatalogSyncQueue(*repo, safe);
    });
}

void MainComponent::runCatalogSyncQueue(SampleRepository& repo,
                                        juce::Component::SafePointer<MainComponent> safe)
{
    bool reload = false;
    juce::StringArray messages;
    for (;;)
    {
        std::vector<CatalogSyncRequest> batch;
        {
            std::lock_guard lock(catalogSyncMutex);
            if (catalogSyncStop)
            {
                catalogSyncRequests.clear();
                catalogSyncQueued = false;
                break;
            }
            batch.swap(catalogSyncRequests);
            if (batch.empty())
            {
                catalogSyncQueued = false;
                break;
            }
        }
        for (const auto& request : batch)
        {
            try
            {
                const auto result = request.isCopy
                    ? addCatalogCopy(repo, request.file, request.sampleId)
                    : refreshCatalogFile(repo, request.file);
                if (result.ok)
                {
                    messages.add((request.isCopy ? "Added to the catalog: " : "Catalog updated: ")
                                 + request.file.getFileName());
                    reload = true;
                }
                else
                    messages.add(result.error);
            }
            catch (const std::exception& error)
            {
                messages.add(juce::String::fromUTF8(error.what()));
            }
        }
    }
    juce::MessageManager::callAsync([safe, messages, reload]()
    {
        if (safe == nullptr) return;
        if (! messages.isEmpty())
        {
            auto text = messages.joinIntoString("  |  ");
            if (messages.size() > 3)
                text = messages[0] + "  |  " + messages[1] + "  |  " + messages[2]
                    + "  (+" + juce::String(messages.size() - 3) + " more)";
            safe->status.setText(text, juce::dontSendNotification);
            safe->status.setTooltip(messages.joinIntoString("\n"));
        }
        if (reload) safe->reloadLibrary();
    });
}

void MainComponent::setBpmForRows(const std::vector<int64_t>& ids, double bpm)
{
    if (writeRepo == nullptr || ids.empty()) return;
    bpm = juce::jlimit(20.0, 300.0, bpm);
    SmartOverride value;
    value.numericValue = bpm;
    try
    {
        for (const auto id : ids) writeRepo->setSmartOverride(id, SmartField::Bpm, value);
        writeRepo->touchFeedback(ids);
    }
    catch (const std::exception& error)
    {
        status.setText(juce::String::fromUTF8(error.what()), juce::dontSendNotification);
        return;
    }
    reloadLibrary();
    status.setText("BPM " + juce::String(bpm, bpm == std::round(bpm) ? 0 : 1)
        + " set for " + juce::String((int) ids.size()) + " sample(s)", juce::dontSendNotification);
}

void MainComponent::clearBpmForRows(const std::vector<int64_t>& ids)
{
    if (writeRepo == nullptr || ids.empty()) return;
    int cleared = 0;
    try
    {
        for (const auto id : ids)
        {
            const auto displays = writeRepo->smartDisplays({ id });
            const auto found = displays.find(id);
            if (found == displays.end() || found->second.bpmOverride != OverrideState::Value) continue;
            writeRepo->clearSmartOverride(id, SmartField::Bpm);
            ++cleared;
        }
        if (cleared == 0) return;
        writeRepo->touchFeedback(ids);
    }
    catch (const std::exception& error)
    {
        status.setText(juce::String::fromUTF8(error.what()), juce::dontSendNotification);
        return;
    }
    reloadLibrary();
    status.setText("Manual BPM cleared for " + juce::String(cleared) + " sample(s)",
                   juce::dontSendNotification);
}

void MainComponent::prepareForQuit(std::function<void(bool)> done)
{
    preview.withUnsavedEditsConfirmed(std::move(done));
}

void MainComponent::selectSample(const SampleRecord& r)
{
    if (preview.hasUnsavedEdits())
    {
        pendingSample = r;
        const auto editingId = selectedSampleId;
        auto safe = juce::Component::SafePointer<MainComponent>(this);
        preview.withUnsavedEditsConfirmed([safe, editingId](bool proceed)
        {
            if (safe == nullptr) return;
            if (! proceed)
            {
                if (editingId != 0) safe->library.selectSampleById(editingId);
                safe->pendingSample.reset();
                return;
            }
            auto next = safe->pendingSample;
            safe->pendingSample.reset();
            if (next.has_value()) safe->loadSelectedSample(*next);
        });
        return;
    }
    loadSelectedSample(r);
}
void MainComponent::refreshAnalysis(AnalysisContent content)
{
    keyBpm.setValues(content.key, content.bpm);
    analysisCard.setContent(std::move(content));
    resized();   // the card's height inside the viewport follows its content
}

void MainComponent::showAnalysisPlaceholder()
{
    keyBpm.setValues({}, {});
    analysisCard.showPlaceholder("Select a sample to inspect its measured audio features.");
    resized();
}

AnalysisContent MainComponent::analysisFor(const SampleRecord& r) const
{
    auto dbfs = [](double v) { return v > 0 ? juce::String(juce::Decibels::gainToDecibels(v),1) + " dBFS" : "-inf dBFS"; };
    AnalysisContent card;
    card.title = r.fileName;
    card.meta = r.format + " · " + juce::String(r.numChannels) + " ch · "
        + (r.sampleRate > 0 ? juce::String(r.sampleRate / 1000, 1) + " kHz" : juce::String("unknown rate"))
        + " · " + (r.bitsPerSample > 0 ? juce::String(r.bitsPerSample) + " bit" : juce::String("unknown depth"));
    card.stats.push_back({ formatLengthText(r.duration), "Length" });
    card.stats.push_back({ r.sampleRate > 0 ? juce::String(r.sampleRate/1000,1) + " kHz" : juce::String("N/A"), "Rate" });
    card.stats.push_back({ r.bitsPerSample > 0 ? juce::String(r.bitsPerSample) : juce::String("N/A"), "Bit depth" });
    card.stats.push_back({ dbfs(r.peakLevel), "Peak" });
    card.stats.push_back({ dbfs(r.rmsLevel), "RMS" });
    card.stats.push_back({ juce::String(r.spectralCentroid,0) + " Hz", "Centroid" });
    card.stats.push_back({ juce::String(r.spectralRolloff,0) + " Hz", "Rolloff" });
    card.stats.push_back({ juce::String(r.spectralFlatness,4), "Flatness" });
    if (r.isDiscEntry())
    {
        card.notes.push_back("Inside image " + juce::File(r.containerPath).getFileName());
        card.notes.push_back("Entry " + r.discPath);
        card.notes.push_back("Decoded on demand; the image is never extracted.");
    }
    if (readRepo != nullptr)
    {
        // Where a genre came from decides how much a person should trust it, so the card names
        // the source next to the value.
        const auto displays = readRepo->smartDisplays({ r.id });
        const auto found = displays.find(r.id);
        // Key and BPM are the two values a person looks for first: they go big above the tiles.
        if (found != displays.end())
        {
            const auto& d = found->second;
            card.key = d.key.isNotEmpty() ? d.key : juce::String("--");
            card.bpm = d.hasBpm ? juce::String(d.bpm, d.bpm >= 100.0 ? 0 : 1) : juce::String("--");
        }
        else
        {
            card.key = "--";
            card.bpm = "--";
        }
        if (found != displays.end() && found->second.genre.isNotEmpty())
        {
            const auto& d = found->second;
            const auto source = d.genreOverride == OverrideState::Value ? juce::String("manual")
                : d.genreState == SmartState::Ambiguous ? juce::String("ambiguous")
                : juce::String("automatic");
            card.stats.push_back({ d.genre, "Genre · " + source });
        }
        else if (found != displays.end() && found->second.genreOverride == OverrideState::Unknown)
            card.stats.push_back({ "unknown", "Genre · manual" });
    }
    if (r.missing) card.notes.push_back("File is missing");
    if (r.error.isNotEmpty()) card.notes.push_back(r.error);
    if (readRepo != nullptr)
    {
        if (const auto duplicate = readRepo->duplicateInfo(r.id))
        {
            card.stats.push_back({ juce::String::charToString(0x00d7) + juce::String(duplicate->groupSize),
                                   "Duplicates" });
            if (duplicate->representativeId != r.id)
            {
                card.notes.push_back("Shown copy is not the catalog representative.");
                card.notes.push_back("Representative: " + duplicate->representativePath);
            }
            for (const auto& copy : readRepo->duplicateMembers(r.id))
                if (copy.id != r.id)
                    card.notes.push_back(copy.filePath + (copy.missing ? juce::String("  (missing)") : juce::String()));
        }
    }
    card.tooltip = r.filePath;
    return card;
}

void MainComponent::loadSelectedSample(const SampleRecord& r)
{
    if (cloudPanel != nullptr && cloudVisible)
    {
        cloudPanel->setSelectedSample(r.id);
        cloudHoverEnd();
    }
    const int selectedRows = library.getSelectedRowCount();
    if (selectedRows > 1)
    {
        // A batch is a library operation, not an audition: no single waveform or sound should
        // look as if it belonged to the whole selection.
        selectedSampleId = 0;
        preview.editingSampleId = 0;
        sessionSamplePath.clear();
        markSessionStateDirty();
        preview.clearSource();
        // The selection still has a useful answer: every tag on any selected file, once.
        std::vector<int64_t> ids;
        for (const auto& sample : library.getSelectedSamples()) ids.push_back(sample.id);
        std::map<int64_t, Tag> unique;
        if (readRepo != nullptr)
            for (const auto& [sampleId, tags] : readRepo->tagsForSamples(ids))
                for (const auto& tag : tags) unique[tag.id] = tag;
        std::vector<Tag> unionTags;
        for (const auto& [id, tag] : unique) { juce::ignoreUnused(id); unionTags.push_back(tag); }
        std::sort(unionTags.begin(), unionTags.end(),
                  [](const Tag& a, const Tag& b) { return a.name.compareIgnoreCase(b.name) < 0; });
        selectedTags.setTags(unionTags);
        editTags.setEnabled(false); // editing a batch would need its own menu; display only
        AnalysisContent card;
        card.title = juce::String(ids.size()) + " samples selected";
        card.meta = juce::String(unionTags.size()) + " unique tags on the selection";
        card.notes = { unionTags.empty()
                           ? juce::String("Tags apply to the whole selection.")
                           : juce::String("Alt+click a tag to remove it from every selected file.") };
        refreshAnalysis(std::move(card));
        selectionStatus.setText(juce::String(selectedRows) + " samples selected",
                                juce::dontSendNotification);
        selectionStatus.setTooltip({});
        return;
    }
    selectedSampleId = r.id;
    preview.editingSampleId = r.id;
    sessionSamplePath = r.filePath;
    markSessionStateDirty();
    refreshAnalysis(analysisFor(r));
    updateSelectionStatus(r);
    updateSelectedTags();
    if (r.isDiscEntry())
    {
        ensureDiscBrowser();
        DiscEntryId parent, entry;
        parent.value = (uint64_t) r.discParent.getLargeIntValue();
        entry.value = (uint64_t) r.discEntry.getLargeIntValue();
        status.setText("Loading " + r.fileName + " from "
            + juce::File(r.containerPath).getFileName() + "...", juce::dontSendNotification);
        discBrowser->previewEntry(juce::File(r.containerPath), parent, entry, r.fileName);
        return;
    }
    if (isDiscImageFormat(r.format))
    {
        // A metadata-only image never leaves the previous source audible under a new label.
        preview.clearDiscSource();
        status.setText(r.error.isNotEmpty() ? r.error
            : "Double-click the image to browse partitions, folders and samples", juce::dontSendNotification);
        return;
    }
    if (! preview.loadFile(juce::File(r.filePath))) status.setText(engine.error(),juce::dontSendNotification);
}
void MainComponent::clearSampleSelection()
{
    auto safe = juce::Component::SafePointer<MainComponent>(this);
    preview.withUnsavedEditsConfirmed([safe](bool proceed)
    {
        if (safe != nullptr && proceed) safe->clearSampleSelectionNow();
    });
}
void MainComponent::clearSampleSelectionNow()
{
    library.selectRows({});
    selectedSampleId = 0;
    preview.editingSampleId = 0;
    sessionSamplePath.clear();
    markSessionStateDirty();
    preview.clearSource();
    updateSelectedTags();
    showAnalysisPlaceholder();
    analysisCard.setTooltip({});
    selectionStatus.setText("No sample selected", juce::dontSendNotification);
    selectionStatus.setTooltip({});
}

bool MainComponent::EnterKeyListener::keyPressed(const juce::KeyPress& key, juce::Component*)
{
    return key.getKeyCode() == juce::KeyPress::returnKey && owner.openSelectedDiscIfAny();
}

bool MainComponent::openSelectedDiscIfAny()
{
    if (discBrowser != nullptr && discBrowser->isVisible()) return false;
    const auto selected = library.getSelectedSamples();
    if (selected.empty()) return false;
    if (! selected[0].isDiscEntry() && ! isDiscImageFormat(selected[0].format)) return false;
    openDiscImage(selected[0]);
    return true;
}

bool MainComponent::keyPressed(const juce::KeyPress& key)
{
    // Every bound command resolves through one router, so a menu hint and the working key can
    // never disagree. Text fields, dialogs and the shortcut recorder are excluded inside it.
    if (dispatchCommandKey(key)) return true;
    // Enter opens the selected container wherever focus is; ordinary rows keep their normal
    // transport keys. This also drives the session-restored row without a mouse click.
    if (key.getKeyCode() == juce::KeyPress::returnKey && openSelectedDiscIfAny()) return true;
    // Every bound key is resolved by the router above; the preview only sees keys the router did
    // not consume, so a removed or rebound shortcut can never fall back to its old default.
    return preview.keyPressed(key);
}

void MainComponent::ensureDiscBrowser()
{
    if (discBrowser) return;
    discBrowser = std::make_unique<DiscBrowserPanel>();
    discBrowser->onSelectionBegin = [this] { engine.stop(); };
    discBrowser->onImageError = [this](const juce::File& image, const juce::String& error)
    {
        folders.setDiscError(image.getFullPathName(), true);
        status.setText(error, juce::dontSendNotification);
    };
    discBrowser->onTreeReady = [this](const juce::File& image, DiscEntryId root, const std::vector<DiscEntry>& entries)
    {
        folders.setDiscFolders(image.getFullPathName(), root, entries);
    };
    discBrowser->onError = [this](const juce::String& message)
    {
        status.setText(message, juce::dontSendNotification);
    };
    discBrowser->onExit = [this]
    {
        if (discBrowser != nullptr) { discBrowser->closeImage(); discBrowser->setVisible(false); }
        library.focusForNavigation();
    };
    discBrowser->onSampleSelected = [this](const juce::File& container, const juce::String& stem,
                                           SampleAudioPtr audio)
    {
        if (! discBrowser || discBrowser->openedImage() != container) return;
        if (! discBrowser->isVisible())
        {
            // A hidden preview belongs to the library selection. Ignore a decode that finished
            // after the user selected another row (ordinary file, image or different entry).
            const auto current = selectedSampleId > 0 && readRepo != nullptr
                ? readRepo->findById(selectedSampleId) : std::nullopt;
            if (! current || ! current->isDiscEntry() || current->containerPath != container.getFullPathName()
                || current->fileName != stem) return;
        }
        if (! preview.loadDiscSample(container, stem, std::move(audio), false, 0, 0, false, true))
            status.setText(engine.error(), juce::dontSendNotification);
        else
            status.setText("Inside " + container.getFileName() + "  ·  " + stem, juce::dontSendNotification);
    };
    addChildComponent(*discBrowser);
}
void MainComponent::openDiscImage(const SampleRecord& r)
{
    auto safe = juce::Component::SafePointer<MainComponent>(this);
    const auto record = r;
    preview.withUnsavedEditsConfirmed([safe, record](bool proceed)
    {
        if (safe != nullptr && proceed) safe->openDiscImageNow(record);
    });
}
void MainComponent::openDiscImageNow(const SampleRecord& r)
{
    if (r.isDiscEntry())
    {
        DiscEntryId parent, entry;
        parent.value = (uint64_t) r.discParent.getLargeIntValue();
        entry.value = (uint64_t) r.discEntry.getLargeIntValue();
        ensureDiscBrowser();
        discBrowser->setBounds(library.getBounds());
        discBrowser->setHideEmpty(filter.hideEmpty);
        discBrowser->setVisible(true);
        discBrowser->toFront(true);
        discBrowser->previewEntry(juce::File(r.containerPath), parent, entry, r.fileName);
        return;
    }
    if (! isDiscImageFormat(r.format)) return;
    if (isIncompatible(r.compatibility))
    {
        status.setText(r.error.isNotEmpty() ? r.error : "This image is not supported", juce::dontSendNotification);
        return;
    }
    ensureDiscBrowser();
    discBrowser->setBounds(library.getBounds());
    discBrowser->setHideEmpty(filter.hideEmpty);
    discBrowser->setVisible(true);
    discBrowser->toFront(true);
    if (discBrowser->canReuseImage(juce::File(r.filePath)))
    {
        discBrowser->refreshFolderTree();
        discBrowser->showDirectory({});
        return;
    }
    discBrowser->open(juce::File(r.filePath));
}
void MainComponent::showTagFilterMenu()
{
    if (! readRepo) return;
    try { allTags = readRepo->tags(); }
    catch (const std::exception& e) { status.setText(juce::String::fromUTF8(e.what()),juce::dontSendNotification); return; }
    juce::PopupMenu menu, excludes, manage;
    menu.setLookAndFeel(&look);
    if (allTags.empty()) menu.addSectionHeader("No tags yet");
    for (int i = 0; i < (int) allTags.size(); ++i)
    {
        const auto& tag = allTags[(size_t) i];
        menu.addItem(1000+i, tag.name + "  (" + juce::String(tag.sampleCount) + ")", true, contains(filter.includeTags, tag.id));
        excludes.addItem(2000+i, tag.name, true, contains(filter.excludeTags, tag.id));
        juce::PopupMenu actions;
        actions.addItem(4000+i*10, "Change colour...");
        actions.addItem(4000+i*10+1, "Rename...");
        if (! tag.builtin) actions.addItem(4000+i*10+2, "Delete");
        manage.addSubMenu(tag.name, actions);
    }
    menu.addSeparator();
    menu.addSubMenu("Exclude tags", excludes);
    menu.addItem(3000, "Match all selected", true, ! filter.matchAnyTag);
    menu.addItem(3001, "Match any selected", true, filter.matchAnyTag);
    menu.addSeparator();
    menu.addItem(3010, "New tag...");
    if (! allTags.empty()) menu.addSubMenu("Manage tags", manage);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&tagFilter).withMaximumNumColumns(1),
        [safe = juce::Component::SafePointer<MainComponent>(this)](int result)
        {
            if (safe == nullptr || result == 0) return;
            auto& f = safe->filter;
            if (result >= 1000 && result < 2000)
            {
                const size_t index = (size_t) (result - 1000);
                if (index >= safe->allTags.size()) return;
                const auto id = safe->allTags[index].id;
                const auto excluded = std::find(f.excludeTags.begin(), f.excludeTags.end(), id);
                if (excluded != f.excludeTags.end()) f.excludeTags.erase(excluded);
                toggle(f.includeTags, id);
            }
            else if (result >= 2000 && result < 3000)
            {
                const size_t index = (size_t) (result - 2000);
                if (index >= safe->allTags.size()) return;
                const auto id = safe->allTags[index].id;
                const auto included = std::find(f.includeTags.begin(), f.includeTags.end(), id);
                if (included != f.includeTags.end()) f.includeTags.erase(included);
                toggle(f.excludeTags, id);
            }
            else if (result == 3000) f.matchAnyTag = false;
            else if (result == 3001) f.matchAnyTag = true;
            else if (result == 3010)
            {
                safe->promptTagName("New tag", {}, [safe](juce::String name)
                {
                    if (safe == nullptr || name.trim().isEmpty() || safe->writeRepo == nullptr) return;
                    try
                    {
                        const auto colour = tagPalette[safe->allTags.size() % std::size(tagPalette)].colour;
                        safe->writeRepo->createTag(name, colour);
                        safe->reloadLibrary();
                    }
                    catch (const std::exception& e) { safe->status.setText(juce::String::fromUTF8(e.what()),juce::dontSendNotification); }
                });
                return;
            }
            else if (result >= 4000)
            {
                const size_t index = (size_t) (result - 4000) / 10;
                const int action = (result - 4000) % 10;
                if (index >= safe->allTags.size()) return;
                const auto tag = safe->allTags[index];
                if (action == 0) { safe->showTagColourMenu(tag.id); return; }
                if (action == 2 && ! tag.builtin)
                {
                    dialogs::showAlert(*safe, "Delete tag",
                        "Remove \"" + tag.name + "\" from every sample?",
                        { { "Delete", 1 }, { "Cancel", 0 } },
                        [safe, id = tag.id](int choice)
                        {
                            if (safe == nullptr || choice != 1) return;
                            try { safe->writeRepo->deleteTag(id); safe->reloadLibrary(); }
                            catch (const std::exception& e) { safe->status.setText(juce::String::fromUTF8(e.what()),juce::dontSendNotification); }
                        }, juce::MessageBoxIconType::WarningIcon);
                    return;
                }
                if (action == 1)
                {
                    safe->promptTagName("Rename tag", tag.name, [safe, id = tag.id](juce::String name)
                    {
                        if (safe == nullptr || name.trim().isEmpty() || safe->writeRepo == nullptr) return;
                        if (! safe->writeRepo->renameTag(id, name))
                            safe->status.setText("That tag name is already in use.",juce::dontSendNotification);
                        safe->reloadLibrary();
                    });
                    return;
                }
            }
            safe->pageOffset = 0;
            safe->reloadLibrary();
        });
}
void MainComponent::showTagColourMenu(int64_t tagId)
{
    juce::PopupMenu menu;
    menu.setLookAndFeel(&look);
    for (int i = 0; i < (int) std::size(tagPalette); ++i) menu.addItem(i+1, tagPalette[i].name);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&tagFilter),
        [safe = juce::Component::SafePointer<MainComponent>(this), tagId](int result)
        {
            if (safe == nullptr || result <= 0 || result > (int) std::size(tagPalette) || safe->writeRepo == nullptr) return;
            safe->writeRepo->setTagColour(tagId, tagPalette[result-1].colour);
            safe->reloadLibrary();
        });
}
int MainComponent::assignTagAndRemember(const std::vector<int64_t>& sampleIds, int64_t tagId)
{
    if (writeRepo == nullptr || sampleIds.empty() || tagId <= 0) return 0;
    const auto visible = writeRepo->visibleIds(sampleIds);
    if (visible.empty()) return 0;
    const int added = writeRepo->assignTag(visible, tagId);
    if (added > 0)
    {
        writeRepo->recordRecentTag(tagId);
        writeRepo->recordTagUse(tagId);
        triggerAdaptiveLearning(visible);
    }
    return added;
}
namespace
{
struct BuiltRoundItems
{
    std::vector<RoundTagItem> inner, outer;
};
BuiltRoundItems buildRoundItems(const std::vector<Tag>& ranked, bool forFolder,
                               const std::function<bool(int64_t)>& hasEverywhere)
{
    BuiltRoundItems out;
    for (size_t i = 0; i < ranked.size(); ++i)
    {
        RoundTagItem item;
        item.tagId = ranked[i].id;
        item.label = ranked[i].name;
        item.colour = juce::Colour(ranked[i].colour);
        item.assigned = ! forFolder && hasEverywhere(ranked[i].id);
        if (i < 8) out.inner.push_back(item);
        else if (out.outer.size() < 7) out.outer.push_back(item);
        else break;
    }
    RoundTagItem all;
    all.action = true;
    all.label = "All tags" + juce::String::charToString((juce::juce_wchar) 0x2026);
    all.colour = theme::muted;
    out.outer.push_back(all);
    return out;
}
}

juce::Point<int> MainComponent::commandAnchor(bool preferFolders) const
{
    const auto screen = juce::Desktop::getMousePosition();
    const auto local = getLocalPoint(nullptr, screen);
    if (getLocalBounds().contains(local)) return local;
    return preferFolders ? folders.getBounds().getCentre() : library.getBounds().getCentre();
}

void MainComponent::showRoundTagMenuAt(juce::Point<int> anchor, bool forFolder)
{
    if (! appSettings.roundTagMenu) return;
    if (forFolder)
    {
        if (! selectedFolder.isEmpty()) showRoundTagMenuForFolder(selectedFolder, anchor);
        return;
    }
    std::vector<int64_t> ids;
    for (const auto& record : library.getSelectedSamples()) ids.push_back(record.id);
    showRoundTagMenuForIds(ids, anchor);
}

void MainComponent::showRoundTagMenuForIds(const std::vector<int64_t>& ids, juce::Point<int> anchor)
{
    if (! appSettings.roundTagMenu || readRepo == nullptr || writeRepo == nullptr
        || roundTagMenu == nullptr || ids.empty())
        return;
    std::vector<Tag> ranked;
    std::map<int64_t, std::vector<Tag>> assigned;
    try
    {
        ranked = readRepo->rankedTags(15, -1);
        assigned = readRepo->tagsForSamples(ids);
    }
    catch (const std::exception& e)
    {
        status.setText(juce::String::fromUTF8(e.what()), juce::dontSendNotification);
        return;
    }
    const auto hasEverywhere = [&assigned, &ids](int64_t tagId)
    {
        for (const auto id : ids)
        {
            const auto found = assigned.find(id);
            if (found == assigned.end()) return false;
            bool has = false;
            for (const auto& tag : found->second)
                if (tag.id == tagId) { has = true; break; }
            if (! has) return false;
        }
        return true;
    };
    auto items = buildRoundItems(ranked, false, hasEverywhere);
    auto names = std::make_shared<std::map<int64_t, juce::String>>();
    auto allHave = std::make_shared<std::set<int64_t>>();
    for (const auto& tag : ranked)
    {
        (*names)[tag.id] = tag.name;
        if (! assigned.empty() && hasEverywhere(tag.id)) allHave->insert(tag.id);
    }
    roundTagAnchorScreen = getScreenPosition() + anchor;
    const auto catalogPath = dbPath;
    roundTagMenu->onTagPicked = [safe = juce::Component::SafePointer<MainComponent>(this),
                                 ids, names, allHave, catalogPath](int64_t tagId)
    {
        if (safe == nullptr || safe->dbPath != catalogPath || safe->writeRepo == nullptr) return;
        if (safe->isSplitRunning() || safe->isExtractRunning())
        {
            safe->status.setText("Wait for the running job to finish.",
                                 juce::dontSendNotification);
            return;
        }
        const auto nameIt = names->find(tagId);
        const auto name = nameIt != names->end() ? nameIt->second : juce::String("tag");
        if (allHave->count(tagId) > 0)
        {
            safe->writeRepo->setSampleTag(ids, tagId, false);
            safe->triggerAdaptiveLearning(ids);
            safe->status.setText("Removed " + name + " from " + juce::String((int) ids.size())
                                     + " file(s)", juce::dontSendNotification);
        }
        else
        {
            const int added = safe->assignTagAndRemember(ids, tagId);
            safe->status.setText(added > 0
                ? "Tagged " + juce::String(added) + " of " + juce::String((int) ids.size())
                      + " file(s) as " + name
                : "All selected files already have the tag " + name,
                juce::dontSendNotification);
        }
        safe->reloadLibrary();
    };
    roundTagMenu->onAllTagsPicked = [safe = juce::Component::SafePointer<MainComponent>(this),
                                     ids, catalogPath]
    {
        if (safe == nullptr || safe->dbPath != catalogPath) return;
        safe->showAllTagsMenu(ids, safe->roundTagAnchorScreen);
    };
    const auto font = fonts::font(10.5f, fonts::Weight::medium);
    auto geometry = layoutRoundTagMenu(anchor, getLocalBounds(), items.inner, items.outer, font);
    roundTagMenu->open(std::move(geometry),
                       ids.size() == 1 ? juce::String("1 file")
                                       : juce::String((int) ids.size()) + " files",
                       "TAGS");
}

void MainComponent::showRoundTagMenuForFolder(const juce::String& folder, juce::Point<int> anchor)
{
    if (! appSettings.roundTagMenu || readRepo == nullptr || roundTagMenu == nullptr)
        return;
    std::vector<Tag> ranked;
    int catalogued = 0;
    try
    {
        ranked = readRepo->rankedTags(15, -1);
        catalogued = readRepo->countInFolder(folder);
    }
    catch (const std::exception& e)
    {
        status.setText(juce::String::fromUTF8(e.what()), juce::dontSendNotification);
        return;
    }
    if (catalogued <= 0)
    {
        status.setText("No catalogued files below " + folder, juce::dontSendNotification);
        return;
    }
    const auto items = buildRoundItems(ranked, true, [](int64_t) { return false; });
    roundTagAnchorScreen = getScreenPosition() + anchor;
    const auto catalogPath = dbPath;
    const auto folderPath = folder;
    roundTagMenu->onTagPicked = [safe = juce::Component::SafePointer<MainComponent>(this),
                                 folderPath, catalogPath](int64_t tagId)
    {
        if (safe == nullptr || safe->dbPath != catalogPath) return;
        safe->confirmTagFolder(folderPath, tagId);
    };
    roundTagMenu->onAllTagsPicked = [safe = juce::Component::SafePointer<MainComponent>(this),
                                     folderPath, catalogPath]
    {
        if (safe == nullptr || safe->dbPath != catalogPath) return;
        safe->showAllTagsMenuForFolder(folderPath, safe->roundTagAnchorScreen);
    };
    const auto font = fonts::font(10.5f, fonts::Weight::medium);
    auto geometry = layoutRoundTagMenu(anchor, getLocalBounds(), items.inner, items.outer, font);
    auto name = juce::File(folder).getFileName();
    if (name.isEmpty()) name = folder;
    roundTagMenu->open(std::move(geometry), name,
                       catalogued == 1 ? juce::String("1 file")
                                       : juce::String(catalogued) + " files");
}

void MainComponent::showAllTagsMenu(const std::vector<int64_t>& ids, juce::Point<int> screenPosition)
{
    if (readRepo == nullptr || writeRepo == nullptr || ids.empty()) return;
    std::vector<Tag> tags;
    std::map<int64_t, std::vector<Tag>> assigned;
    try
    {
        tags = readRepo->tags();
        assigned = readRepo->tagsForSamples(ids);
    }
    catch (...) { return; }
    std::map<int64_t, bool> allHave;
    for (const auto& tag : tags)
    {
        bool every = true;
        for (const auto id : ids)
        {
            bool has = false;
            const auto found = assigned.find(id);
            if (found != assigned.end())
                for (const auto& current : found->second)
                    if (current.id == tag.id) { has = true; break; }
            if (! has) { every = false; break; }
        }
        allHave[tag.id] = every;
    }
    juce::PopupMenu menu;
    menu.setLookAndFeel(&look);
    for (int i = 0; i < (int) tags.size(); ++i)
        menu.addItem(10000 + i, tags[(size_t) i].name, true, allHave[tags[(size_t) i].id],
                     tagDotImage(tags[(size_t) i].colour));
    if (tags.empty()) menu.addSectionHeader("No tags yet");
    if (! tags.empty()) menu.addSeparator();
    menu.addItem(9003, "New tag...");
    const auto catalogPath = dbPath;
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(
                           { screenPosition.x, screenPosition.y, 1, 1 })
                           .withMaximumNumColumns(1),
        [safe = juce::Component::SafePointer<MainComponent>(this), ids, tags, allHave,
         catalogPath](int result)
        {
            if (safe == nullptr || result == 0 || safe->dbPath != catalogPath
                || safe->writeRepo == nullptr)
                return;
            if (result == 9003)
            {
                safe->promptTagName("New tag", {}, [safe, ids](juce::String name)
                {
                    if (safe == nullptr || name.trim().isEmpty() || safe->writeRepo == nullptr) return;
                    try
                    {
                        const auto colour = tagPalette[safe->allTags.size() % std::size(tagPalette)].colour;
                        const auto id = safe->writeRepo->createTag(name, colour);
                        safe->assignTagAndRemember(ids, id);
                        safe->reloadLibrary();
                    }
                    catch (const std::exception& e)
                    {
                        safe->status.setText(juce::String::fromUTF8(e.what()),
                                             juce::dontSendNotification);
                    }
                });
                return;
            }
            const size_t index = (size_t) (result - 10000);
            if (index >= tags.size()) return;
            const auto tag = tags[index];
            const auto state = allHave.find(tag.id);
            const bool assign = state == allHave.end() || ! state->second;
            if (assign) safe->assignTagAndRemember(ids, tag.id);
            else
            {
                safe->writeRepo->setSampleTag(ids, tag.id, false);
                safe->triggerAdaptiveLearning(ids);
            }
            safe->reloadLibrary();
        });
}

void MainComponent::showAllTagsMenuForFolder(const juce::String& folder,
                                             juce::Point<int> screenPosition)
{
    if (readRepo == nullptr) return;
    std::vector<Tag> tags;
    try { tags = readRepo->tags(); } catch (...) { return; }
    juce::PopupMenu menu;
    menu.setLookAndFeel(&look);
    for (int i = 0; i < (int) tags.size(); ++i)
        menu.addItem(11000 + i, tags[(size_t) i].name, true, false,
                     tagDotImage(tags[(size_t) i].colour));
    if (tags.empty()) menu.addSectionHeader("No tags yet");
    const auto catalogPath = dbPath;
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(
                           { screenPosition.x, screenPosition.y, 1, 1 })
                           .withMaximumNumColumns(1),
        [safe = juce::Component::SafePointer<MainComponent>(this), folder, tags,
         catalogPath](int result)
        {
            if (safe == nullptr || result == 0 || safe->dbPath != catalogPath) return;
            const size_t index = (size_t) (result - 11000);
            if (index >= tags.size()) return;
            safe->confirmTagFolder(folder, tags[index].id);
        });
}

void MainComponent::confirmTagFolder(const juce::String& folderPath, int64_t tagId)
{
    if (readRepo == nullptr || writeRepo == nullptr || folderPath.isEmpty() || tagId <= 0) return;
    if (isSplitRunning() || isExtractRunning())
    {
        status.setText("Wait for the running job to finish.", juce::dontSendNotification);
        return;
    }
    std::vector<Tag> tags;
    int catalogued = 0, already = 0;
    try
    {
        tags = readRepo->tags();
        catalogued = readRepo->countInFolder(folderPath);
        already = readRepo->countWithTagInFolder(folderPath, tagId);
    }
    catch (const std::exception& e)
    {
        status.setText(juce::String::fromUTF8(e.what()), juce::dontSendNotification);
        return;
    }
    const auto found = std::find_if(tags.begin(), tags.end(),
                                    [tagId](const Tag& tag) { return tag.id == tagId; });
    if (found == tags.end()) return;
    const auto tag = *found;
    const int added = juce::jmax(0, catalogued - already);
    dialogs::showAlert(*this, "Tag folder",
        "Folder:\n" + folderPath
            + "\n\nIncludes every subfolder."
            + "\nCatalogued files: " + juce::String(catalogued)
            + "\nNew assignments: " + juce::String(added)
            + "\nAlready tagged: " + juce::String(already)
            + "\n\nOriginal files are not changed. Repeating this is safe.",
        { { "Tag", 1 }, { "Cancel", 0 } },
        [safe = juce::Component::SafePointer<MainComponent>(this), folderPath, tag](int choice)
        {
            if (safe == nullptr || choice != 1 || safe->tagging == nullptr) return;
            if (safe->tagging->progress().running)
            {
                safe->status.setText("Another tagging operation is already running.",
                                     juce::dontSendNotification);
                return;
            }
            if (safe->autoTag != nullptr && safe->autoTag->progress().running)
            {
                safe->status.setText("Wait for AutoTag to finish before tagging the folder.",
                                     juce::dontSendNotification);
                return;
            }
            if (safe->tagging->startTagFolder(folderPath, tag.id))
                safe->status.setText("Tagging catalogued files in " + folderPath,
                                     juce::dontSendNotification);
            else
                safe->status.setText("Tagging could not start.", juce::dontSendNotification);
        });
}
void MainComponent::showEditTagsMenu()
{
    if (! readRepo || selectedSampleId <= 0)
        return;
    if (appSettings.roundTagMenu)
    {
        showRoundTagMenuForIds({ selectedSampleId }, editTags.getBounds().getCentre());
        return;
    }
    const auto sampleId = selectedSampleId;
    auto assigned = readRepo->tagsForSample(sampleId);
    juce::PopupMenu menu;
    menu.setLookAndFeel(&look);
    for (int i = 0; i < (int) allTags.size(); ++i)
    {
        bool isAssigned = false;
        for (const auto& tag : assigned) if (tag.id == allTags[(size_t) i].id) isAssigned = true;
        menu.addItem(6000+i, allTags[(size_t) i].name, true, isAssigned);
    }
    if (allTags.empty()) menu.addSectionHeader("No tags yet");
    menu.addSeparator();
    menu.addItem(6999, "New tag...");
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&editTags).withMaximumNumColumns(1),
        [safe = juce::Component::SafePointer<MainComponent>(this), sampleId](int result)
        {
            if (safe == nullptr || result == 0 || safe->writeRepo == nullptr) return;
            if (result == 6999)
            {
                safe->promptTagName("New tag", {}, [safe, sampleId](juce::String name)
                {
                    if (safe == nullptr || name.trim().isEmpty() || safe->writeRepo == nullptr) return;
                    try
                    {
                        const auto colour = tagPalette[safe->allTags.size() % std::size(tagPalette)].colour;
                        const auto id = safe->writeRepo->createTag(name, colour);
                        safe->assignTagAndRemember({ sampleId }, id);
                        safe->reloadLibrary();
                    }
                    catch (const std::exception& e) { safe->status.setText(juce::String::fromUTF8(e.what()),juce::dontSendNotification); }
                });
                return;
            }
            const size_t index = (size_t) (result - 6000);
            if (index >= safe->allTags.size()) return;
            const auto tag = safe->allTags[index];
            bool isAssigned = false;
            for (const auto& current : safe->readRepo->tagsForSample(sampleId))
                if (current.id == tag.id) isAssigned = true;
            if (isAssigned) { safe->writeRepo->setSampleTag(sampleId, tag.id, false); safe->triggerAdaptiveLearning({ sampleId }); }
            else safe->assignTagAndRemember({ sampleId }, tag.id);
            safe->reloadLibrary();
        });
}
void MainComponent::markSessionStateDirty()
{
    if (restoringSession || ! sessionLive) return;
    sessionDirty = true;
    // The plugin stores the same two values in the DAW project instead of settings.json.
    if (embeddedSession.onChanged != nullptr)
        embeddedSession.onChanged(selectedFolder, sessionSamplePath);
}
void MainComponent::flushSessionState()
{
    saveSessionState();
}
void MainComponent::applySessionSelection(const juce::String& folder, const juce::String& sample)
{
    if (readRepo == nullptr) return;
    // Restoring a project is not a user edit: no selection callback and no dirty session while it
    // runs, exactly as when the window opens with the same values.
    const juce::ScopedValueSetter<bool> restoring(restoringSession, true);
    selectedFolder.clear();
    if (folder.isNotEmpty()
        && juce::File(folder).isDirectory()
        && readRepo->countInFolder(folder) > 0)
        selectedFolder = folder;
    pendingTreeSelection = selectedFolder;
    requestFolderTreeRefresh();
    reloadLibrary();
    if (sample.isEmpty()) return;
    const auto record = readRepo->findByPath(sample);
    if (! record.has_value()) return;
    const bool inScope = selectedFolder.isEmpty()
        || juce::File(record->filePath).isAChildOf(juce::File(selectedFolder));
    if (! inScope || ! library.selectSampleById(record->id)) return;
    selectedSampleId = record->id;
    sessionSamplePath = record->filePath;
    updateSelectionStatus(*record);
    updateSelectedTags();
    // The restored sample is shown by the preview; the analysis card must match it.
    refreshAnalysis(analysisFor(*record));
    if (record->isDiscEntry())
    {
        preview.clearDiscSource();
        status.setText("Inside " + juce::File(record->containerPath).getFileName()
            + "  ·  " + record->discPath + ". Press Enter to open the image.",
            juce::dontSendNotification);
    }
    else if (isDiscImageFormat(record->format))
    {
        preview.clearDiscSource();
        status.setText("Double-click the image or press Enter to browse its contents",
                      juce::dontSendNotification);
    }
    // Restored projects never start playback: a host opening a session must stay silent.
    else preview.loadFile(juce::File(record->filePath), false);
}
void MainComponent::saveSessionState()
{
    if (! sessionDirty) return;
    // An embedded window's session belongs to the DAW project, not to the application's file.
    if (embeddedUi) return;
    try
    {
        settingsStore.update([this](AppSettings& saved)
        {
            saved.lastFolder = selectedFolder;
            saved.lastSamplePath = sessionSamplePath;
        });
        sessionDirty = false;
        lastSessionSaveMs = juce::Time::getMillisecondCounter();
    }
    catch (const std::exception& e)
    {
        status.setText("Session state was not saved: " + juce::String::fromUTF8(e.what()),
            juce::dontSendNotification);
    }
}
void MainComponent::requestFolderTreeRefresh()
{
    if (readRepo == nullptr) return;
    if (folderIndexRunner == nullptr)
        folderIndexRunner = std::make_unique<FolderIndexRunner>(*readRepo);

    FolderIndexRequest request;
    request.roots = readRepo->roots();
    request.ignored = readRepo->ignoredPaths();
    request.revealIgnored = filter.showIgnored;
    request.showAllSamples = true;
    juce::Component::SafePointer<MainComponent> safe(this);
    folderIndexRunner->request(std::move(request), [safe](FolderIndex index)
    {
        if (safe != nullptr) safe->applyFolderIndex(std::move(index));
    });
}
void MainComponent::applyFolderIndex(FolderIndex index)
{
    lastFolderIndex = std::move(index);
    folders.applyIndex(lastFolderIndex, true);
    applyFavoritesIndex();
    if (pendingTreeSelection.isNotEmpty())
    {
        folders.selectPath(pendingTreeSelection);
        pendingTreeSelection.clear();
    }
    resized();
}
void MainComponent::applyFavoritesIndex()
{
    const bool hasFavorites = ! appSettings.favoriteFolders.isEmpty();
    if (hasFavorites)
        favorites.applyIndex(folderIndexUnderRoots(lastFolderIndex, appSettings.favoriteFolders), false);
    else
        favorites.applyIndex({}, false);
    favoritesLabel.setVisible(hasFavorites);
    favorites.setVisible(hasFavorites);
    favoritesSplitter.setVisible(hasFavorites);
    if (! hasFavorites) favorites.clearSelection();
}
void MainComponent::refreshFavorites()
{
    if (readRepo == nullptr) return;
    // The folder structure comes from the last background build; changing favorites only picks a
    // different subtree out of it, so this stays instant.
    applyFavoritesIndex();
    resized();
}
bool MainComponent::isFavoriteFolder(const juce::String& path) const
{
    const auto target = canonicalFolderPath(path);
    if (target.isEmpty()) return false;
    for (const auto& favorite : appSettings.favoriteFolders)
        if (canonicalFolderPath(favorite) == target) return true;
    return false;
}
bool MainComponent::isUnderFavoriteFolder(const juce::String& path) const
{
    const juce::File target(canonicalFolderPath(path));
    if (target.getFullPathName().isEmpty()) return false;
    for (const auto& favorite : appSettings.favoriteFolders)
    {
        const juce::File root(canonicalFolderPath(favorite));
        if (target != root && target.isAChildOf(root)) return true;
    }
    return false;
}
void MainComponent::toggleFavoriteFolder(const juce::String& path)
{
    const auto target = canonicalFolderPath(path);
    if (target.isEmpty()) return;
    auto next = appSettings.favoriteFolders;
    bool removed = false;
    for (int i = next.size(); --i >= 0;)
        if (canonicalFolderPath(next[i]) == target) { next.remove(i); removed = true; }
    if (! removed)
    {
        if (isUnderFavoriteFolder(target))
        {
            status.setText("This folder is already visible inside a favorite.",juce::dontSendNotification);
            return;
        }
        // A parent favorite contains the whole subtree; drop roots that would duplicate it.
        for (int i = next.size(); --i >= 0;)
            if (juce::File(canonicalFolderPath(next[i])).isAChildOf(juce::File(target)))
                next.remove(i);
        next.add(target);
    }
    try
    {
        settingsStore.update([&next](AppSettings& saved) { saved.favoriteFolders = next; });
        appSettings.favoriteFolders = next;
        refreshFavorites();
        status.setText(removed ? "Removed from favorites: " + target
                               : "Added to favorites: " + target,juce::dontSendNotification);
    }
    catch (const std::exception& e)
    {
        status.setText("Could not save favorites: " + juce::String::fromUTF8(e.what()),juce::dontSendNotification);
    }
}
void MainComponent::showFavoriteContextMenu(const juce::String& path, juce::Point<int> screenPosition)
{
    // Only favorite roots get the menu; children are already covered by their root.
    if (path.isEmpty() || ! isFavoriteFolder(path)) return;
    juce::PopupMenu menu;
    menu.setLookAndFeel(&look);
    menu.addItem(7201, "Remove from favorites");
    menu.addItem(7202, "Rename folder...");
    if (! embeddedUi) menu.addItem(7203, "Find duplicates...");
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&favorites).withTargetScreenArea(
                           { screenPosition.x, screenPosition.y, 1, 1 })
                           .withMaximumNumColumns(1)
                           .withPreferredPopupDirection(juce::PopupMenu::Options::PopupDirection::downwards),
        [safe = juce::Component::SafePointer<MainComponent>(this), path](int result)
        {
            if (safe == nullptr) return;
            if (result == 7201) safe->toggleFavoriteFolder(path);
            else if (result == 7202) safe->promptRenameFolder(path);
            else if (result == 7203) safe->openDuplicateCleanup(path);
        });
}
void MainComponent::promptRenameFolder(const juce::String& path)
{
    if (readRepo == nullptr) return;
    const auto canonical = canonicalFolderPath(path);
    if (canonical.isEmpty()) return;
    const auto current = appSettings.folderAliases.getValue(
        canonical, juce::File(canonical).getFileName());
    dialogs::showPrompt(*this, "Rename folder", "Display name:", current,
        [safe = juce::Component::SafePointer<MainComponent>(this), canonical](const juce::String& value)
        {
            if (safe == nullptr) return;
            const auto name = value.trim();
            const auto original = juce::File(canonical).getFileName();
            auto aliases = safe->appSettings.folderAliases;
            if (name.isEmpty() || name == original) aliases.remove(juce::StringRef(canonical));
            else aliases.set(canonical, name);
            try
            {
                safe->settingsStore.update([aliases](AppSettings& saved) { saved.folderAliases = aliases; });
                safe->appSettings.folderAliases = aliases;
                safe->folders.setFolderAliases(aliases);
                safe->favorites.setFolderAliases(aliases);
                safe->status.setText("Folder shown as \"" + (name.isEmpty() ? original : name) + "\"",
                                     juce::dontSendNotification);
            }
            catch (const std::exception& e)
            {
                safe->status.setText("Could not save the folder name: "
                    + juce::String::fromUTF8(e.what()), juce::dontSendNotification);
            }
        });
}
void MainComponent::resetTagsSelected(const std::vector<int64_t>& sampleIds)
{
    if (isSplitRunning())
    {
        status.setText("Wait for Split to finish.", juce::dontSendNotification);
        return;
    }
    if (readRepo == nullptr || sampleIds.empty()) return;
    if (autoTagDialog != nullptr)
    {
        status.setText("Close the AutoTag review before resetting tags.",juce::dontSendNotification);
        return;
    }
    std::vector<int64_t> visible;
    try { visible = readRepo->visibleIds(sampleIds); }
    catch (const std::exception& e) { status.setText(juce::String::fromUTF8(e.what()),juce::dontSendNotification); return; }
    if (visible.empty()) { status.setText("The selected files are ignored.",juce::dontSendNotification); return; }
    int tagged = 0;
    try { tagged = readRepo->countTaggedSamples(visible); }
    catch (const std::exception& e) { status.setText(juce::String::fromUTF8(e.what()),juce::dontSendNotification); return; }
    if (tagged == 0)
    {
        status.setText("The selected files have no tags to reset.",juce::dontSendNotification);
        return;
    }
    dialogs::showAlert(*this, "Reset tags",
        "Remove all tag assignments from " + juce::String(visible.size())
            + " selected file(s)?\n\n" + juce::String(tagged)
            + " file(s) currently have tags.\n\nTag definitions, stars, smart overrides and original files are kept.",
        { { "Reset", 1 }, { "Cancel", 0 } },
        [safe = juce::Component::SafePointer<MainComponent>(this), visible](int choice)
        {
            if (safe == nullptr || choice != 1 || safe->tagging == nullptr) return;
            if (safe->autoTag != nullptr && safe->autoTag->progress().running)
            {
                safe->status.setText("Wait for AutoTag to finish before resetting tags.",juce::dontSendNotification);
                return;
            }
            if (safe->tagging->startResetSelected(visible))
                safe->status.setText("Removing tags from selected files",juce::dontSendNotification);
            else
                safe->status.setText("Tag reset could not start.",juce::dontSendNotification);
        }, juce::MessageBoxIconType::WarningIcon);
}
void MainComponent::resetTagsFolder(const juce::String& path, int catalogued)
{
    if (isSplitRunning())
    {
        status.setText("Wait for Split to finish.", juce::dontSendNotification);
        return;
    }
    if (readRepo == nullptr || path.isEmpty()) return;
    if (autoTagDialog != nullptr)
    {
        status.setText("Close the AutoTag review before resetting tags.",juce::dontSendNotification);
        return;
    }
    int tagged = 0;
    try { tagged = readRepo->countTaggedInFolder(path); }
    catch (const std::exception& e) { status.setText(juce::String::fromUTF8(e.what()),juce::dontSendNotification); return; }
    if (tagged == 0)
    {
        status.setText("This folder has no tagged files to reset.",juce::dontSendNotification);
        return;
    }
    dialogs::showAlert(*this, "Reset tags in folder",
        "Folder:\n" + path
            + "\n\nIncludes every subfolder."
            + "\nCatalogued files: " + juce::String(catalogued)
            + "\nTagged files: " + juce::String(tagged)
            + "\n\nTag definitions, stars, smart overrides and original files are kept.",
        { { "Reset", 1 }, { "Cancel", 0 } },
        [safe = juce::Component::SafePointer<MainComponent>(this), path](int choice)
        {
            if (safe == nullptr || choice != 1 || safe->tagging == nullptr) return;
            if (safe->autoTag != nullptr && safe->autoTag->progress().running)
            {
                safe->status.setText("Wait for AutoTag to finish before resetting tags.",juce::dontSendNotification);
                return;
            }
            if (safe->tagging->startResetFolder(path))
                safe->status.setText("Removing tags from " + path,juce::dontSendNotification);
            else
                safe->status.setText("Tag reset could not start.",juce::dontSendNotification);
        }, juce::MessageBoxIconType::WarningIcon);
}
void MainComponent::startAutoTagSelected(const std::vector<int64_t>& sampleIds)
{
    if (isSplitRunning())
    {
        status.setText("Wait for Split to finish.", juce::dontSendNotification);
        return;
    }
    if (autoTag == nullptr || sampleIds.empty()) return;
    std::vector<int64_t> visible;
    try { visible = readRepo == nullptr ? sampleIds : readRepo->visibleIds(sampleIds); }
    catch (const std::exception& e) { status.setText(juce::String::fromUTF8(e.what()), juce::dontSendNotification); return; }
    if (visible.empty()) { status.setText("The selected files are ignored.", juce::dontSendNotification); return; }
    if (autoTagDialog != nullptr)
    {
        autoTagDialog->toFront(true);
        status.setText("Close the open AutoTag review before preparing another.", juce::dontSendNotification);
        return;
    }
    if (autoTag->progress().running || (tagging && tagging->progress().running))
    {
        status.setText("Another bulk tagging operation is already running.", juce::dontSendNotification);
        return;
    }
    if (! analysisAllows("AutoTag", false,
            [safe = juce::Component::SafePointer<MainComponent>(this), sampleIds]
            { if (safe != nullptr) safe->startAutoTagSelected(sampleIds); }))
        return;
    if (autoTag->startSelected(visible))
    {
        autoTagPreparing = true;
        autoTagWasRunning = true;
        status.setText("Preparing AutoTag proposals for " + juce::String(visible.size())
            + " selected file(s)", juce::dontSendNotification);
    }
    else
        status.setText("AutoTag could not start.", juce::dontSendNotification);
}
void MainComponent::startAutoTagFolder(const juce::String& folder)
{
    if (isSplitRunning())
    {
        status.setText("Wait for Split to finish.", juce::dontSendNotification);
        return;
    }
    if (autoTag == nullptr || folder.isEmpty()) return;
    if (autoTagDialog != nullptr)
    {
        autoTagDialog->toFront(true);
        status.setText("Close the open AutoTag review before preparing another.", juce::dontSendNotification);
        return;
    }
    if (autoTag->progress().running || (tagging && tagging->progress().running))
    {
        status.setText("Another bulk tagging operation is already running.", juce::dontSendNotification);
        return;
    }
    if (! analysisAllows("AutoTag", false,
            [safe = juce::Component::SafePointer<MainComponent>(this), folder]
            { if (safe != nullptr) safe->startAutoTagFolder(folder); }))
        return;
    if (autoTag->startFolder(folder))
    {
        autoTagPreparing = true;
        autoTagWasRunning = true;
        status.setText("Preparing AutoTag proposals for " + folder, juce::dontSendNotification);
    }
    else
        status.setText("AutoTag could not start.", juce::dontSendNotification);
}
void MainComponent::showAutoTagDialog(AutoTagResult result)
{
    if (autoTagDialog != nullptr || autoTag == nullptr) return;
    auto* dialog = new AutoTagDialog(std::move(result), *autoTag,
        [safe = juce::Component::SafePointer<MainComponent>(this)]
        {
            if (safe != nullptr) safe->reloadLibrary();
        },
        [safe = juce::Component::SafePointer<MainComponent>(this)](const juce::File& file)
        {
            if (safe == nullptr) return;
            if (safe->preview.loadFile(file, true) && ! safe->preview.transportPlaying())
                safe->preview.playPause();
        },
        [safe = juce::Component::SafePointer<MainComponent>(this)]
        {
            if (safe != nullptr) safe->preview.stopPlayback();
        },
        [this]() -> std::vector<Tag>
        {
            try { return readRepo != nullptr ? readRepo->rankedTags(15) : std::vector<Tag>(); }
            catch (...) { return {}; }
        }(),
        [this]() -> std::vector<Tag>
        {
            try { return readRepo != nullptr ? readRepo->tags() : std::vector<Tag>(); }
            catch (...) { return {}; }
        }());
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(dialog);
    options.dialogTitle = "AutoTag proposals";
    options.dialogBackgroundColour = theme::background;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = false;
    options.resizable = true;
    autoTagDialog = options.launchAsync();
    if (autoTagDialog != nullptr) autoTagDialog->setLookAndFeel(&look);
}

void MainComponent::openBatchProcessing(const batch::SourceSelection& selection)
{
    if (readRepo == nullptr) return;
    if (isBatchRunning())
    {
        status.setText("Batch Processing is already running.", juce::dontSendNotification);
        return;
    }
    if (batchContent != nullptr)
    {
        if (batchDialog != nullptr) batchDialog->toFront(true);
        return;
    }
    if (isSplitRunning() || isExtractRunning() || isNormalizeRunning()
        || (tagging && tagging->progress().running) || (autoTag && autoTag->progress().running)
        || (indexer && indexer->progress().running) || (smart && smart->progress().running))
    {
        status.setText("Wait for the running job to finish before Batch Processing.",
                       juce::dontSendNotification);
        return;
    }
    batch::Request request;
    request.sources = selection;
    if (preview.hasUnsavedEdits() && selection.kind == batch::SourceSelection::Kind::rows)
    {
        const auto unsaved = preview.loadedFilePath();
        for (const auto id : selection.rows)
        {
            const auto record = readRepo->findById(id);
            if (record.has_value() && juce::File(record->filePath) == unsaved)
                request.sources.unsavedEditedRows.push_back(id);
        }
    }
    batch::PreflightResult prepared;
    try { prepared = batch::prepareBatch(*readRepo, request, {}); }
    catch (const std::exception& error)
    {
        status.setText("Batch preflight failed: " + juce::String::fromUTF8(error.what()),
                       juce::dontSendNotification);
        return;
    }
    if (! prepared.ok || prepared.batch.sources.empty())
    {
        juce::String message = "No file can be batch processed.";
        int shown = 0;
        for (const auto& issue : prepared.issues)
        {
            if (issue.warning || shown >= 6) continue;
            message += "\n\n" + issue.message;
            if (issue.path.isNotEmpty()) message += "\n" + issue.path;
            ++shown;
        }
        if (shown == 0) message += "\n\n" + juce::String("The selection has no supported audio file.");
        dialogs::showAlert(*this, "Batch Processing", message, { { "OK", 1 } }, nullptr,
                           juce::MessageBoxIconType::InfoIcon);
        return;
    }

    auto* dialog = new batch::BatchProcessingDialog(prepared.batch,
                                                    batch::BatchPresetStore::defaultFileFor(settingsStore));
    dialog->onStart = [safe = juce::Component::SafePointer<MainComponent>(this)]
    {
        if (safe != nullptr) safe->startBatchProcessing();
    };
    dialog->onCancelJob = [safe = juce::Component::SafePointer<MainComponent>(this)]
    {
        if (safe != nullptr && safe->batchService != nullptr) safe->batchService->cancel();
    };
    dialog->onClose = [safe = juce::Component::SafePointer<MainComponent>(this)]
    {
        if (safe != nullptr) safe->closeBatchDialog();
    };
    dialog->onPreparePreview = [safe = juce::Component::SafePointer<MainComponent>(this)](
        batch::SourceSnapshot snapshot, batch::Recipe recipe, int generation)
    {
        if (safe != nullptr) safe->startBatchPreview(snapshot, recipe, generation);
    };
    dialog->onPlayPreview = [safe = juce::Component::SafePointer<MainComponent>(this)](
        juce::File file, bool processed)
    {
        if (safe != nullptr) safe->auditionBatchPreview(file, processed);
    };
    dialog->onStopPreview = [safe = juce::Component::SafePointer<MainComponent>(this)]
    {
        if (safe != nullptr) safe->stopBatchPreview();
    };
    dialog->onOpenOutputFolder = [safe = juce::Component::SafePointer<MainComponent>(this)]
    {
        if (safe == nullptr) return;
        if (safe->batchContent != nullptr)
            for (const auto& output : safe->batchContent->report().outputs)
                if (output.state == batch::OutputState::completed && output.target.existsAsFile())
                {
                    juce::Process::openDocument(output.target.getParentDirectory().getFullPathName(), {});
                    return;
                }
        if (safe->batchService != nullptr)
            for (const auto& output : safe->batchService->results().outputs)
                if (output.state == batch::OutputState::completed)
                {
                    juce::Process::openDocument(output.target.getParentDirectory().getFullPathName(), {});
                    return;
                }
        safe->status.setText("No output folder yet.", juce::dontSendNotification);
    };
    dialog->onExportReport = [safe = juce::Component::SafePointer<MainComponent>(this)]
    {
        if (safe == nullptr || safe->batchContent == nullptr) return;
        auto* safeDialog = safe->batchContent.getComponent();
        auto* chooser = new juce::FileChooser("Export batch report",
            juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                .getChildFile("batch-report.json"), "*.json", true);
        const auto report = safe->batchContent->report();
        chooser->launchAsync(juce::FileBrowserComponent::saveMode
                                 | juce::FileBrowserComponent::canSelectFiles,
            [chooser, report](const juce::FileChooser& chosen)
            {
                const auto file = chosen.getResult();
                if (file != juce::File())
                    file.replaceWithText(batch::BatchService::reportToJson(report), false, false, "\n");
                delete chooser;
            });
        juce::ignoreUnused(safeDialog);
    };
    dialog->onScanNewFiles = [safe = juce::Component::SafePointer<MainComponent>(this)]
    {
        if (safe == nullptr || safe->indexer == nullptr || safe->batchContent == nullptr) return;
        if (safe->isBatchRunning() || safe->indexer->progress().running) return;
        juce::StringArray folders;
        for (const auto& output : safe->batchContent->report().outputs)
            if (output.state == batch::OutputState::completed)
                folders.addIfNotAlreadyThere(output.target.getParentDirectory().getFullPathName());
        if (! folders.isEmpty() && safe->indexer->startFoldersNewOnly(folders))
            safe->status.setText("Scanning new batch outputs", juce::dontSendNotification);
        else
            safe->status.setText("The new outputs cannot be scanned now.", juce::dontSendNotification);
    };
    batchContent = dialog;
    auto* window = new BatchDialogWindow();
    window->setContentOwned(dialog, true);
    window->setLookAndFeel(&look);
    window->setUsingNativeTitleBar(false);
    window->setResizable(true, true);
    window->setSize(dialog->getWidth(), dialog->getHeight());
    window->centreAroundComponent(this, window->getWidth(), window->getHeight());
    window->onClose = [safe = juce::Component::SafePointer<MainComponent>(this)]
    {
        if (safe != nullptr) safe->closeBatchDialog();
    };
    batchDialog = window;
    batchClosePending = false;
    window->enterModalState(true, nullptr, true);
}

void MainComponent::startBatchProcessing()
{
    if (batchContent == nullptr || readRepo == nullptr || isBatchRunning()) return;
    batch::Request request;
    request.sources = batchContent->selection();
    request.recipe = batchContent->currentRecipe();
    request.conflicts = batchContent->conflictPolicy();
    request.destination = batchContent->destination();
    if (request.conflicts == batch::ConflictPolicy::replaceOutput)
    {
        batch::PreflightResult check;
        try { check = batch::replanBatch(batchContent->prepared(), request, {}); }
        catch (const std::exception& error)
        {
            batchContent->setPreviewStatus("Batch preflight failed: "
                + juce::String::fromUTF8(error.what()));
            return;
        }
        int replacements = 0;
        for (const auto& issue : check.issues)
            if (issue.code == batch::ErrorCode::replacement_not_confirmed) ++replacements;
        if (replacements > 0)
        {
            auto captured = request;
            juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon,
                "Replace existing outputs",
                juce::String(replacements)
                    + " existing output file(s) will be replaced. A file that changed since "
                      "preflight is refused, never overwritten.\n\nReplace them?",
                "Replace", "Cancel", this,
                juce::ModalCallbackFunction::create(
                    [safe = juce::Component::SafePointer<MainComponent>(this), captured](int choice) mutable
                    {
                        if (safe == nullptr || choice == 0) return;
                        captured.replacementConfirmed = true;
                        safe->launchBatch(std::move(captured));
                    }));
            return;
        }
    }
    request.replacementConfirmed = true;
    launchBatch(std::move(request));
}

void MainComponent::launchBatch(batch::Request request)
{
    if (batchContent == nullptr || isBatchRunning()) return;
    if (batchPreview != nullptr && batchPreview->isRunning())
    {
        // Start never blocks the message thread on the preview worker: cancel it and let the
        // timer launch the job once that thread has stopped.
        batchPreview->cancel();
        pendingBatchRequest = std::move(request);
        pendingBatchLaunch = true;
        batchContent->setPreviewStatus("Stopping the preview before Start...");
        return;
    }
    batch::PreflightResult prepared;
    try { prepared = batch::replanBatch(batchContent->prepared(), request, {}); }
    catch (const std::exception& error)
    {
        batchContent->setPreviewStatus("Batch preflight failed: "
            + juce::String::fromUTF8(error.what()));
        return;
    }
    if (! prepared.ok)
    {
        juce::String message;
        for (const auto& issue : prepared.issues)
            if (! issue.warning) message += (message.isEmpty() ? "" : "\n") + issue.message;
        batchContent->setPreviewStatus(message.isEmpty() ? "The request is not valid" : message);
        return;
    }
    batchService = std::make_unique<batch::BatchService>();
    const auto reportFolder = settingsStore.file().getParentDirectory().getChildFile("batch-reports");
    if (! batchService->start(prepared.batch, reportFolder))
    {
        batchService.reset();
        batchContent->setPreviewStatus("The batch job could not start.");
        return;
    }
    batchWasRunning = true;
    batchContent->setRunning(true);
    status.setText("Batch Processing started", juce::dontSendNotification);
}

void MainComponent::startBatchPreview(const batch::SourceSnapshot& snapshot,
                                      const batch::Recipe& recipe, int generation)
{
    if (batchContent == nullptr || isBatchRunning()) return;
    if (preview.hasUnsavedEdits())
    {
        batchContent->setPreviewStatus("Save or discard the editor changes before auditioning.");
        return;
    }
    if (batchPreview != nullptr && batchPreview->isRunning())
    {
        batchContent->setPreviewStatus("A preview is still running; stop it first.");
        return;
    }
    if (batchPreview != nullptr) batchPreview.reset();
    if (batchPreviewFolder != juce::File()) batchPreviewFolder.deleteRecursively();
    if (! batchPreviewRestore.existsAsFile() && preview.loadedFilePath().existsAsFile())
        batchPreviewRestore = preview.loadedFilePath();
    batchPreview = std::make_unique<batch::BatchPreviewService>();
    batchPreviewFolder = settingsStore.file().getParentDirectory().getChildFile("batch-preview");
    if (! batchPreview->start(snapshot, recipe, generation, batchPreviewFolder))
    {
        batchPreview.reset();
        batchContent->setPreviewStatus("Only one preview can run at a time.");
        return;
    }
    batchPreviewWasRunning = true;
    batchContent->setPreviewStatus("Analyzing the whole source...");
}

void MainComponent::auditionBatchPreview(const juce::File& file, bool processed)
{
    if (! file.existsAsFile()) return;
    if (preview.hasUnsavedEdits()) return;
    preview.loadFile(file, true);
    batchPreviewAuditioned = true;
    if (batchContent != nullptr)
        batchContent->setPreviewStatus(processed ? "Playing processed preview"
                                                 : "Playing original preview");
}

void MainComponent::stopBatchPreview()
{
    if (batchPreview != nullptr && batchPreview->isRunning())
    {
        batchPreview->cancel();
        pendingPreviewStop = true;
        return;
    }
    preview.stopPlayback();
    if (batchPreviewAuditioned && batchPreviewRestore.existsAsFile())
    {
        preview.loadFile(batchPreviewRestore, false);
        batchPreviewAuditioned = false;
    }
    if (batchPreview != nullptr)
    {
        const auto result = batchPreview->takeResult();
        juce::ignoreUnused(result);
        batchPreview.reset();
    }
    if (batchPreviewFolder != juce::File())
    {
        batchPreviewFolder.deleteRecursively();
        batchPreviewFolder = juce::File();
    }
    pendingPreviewStop = false;
}

void MainComponent::closeBatchDialog()
{
    if (isBatchRunning())
    {
        if (batchService != nullptr) batchService->cancel();
        batchClosePending = true;
    }
    if (batchPreview != nullptr && batchPreview->isRunning())
    {
        batchPreview->cancel();
        pendingPreviewStop = true;
        batchClosePending = true;
        return;
    }
    stopBatchPreview();
    if (isBatchRunning() || batchClosePending)
    {
        batchClosePending = true;
        return;
    }
    if (batchDialog != nullptr)
    {
        batchDialog->exitModalState(0);
        batchDialog = nullptr;
    }
    batchContent = nullptr;
    batchService.reset();
    batchClosePending = false;
}

void MainComponent::startSplit(const std::vector<juce::File>& files)
{
    if (files.empty() || splitter == nullptr) return;
    if (isBatchRunning())
    {
        status.setText("Wait for Batch Processing to finish.", juce::dontSendNotification);
        return;
    }
    if (isSplitRunning())
    {
        status.setText("Split is already running.", juce::dontSendNotification);
        return;
    }
    // Split only reads the sources and writes new parts; its catalog follow-up waits for an idle
    // indexer, so a paused scan or analysis is enough.
    if (! analysisAllows("Split by silence", true,
            [safe = juce::Component::SafePointer<MainComponent>(this), files]
            { if (safe != nullptr) safe->startSplit(files); }))
        return;
    if ((tagging && tagging->progress().running) || (autoTag && autoTag->progress().running)
        || isExtractRunning() || isNormalizeRunning())
    {
        status.setText("Wait for the running job to finish before splitting.", juce::dontSendNotification);
        return;
    }
    if (splitContent != nullptr)
    {
        if (splitDialog != nullptr) splitDialog->toFront(true);
        return;
    }
    auto* dialog = new SplitDialog(files, appSettings,
        [safe = juce::Component::SafePointer<MainComponent>(this), files](SplitOptions options) -> bool
        {
            if (safe == nullptr || safe->splitter == nullptr) return false;
            safe->activeSplitOptions = options;
            safe->appSettings.splitThresholdDb = options.silence.thresholdDb;
            safe->appSettings.splitMinSilenceMs = (int) options.silence.minSilenceMs;
            safe->appSettings.splitMinPartMs = (int) options.silence.minPartMs;
            safe->appSettings.splitSameFolder = options.sameFolder;
            safe->appSettings.splitMoveToTrash = options.moveToTrash;
            safe->appSettings.splitNormalize = options.normalize;
            try
            {
                safe->settingsStore.update([options](AppSettings& saved)
                {
                    saved.splitNormalize = options.normalize;
                    saved.splitThresholdDb = options.silence.thresholdDb;
                    saved.splitMinSilenceMs = (int) options.silence.minSilenceMs;
                    saved.splitMinPartMs = (int) options.silence.minPartMs;
                    saved.splitSameFolder = options.sameFolder;
                    saved.splitMoveToTrash = options.moveToTrash;
                });
            }
            catch (const std::exception&)
            {
            }
            if (! safe->splitter->start(files, options)) return false;
            safe->splitStartMs = juce::Time::getMillisecondCounter();
            safe->splitWasRunning = true;
            // The batch replaces the source selection; leaving its row selected would keep the
            // old waveform visible and the preview audible under the progress report.
            safe->clearSampleSelection();
            safe->status.setText("Splitting " + juce::String((int) files.size()) + " file(s)",
                                 juce::dontSendNotification);
            return true;
        });
    dialog->onCancel = [safe = juce::Component::SafePointer<MainComponent>(this)]
    {
        if (safe != nullptr && safe->splitter != nullptr) safe->splitter->cancel();
    };
    dialog->onOk = [safe = juce::Component::SafePointer<MainComponent>(this)]
    {
        if (safe == nullptr) return;
        if (! safe->pendingSplitScan.isEmpty())
        {
            const auto folders = safe->pendingSplitScan;
            safe->pendingSplitScan.clear();
            safe->requestSplitScan(folders);
        }
    };
    splitContent = dialog;
    auto* window = new SplitDialogWindow();
    window->setContentOwned(dialog, true);
    window->setLookAndFeel(&look);
    window->setUsingNativeTitleBar(false);
    window->setResizable(false, false);
    window->setSize(dialog->getWidth(), dialog->getHeight());
    window->centreAroundComponent(this, window->getWidth(), window->getHeight());
    window->onClose = [safe = juce::Component::SafePointer<MainComponent>(this)]
    {
        if (safe != nullptr) safe->requestCloseSplit();
    };
    splitDialog = window;
    window->enterModalState(true, nullptr, true);
}
void MainComponent::requestSplitScan(const juce::StringArray& folders)
{
    if (folders.isEmpty()) return;
    splitScanApproved = true;
    if (indexer != nullptr && ! indexer->progress().running && ! isSplitRunning())
    {
        if (indexer->startFoldersNewOnly(folders))
        {
            wasRunning = true;
            status.setText("Scanning split parts", juce::dontSendNotification);
            return;
        }
    }
    pendingSplitScan = folders;
}
void MainComponent::closeSplitDialog()
{
    if (splitDialog != nullptr)
    {
        splitDialog->exitModalState(0);
        splitDialog = nullptr;
    }
    splitContent = nullptr;
}
void MainComponent::requestCloseSplit()
{
    if (splitContent == nullptr)
    {
        closeSplitDialog();
        return;
    }
    if (splitContent->isRunning())
    {
        if (splitter != nullptr) splitter->cancel();
        return;
    }
    if (splitContent->isFinished() && splitContent->onOk) splitContent->onOk();
    closeSplitDialog();
}
bool MainComponent::isDiscDropTarget(const juce::StringArray& files)
{
    for (const auto& path : files)
        if (isDiscExtractSource(juce::File(path))) return true;
    return false;
}
bool MainComponent::isInterestedInFileDrag(const juce::StringArray& files)
{
    return isDiscDropTarget(files);
}
void MainComponent::filesDropped(const juce::StringArray& files, int, int)
{
    std::vector<juce::File> images;
    std::set<juce::String> seen;
    for (const auto& path : files)
    {
        const juce::File file(path);
        if (! isDiscExtractSource(file)) continue;
        if (seen.insert(file.getFullPathName()).second) images.push_back(file);
    }
    if (images.empty()) return;
    startExtract(images);
}
void MainComponent::startExtract(const std::vector<juce::File>& files,
                                 const std::vector<DiscExtractJob>& discJobs)
{
    const auto routing = routeExtractSources(files, discJobs);
    if ((routing.embedded.empty() && routing.jobs.empty()) || extractor == nullptr) return;
    if (isExtractRunning())
    {
        status.setText("Extract is already running.", juce::dontSendNotification);
        return;
    }
    if (! analysisAllows("Extract samples", true,
            [safe = juce::Component::SafePointer<MainComponent>(this), files, discJobs]
            { if (safe != nullptr) safe->startExtract(files, discJobs); }))
        return;
    if ((tagging && tagging->progress().running) || (autoTag && autoTag->progress().running)
        || isSplitRunning() || isNormalizeRunning())
    {
        status.setText("Wait for the running job to finish before extracting.",
                       juce::dontSendNotification);
        return;
    }
    if (extractContent != nullptr)
    {
        if (extractDialog != nullptr) extractDialog->toFront(true);
        return;
    }
    // The dialog names every image and bank in the batch; entry jobs contribute their image.
    auto dialogSources = routing.embedded;
    for (const auto& job : routing.jobs)
    {
        bool known = false;
        for (const auto& existing : dialogSources)
            if (existing == job.image) { known = true; break; }
        if (! known) dialogSources.push_back(job.image);
    }
    auto* dialog = new ExtractDialog(dialogSources, appSettings,
        [safe = juce::Component::SafePointer<MainComponent>(this), embedded = routing.embedded,
         jobs = routing.jobs](ExtractOptions options) -> bool
        {
            if (safe == nullptr || safe->extractor == nullptr) return false;
            safe->appSettings.extractSameFolder = options.sameFolder;
            safe->appSettings.extractNormalize = options.normalize;
            safe->appSettings.extractTuneToSong = options.tuneToSong;
            if (! options.sameFolder && options.outputFolder.isDirectory())
                safe->appSettings.extractOutputFolder = options.outputFolder.getFullPathName();
            try
            {
                safe->settingsStore.update([options](AppSettings& saved)
                {
                    saved.extractSameFolder = options.sameFolder;
                    saved.extractNormalize = options.normalize;
                    saved.extractTuneToSong = options.tuneToSong;
                    if (! options.sameFolder && options.outputFolder.isDirectory())
                        saved.extractOutputFolder = options.outputFolder.getFullPathName();
                });
            }
            catch (const std::exception&)
            {
            }
            if (! embedded.empty())
            {
                if (! safe->extractor->start(embedded, options)) return false;
                // A mixed selection finishes the banks first; finishExtract starts the disc batch.
                safe->pendingDiscExtract = jobs;
                safe->pendingDiscOptions = options;
            }
            else if (! jobs.empty() && ! safe->extractor->startDisc(jobs, options)) return false;
            safe->extractStartMs = juce::Time::getMillisecondCounter();
            safe->extractWasRunning = true;
            safe->status.setText("Extracting samples from "
                + juce::String((int) (embedded.size() + jobs.size())) + " source(s)",
                juce::dontSendNotification);
            return true;
        });
    dialog->onCancel = [safe = juce::Component::SafePointer<MainComponent>(this)]
    {
        if (safe != nullptr && safe->extractor != nullptr) safe->extractor->cancel();
    };
    extractContent = dialog;
    auto* window = new ExtractDialogWindow();
    window->setContentOwned(dialog, true);
    window->setLookAndFeel(&look);
    window->setUsingNativeTitleBar(false);
    window->setResizable(false, false);
    window->setSize(dialog->getWidth(), dialog->getHeight());
    window->centreAroundComponent(this, window->getWidth(), window->getHeight());
    window->onClose = [safe = juce::Component::SafePointer<MainComponent>(this)]
    {
        if (safe != nullptr) safe->requestCloseExtract();
    };
    extractDialog = window;
    window->enterModalState(true, nullptr, true);
}
void MainComponent::finishExtract()
{
    if (extractor == nullptr) return;
    if (! pendingDiscExtract.empty())
    {
        const auto jobs = std::move(pendingDiscExtract);
        pendingDiscExtract.clear();
        if (extractor->progress().cancelled)
        {
            juce::StringArray skipped;
            for (const auto& job : jobs) skipped.add(job.image.getFileName());
            pendingExtractProblems.add("Cancelled before the images were read: "
                                       + skipped.joinIntoString(", "));
        }
        else if (extractor->startDisc(jobs, pendingDiscOptions))
        {
            // Keep the bank phase's report; the disc phase appends its own.
            pendingExtractSummary = status.getText();
            extractStartMs = juce::Time::getMillisecondCounter();
            extractWasRunning = true;
            status.setText("Extracting samples from " + juce::String((int) jobs.size())
                + " image(s)", juce::dontSendNotification);
            return;
        }
    }
    const auto results = extractor->results();
    const auto progress = extractor->progress();
    int filesDone = 0, filesFailed = 0;
    juce::StringArray details;
    for (const auto& result : results)
    {
        if (result.state == ExtractFileState::extracted) ++filesDone;
        else ++filesFailed;
        if (result.state == ExtractFileState::failed && result.error.isNotEmpty())
            details.add(result.source.getFileName() + ": " + result.error);
        for (const auto& problem : result.problems)
            details.add(result.source.getFileName() + ": " + problem);
    }
    details.addArray(pendingExtractProblems);
    pendingExtractProblems.clear();
    if (filesFailed == 0 && details.isEmpty())
        details.add("No problems.");
    juce::String summary = "Extracted " + juce::String(progress.written) + " sample(s) from "
        + juce::String(filesDone) + " of " + juce::String((int) results.size()) + " file(s).";
    summary << "\nSkipped " << juce::String(progress.skippedEmpty) << " empty and "
            << juce::String(progress.skippedSilent) << " silent.";
    if (progress.skippedUnsupported > 0)
        summary << "  " << juce::String(progress.skippedUnsupported) << " unsupported file(s) skipped.";
    if (progress.failed > 0)
        summary << "  " << juce::String(progress.failed) << " problem(s), see details.";
    if (progress.cancelled)
        summary << "\nCancelled; finished samples were kept.";
    juce::StringArray outputFolders;
    for (const auto& result : results)
        if (result.written > 0 && result.folder.isDirectory()
            && (result.state == ExtractFileState::extracted
                || result.state == ExtractFileState::cancelled)
            && ! outputFolders.contains(result.folder.getFullPathName()))
            outputFolders.add(result.folder.getFullPathName());
    pendingExtractScan.addArray(outputFolders);
    if (pendingExtractSummary.isNotEmpty())
    {
        summary = pendingExtractSummary + "\n" + summary;
        pendingExtractSummary.clear();
    }
    status.setText(summary.replace("\n", " "), juce::dontSendNotification);
    status.setTooltip(details.joinIntoString("\n"));
    if (extractContent != nullptr)
        extractContent->showFinished(summary, details.joinIntoString("\n"),
            progress.cancelled ? progress.fraction : 1.0);
}
void MainComponent::closeExtractDialog()
{
    if (extractDialog != nullptr)
    {
        extractDialog->exitModalState(0);
        extractDialog = nullptr;
    }
    extractContent = nullptr;
}
void MainComponent::requestCloseExtract()
{
    if (extractContent == nullptr)
    {
        closeExtractDialog();
        return;
    }
    if (extractContent->isRunning())
    {
        if (extractor != nullptr) extractor->cancel();
        return;
    }
    closeExtractDialog();
}
void MainComponent::finishSplit()
{
    if (splitter == nullptr || readRepo == nullptr) return;
    const auto results = splitter->results();
    const auto progress = splitter->progress();
    const bool batchCancelled = progress.cancelled;
    int splitCount = 0, partCount = 0, failedCount = 0, skippedCount = 0;
    int noSplitCount = 0, cancelledCount = 0, droppedCount = 0;
    juce::StringArray outputFolders, problems;
    std::vector<SplitFileResult> deletable;
    for (const auto& result : results)
    {
        partCount += (int) result.parts.size();
        for (const auto& part : result.parts)
        {
            const auto folder = part.getParentDirectory().getFullPathName();
            if (! outputFolders.contains(folder)) outputFolders.add(folder);
        }
        switch (result.state)
        {
            case SplitFileState::split: ++splitCount; break;
            case SplitFileState::failed: ++failedCount; break;
            case SplitFileState::skipped: ++skippedCount; break;
            case SplitFileState::noSplit: ++noSplitCount; break;
            case SplitFileState::cancelled: ++cancelledCount; break;
        }
        if (result.discardedShortParts > 0)
        {
            ++droppedCount;
            problems.add(result.source.getFileName() + ": "
                + juce::String(result.discardedShortParts) + " short fragment(s) dropped");
        }
        if (result.state != SplitFileState::split)
            problems.add(result.source.getFileName()
                + (result.error.isNotEmpty() ? ": " + result.error : juce::String()));
        if (deletionAllowed(result, batchCancelled)) deletable.push_back(result);
    }

    juce::StringArray moved, moveFailed;
    if (activeSplitOptions.moveToTrash && ! batchCancelled)
    {
        for (const auto& result : deletable)
        {
            const auto source = result.source;
            if (! source.existsAsFile())
            {
                moveFailed.add(source.getFileName());
                continue;
            }
            const auto sidecars = splitSidecarPaths(source);
            if (! source.moveToTrash())
            {
                moveFailed.add(source.getFileName());
                continue;
            }
            moved.add(source.getFullPathName());
            for (const auto& sidecarPath : sidecars)
            {
                juce::File sidecar(sidecarPath);
                if (sidecar.existsAsFile() && ! sidecar.moveToTrash())
                    moveFailed.add(sidecar.getFileName());
            }
        }
        if (! moved.isEmpty())
        {
            try
            {
                writeRepo->removeByPaths(moved);
                writeRepo->refreshDuplicates();
                reloadLibrary();
                requestFolderTreeRefresh();
                refreshFavorites();
            }
            catch (const std::exception& e)
            {
                problems.add(juce::String::fromUTF8(e.what()));
            }
        }
        if (! moveFailed.isEmpty())
            problems.add("Could not move to Trash: " + moveFailed.joinIntoString(", "));
    }

    juce::String summary = "Split: " + juce::String(splitCount) + " file(s), "
        + juce::String(partCount) + " part(s)";
    if (failedCount > 0) summary += "  |  failed " + juce::String(failedCount);
    if (skippedCount > 0) summary += "  |  skipped " + juce::String(skippedCount);
    if (noSplitCount > 0) summary += "  |  no silence " + juce::String(noSplitCount);
    if (cancelledCount > 0) summary += "  |  cancelled " + juce::String(cancelledCount);
    if (droppedCount > 0) summary += "  |  dropped fragments " + juce::String(droppedCount);
    if (batchCancelled) summary += "  |  cancelled";
    if (! moved.isEmpty()) summary += "  |  moved to Trash " + juce::String(moved.size());
    status.setText(summary, juce::dontSendNotification);
    status.setTooltip(summary + (problems.isEmpty() ? juce::String()
                                                    : "\n" + problems.joinIntoString("\n")));
    // The new parts must reach the catalog even when the summary dialog is still open: otherwise
    // the list looks unchanged until the user leaves and re-enters the folder. The scan runs in
    // the background; requestSplitScan leaves it pending while another job owns the indexer.
    pendingSplitScan = outputFolders;
    splitScanApproved = false;
    if (! outputFolders.isEmpty())
    {
        // A split supersedes a queued extract scan; its sources may have just been moved away.
        pendingExtractScan.clear();
        pendingDiscExtract.clear();
        pendingExtractSummary.clear();
        pendingExtractProblems.clear();
        requestSplitScan(outputFolders);
    }
    if (splitContent != nullptr)
    {
        const double fraction = progress.total > 0
            ? juce::jlimit(0.0, 1.0, (double) progress.processed / (double) progress.total) : 1.0;
        splitContent->showFinished(summary + (problems.isEmpty() ? juce::String()
                                                                 : "\n\n" + problems.joinIntoString("\n")),
                                   fraction);
    }
    // Parts written before this point may already be indexed; show them without waiting for the
    // scan to finish.
    reloadLibrary();
}
void MainComponent::showFolderContextMenu(const juce::String& path, bool allSamples,
                                          juce::Point<int> screenPosition)
{
    // All samples is a virtual node: no filesystem open and no implicit whole-catalog bulk work.
    if (allSamples || path.isEmpty() || readRepo == nullptr) return;
    const juce::File folder(path);
    const bool available = folder.isDirectory();
    int catalogued = 0;
    try
    {
        catalogued = readRepo->countInFolder(path);
        allTags = readRepo->tags();
    }
    catch (const std::exception& e) { status.setText(juce::String::fromUTF8(e.what()),juce::dontSendNotification); return; }
#if JUCE_MAC
    const juce::String openLabel = "Open in Finder";
#elif JUCE_WINDOWS
    const juce::String openLabel = "Open in Explorer";
#else
    const juce::String openLabel = "Open folder";
#endif
    juce::PopupMenu menu, tagMenu, genreMenu;
    menu.setLookAndFeel(&look);
    for (int i = 0; i < (int) allTags.size(); ++i)
        tagMenu.addItem(7100 + i, allTags[(size_t) i].name, true, false,
                        tagDotImage(allTags[(size_t) i].colour));
    if (allTags.empty()) tagMenu.addSectionHeader("No tags yet");
    const bool exactFavorite = isFavoriteFolder(path);
    const bool underFavorite = isUnderFavoriteFolder(path);
    const juce::String favoriteLabel = exactFavorite ? "Remove from favorites"
        : (underFavorite ? "Already in favorites" : "Add to favorites");
    menu.addItem(7001, openLabel, available);
    menu.addItem(7006, favoriteLabel, exactFavorite || ! underFavorite);
    const bool exactIgnore = readRepo->hasExactIgnore(path);
    const bool inheritedIgnore = ! exactIgnore && readRepo->isIgnored(path);
    menu.addItem(7007, inheritedIgnore ? "Ignored with parent folder" : "Ignore",
                 ! inheritedIgnore, exactIgnore);
    // Only a registered root gets a display name; subfolders keep the real folder name.
    const bool isRoot = readRepo->roots().contains(path);
    if (isRoot) menu.addItem(7011, "Rename folder...");
    menu.addSeparator();
    const bool splitBusy = isSplitRunning();
    // A disc image is shown as a container node in the tree, so its own menu offers Extract too.
    const bool isDiscImage = isDiscExtractSource(folder);
    const bool extractIdle = extractor != nullptr && ! isExtractRunning()
        && (indexer == nullptr || ! indexer->progress().running)
        && (smart == nullptr || ! smart->progress().running)
        && (tagging == nullptr || ! tagging->progress().running)
        && (autoTag == nullptr || ! autoTag->progress().running)
        && ! splitBusy;
    const bool autoTagIdle = autoTag != nullptr && ! autoTag->progress().running
        && (indexer == nullptr || ! indexer->progress().running)
        && (smart == nullptr || ! smart->progress().running)
        && (tagging == nullptr || ! tagging->progress().running)
        && ! splitBusy;
    // The plugin browses and tags; jobs that own the catalog or rewrite files stay in the app.
    if (embeddedUi)
    {
        menu.addSectionHeader("Scan, analyze, extract and batch run in the SONICATOR app");
    }
    else
    {
        if (isDiscImage) menu.addItem(7012, "Extract samples...", extractIdle);
        menu.addItem(7013, "Batch Processing...", catalogued > 0 && ! isBatchRunning() && ! splitBusy);
        menu.addItem(7015, "Find duplicates...", catalogued > 1 && ! splitBusy && ! isBatchRunning());
        menu.addItem(7002, "Scan folder", indexer != nullptr && ! indexer->progress().running && ! splitBusy);
        menu.addItem(7003, "Analyze smart metadata (" + juce::String(catalogued) + ")",
                     smart != nullptr && ! smart->progress().running && ! splitBusy
                         && (indexer == nullptr || ! indexer->progress().running));
        menu.addItem(7016, "Spectral analysis (" + juce::String(catalogued) + ")...",
                     catalogued > 0 && ! splitBusy && ! isBatchRunning()
                         && (spectralAnalysis == nullptr || ! spectralAnalysis->isRunning()));
        menu.addItem(7004, "AutoTag folder...", autoTagIdle && catalogued > 0);
    }
    menu.addSeparator();
    if (appSettings.roundTagMenu)
        menu.addItem(7014, "Tag folder...", catalogued > 0 && ! splitBusy);
    else
        menu.addSubMenu("Tag folder...", tagMenu, ! allTags.empty() && catalogued > 0 && ! splitBusy);
    const auto genreNames = readRepo->genreNames();
    for (int i = 0; i < genreNames.size(); ++i)
        genreMenu.addItem(genreMenuFirstId + i, genreNames[i]);
    genreMenu.addSeparator();
    genreMenu.addItem(7090, "+ Custom genre...");
    menu.addSubMenu("Set genre...", genreMenu,
                   catalogued > 0 && writeRepo != nullptr && normalizeJobIdle() && ! isNormalizeRunning());
    menu.addItem(7005, "Reset tags...", ! embeddedUi && catalogued > 0 && autoTagIdle
                                            && autoTagDialog == nullptr);
    const bool removable = available || readRepo->roots().contains(path);
    if (removable)
    {
        menu.addSeparator();
        menu.addItem(7008, "Remove from library...", autoTagIdle && ! embeddedUi);
    }
    const juce::String folderPath = path;
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&folders).withTargetScreenArea(
                           { screenPosition.x, screenPosition.y, 1, 1 })
                           .withMaximumNumColumns(1)
                           .withPreferredPopupDirection(juce::PopupMenu::Options::PopupDirection::downwards),
        [safe = juce::Component::SafePointer<MainComponent>(this), folderPath, catalogued, exactIgnore,
         catalogPath = dbPath, genreNames, screenPosition](int result)
        {
            if (safe == nullptr || result == 0 || safe->dbPath != catalogPath) return;
            if (result == 7014)
            {
                safe->showRoundTagMenuForFolder(folderPath,
                                                safe->getLocalPoint(nullptr, screenPosition));
                return;
            }
            if (safe->folderGenreWork.valid() && result != 7001 && result != 7006 && result != 7011)
            {
                safe->status.setText("Wait for folder genre assignment to finish.", juce::dontSendNotification);
                return;
            }
            if (safe->isSplitRunning() && result != 7001 && result != 7006 && result != 7007
                && result != 7011)
            {
                safe->status.setText("Wait for Split to finish.", juce::dontSendNotification);
                return;
            }
            if (safe->isBatchRunning() && result != 7001 && result != 7006)
            {
                safe->status.setText("Wait for Batch Processing to finish.",
                                     juce::dontSendNotification);
                return;
            }
            if (result == 7013)
            {
                batch::SourceSelection batchSelection;
                batchSelection.kind = batch::SourceSelection::Kind::folder;
                batchSelection.folder = folderPath;
                safe->openBatchProcessing(batchSelection);
                return;
            }
            if (result == 7016)
            {
                std::vector<juce::File> spectralFiles;
                if (safe->readRepo != nullptr)
                {
                    for (const auto& record : safe->readRepo->pageInFolder(folderPath, 100000))
                    {
                        const juce::File file(record.filePath);
                        if (file.existsAsFile()) spectralFiles.push_back(file);
                    }
                }
                safe->startSpectralAnalysis(std::move(spectralFiles));
                return;
            }
            try
            {
                if (result >= genreMenuFirstId)
                {
                    const int index = result - genreMenuFirstId;
                    if (index < genreNames.size()) safe->setGenreForFolder(folderPath, genreNames[index]);
                    return;
                }
                if (result == 7090)
                {
                    dialogs::showPrompt(*safe, "Set folder genre", "Genre name", {},
                        [safe, folderPath, catalogPath](const juce::String& name)
                        {
                            if (safe != nullptr && safe->dbPath == catalogPath)
                                safe->setGenreForFolder(folderPath, name);
                        });
                    return;
                }
                if (result == 7015)
                {
                    safe->openDuplicateCleanup(folderPath);
                }
                else if (result == 7012)
                {
                    const juce::File image(folderPath);
                    if (isDiscExtractSource(image)) safe->startExtract({ image });
                }
                else if (result == 7011)
                {
                    safe->promptRenameFolder(folderPath);
                }
                else if (result == 7007)
                {
                    if (safe->writeRepo == nullptr) return;
                    if (exactIgnore) safe->writeRepo->unignorePath(folderPath);
                    else safe->writeRepo->ignorePath(folderPath, true);
                    safe->ensureSelectedFolderVisible();
                    safe->reloadLibrary();
                    safe->requestFolderTreeRefresh();
                    safe->refreshFavorites();
                    safe->status.setText(exactIgnore ? "Ignoring removed for " + folderPath
                                                     : "Ignoring " + folderPath,
                                         juce::dontSendNotification);
                }
                if (result >= 7100)
                {
                    const size_t index = (size_t) (result - 7100);
                    if (index >= safe->allTags.size() || safe->readRepo == nullptr) return;
                    safe->confirmTagFolder(folderPath, safe->allTags[index].id);
                }
                else if (result == 7006)
                {
                    safe->toggleFavoriteFolder(folderPath);
                }
                else if (result == 7005)
                {
                    safe->resetTagsFolder(folderPath, catalogued);
                }
                else if (result == 7001)
                {
                    const juce::File target(folderPath);
                    if (! target.isDirectory())
                        safe->status.setText("Folder is not available: " + folderPath, juce::dontSendNotification);
                    else if (! juce::Process::openDocument(folderPath, {}))
                        safe->status.setText("Could not open " + folderPath, juce::dontSendNotification);
                }
                else if (result == 7002)
                {
                    if (safe->indexer == nullptr || safe->indexer->progress().running)
                    {
                        safe->status.setText("Another scan is already running.", juce::dontSendNotification);
                        return;
                    }
                    if (safe->smart != nullptr && safe->smart->progress().running)
                    {
                        safe->status.setText("Wait for smart analysis to finish before scanning.", juce::dontSendNotification);
                        return;
                    }
                    if (safe->indexer->startSubtree(folderPath))
                        safe->status.setText("Scanning " + folderPath, juce::dontSendNotification);
                    else
                        safe->status.setText("Scan could not start.", juce::dontSendNotification);
                }
                else if (result == 7003)
                {
                    if (safe->smart == nullptr || safe->smart->progress().running)
                    {
                        safe->status.setText("Smart analysis is already running.", juce::dontSendNotification);
                        return;
                    }
                    if (safe->indexer != nullptr && safe->indexer->progress().running)
                    {
                        safe->status.setText("Wait for the scan to finish before analyzing metadata.", juce::dontSendNotification);
                        return;
                    }
                    if (safe->smart->startFolder(folderPath))
                        safe->status.setText("Analyzing metadata for " + juce::String(catalogued)
                            + " catalogued file(s)", juce::dontSendNotification);
                    else
                        safe->status.setText("Smart analysis could not start.", juce::dontSendNotification);
                }
                else if (result == 7004)
                {
                    safe->startAutoTagFolder(folderPath);
                }
                else if (result == 7008)
                {
                    dialogs::showAlert(*safe, "Remove folder from library",
                        "Folder:\n" + folderPath
                            + "\n\nCatalogued files: " + juce::String(catalogued)
                            + "\n\nTheir catalog rows, tags and analysis are removed from this catalog."
                            + "\nFiles on disk are not changed."
                            + (safe->readRepo->roots().contains(folderPath)
                                   ? "\n\nThe folder is also removed from the library roots."
                                   : "\n\nA later scan of the parent folder will not bring it back."),
                        { { "Remove", 1 }, { "Cancel", 0 } },
                        [safe, folderPath](int choice)
                        {
                            if (safe != nullptr && choice == 1) safe->removeFolderFromLibrary(folderPath);
                        }, juce::MessageBoxIconType::WarningIcon);
                }
            }
            catch (const std::exception& e)
            {
                safe->status.setText(juce::String::fromUTF8(e.what()), juce::dontSendNotification);
            }
        });
}
void MainComponent::setGenreForFolder(const juce::String& path, const juce::String& genre,
                                       bool replaceManual)
{
    const auto name = genre.trim();
    if (path.isEmpty() || name.isEmpty() || writeRepo == nullptr) return;
    if (! normalizeJobIdle() || isNormalizeRunning())
    {
        status.setText("Wait for the current catalog operation to finish before setting genre.",
                       juce::dontSendNotification);
        return;
    }
    try
    {
        folderGenrePath = path;
        folderGenreName = name;
        folderGenreReplacement = replaceManual;
        folderGenreWork = std::async(std::launch::async,
            [repo = writeRepo.get(), path, name, replaceManual]
            {
                return repo->setGenreInFolder(path, name, replaceManual);
            });
        status.setText("Setting genre to " + name + " in " + path, juce::dontSendNotification);
    }
    catch (const std::exception& e)
    {
        status.setText("Could not set folder genre: " + juce::String::fromUTF8(e.what()),
                       juce::dontSendNotification);
    }
}
void MainComponent::finishFolderGenre()
{
    if (! folderGenreWork.valid()
        || folderGenreWork.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    try
    {
        const auto result = folderGenreWork.get();
        if (result.conflicts > 0 && ! folderGenreReplacement)
        {
            const auto path = folderGenrePath;
            const auto name = folderGenreName;
            dialogs::showAlert(*this, "Replace manual genres?",
                "Folder:\n" + path + "\n\nIncludes every subfolder."
                    + "\nCatalogued files: " + juce::String(result.total)
                    + "\nDifferent manual genres (including Unknown): " + juce::String(result.conflicts)
                    + "\n\nSet genre to " + name + " for these files?"
                    + "\nNo changes have been made. Original audio files are not changed.",
                { { "Replace", 1 }, { "Cancel", 0 } },
                [safe = juce::Component::SafePointer<MainComponent>(this), catalogPath = dbPath, path, name](int choice)
                {
                    if (safe != nullptr && safe->dbPath == catalogPath && choice == 1)
                        safe->setGenreForFolder(path, name, true);
                }, juce::MessageBoxIconType::WarningIcon);
            return;
        }
        reloadLibrary();
        status.setText("Genre set to " + folderGenreName + " for " + juce::String(result.assigned)
            + " sample(s) in folder and subfolders (" + juce::String(result.total) + " catalogued).",
            juce::dontSendNotification);
    }
    catch (const std::exception& e)
    {
        status.setText("Could not set folder genre: " + juce::String::fromUTF8(e.what()),
                       juce::dontSendNotification);
    }
}
void MainComponent::setIgnoreSymlinks(const bool enabled)
{
    filter.ignoreSymlinks = enabled;
    ignoreSymlinks.setToggleState(enabled, juce::dontSendNotification);
    appSettings.ignoreSymlinks = enabled;
    try
    {
        settingsStore.update([enabled](AppSettings& saved) { saved.ignoreSymlinks = enabled; });
    }
    catch (...) {}
    pageOffset = 0;
    // Older catalogs have no per-row flag yet; the one-time pass fills it before the view reloads.
    if (enabled && readRepo != nullptr && ! readRepo->symlinkFlagsComplete())
        startSymlinkFlagRefresh();
    else
        reloadLibrary();
}
void MainComponent::startSymlinkFlagRefresh()
{
    if (readRepo == nullptr || symlinkFlagWork.valid()) return;
    cancelSymlinkFlags = false;
    status.setText("Checking symbolic links...", juce::dontSendNotification);
    status.setTooltip({});
    // The future is joined (and cancelled) in the destructor, so the repository cannot outlive it.
    symlinkFlagWork = std::async(std::launch::async, [this, repo = readRepo.get()]
    {
        return repo->refreshSymlinkFlags([this] { return cancelSymlinkFlags.load(); });
    });
}
void MainComponent::finishSymlinkFlagRefresh()
{
    if (! symlinkFlagWork.valid()
        || symlinkFlagWork.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;
    const bool complete = symlinkFlagWork.get();
    status.setText(complete ? "Symbolic links checked"
                            : "Symbolic link check stopped before it finished",
                   juce::dontSendNotification);
    pageOffset = 0;
    reloadLibrary();
}
std::vector<SampleRecord> MainComponent::normalizeCandidates(
    const std::vector<SampleRecord>& selection)
{
    std::vector<SampleRecord> records;
    if (readRepo == nullptr) return records;
    std::vector<IgnoredPath> ignoredPaths;
    try { ignoredPaths = readRepo->ignoredPaths(); } catch (...) {}
    for (const auto& record : selection)
    {
        const juce::File file(record.filePath);
        if (! file.existsAsFile()) continue;
        if (isPathIgnored(record.filePath, ignoredPaths)) continue;
        if (normalizeSkipReason(file).isNotEmpty()) continue;
        // The open editor owns a file with unsaved changes: rewriting it from the library would
        // silently throw those edits away.
        if (preview.hasUnsavedEditsFor(file)) continue;
        records.push_back(record);
    }
    return records;
}

void MainComponent::startNormalizeSelected(const std::vector<SampleRecord>& selection)
{
    if (normalizer == nullptr) return;
    if (isBatchRunning())
    {
        status.setText("Wait for Batch Processing to finish.", juce::dontSendNotification);
        return;
    }
    if (isNormalizeRunning())
    {
        status.setText("Normalize is already running.", juce::dontSendNotification);
        return;
    }
    // Normalize rewrites files a paused scan may hold open, so only a finished or cancelled
    // analysis lets it start.
    if (! analysisAllows("Normalize", false,
            [safe = juce::Component::SafePointer<MainComponent>(this), selection]
            { if (safe != nullptr) safe->startNormalizeSelected(selection); }))
        return;
    // Cancel acts on whichever job is running; keeping one writer job at a time makes that
    // unambiguous and keeps the catalog reanalysis from racing another writer.
    if (! normalizeJobIdle())
    {
        status.setText("Wait for the running job to finish.", juce::dontSendNotification);
        return;
    }
    const auto records = normalizeCandidates(selection);
    if (records.empty())
    {
        status.setText("No selected file can be normalized.", juce::dontSendNotification);
        return;
    }
    std::vector<juce::File> files;
    std::map<juce::String, int64_t> sampleIds;
    int overwritable = 0;
    for (const auto& record : records)
    {
        const juce::File file(record.filePath);
        files.push_back(file);
        sampleIds[file.getFullPathName()] = record.id;
        if (edit::canOverwriteInPlace(file)) ++overwritable;
    }
    auto safe = juce::Component::SafePointer<MainComponent>(this);
    const auto begin = [safe, files, sampleIds](bool overwrite)
    {
        if (safe != nullptr) safe->beginNormalize(files, sampleIds, overwrite);
    };
    if (appSettings.alwaysOverwriteOriginalFiles)
    {
        // Settings -> Editing: no confirmation, the safe write path still replaces atomically and
        // formats without a writer still get a copy. Permanent disposal is the one exception: the
        // person asked for the overwrite shortcut, never for an unannounced irreversible delete.
        if (appSettings.overwriteOriginalDisposal == files::DisposalPolicy::DeletePermanently)
        {
            dialogs::showAlert(*this, "Normalize and delete the originals?",
                juce::String((int) files.size()) + " file(s) will be replaced in place and the "
                    "previous copies deleted permanently. This cannot be recovered through Trash.",
                { { "Overwrite and delete", 1 }, { "Cancel", 0 } },
                [begin](int id) { if (id == 1) begin(true); },
                juce::MessageBoxIconType::WarningIcon);
            return;
        }
        begin(true);
        return;
    }
    const auto count = juce::String((int) files.size());
    juce::String message = count + (files.size() == 1 ? " file" : " files") + " to -0.1 dBFS.";
    if (overwritable > 0)
        message += " " + juce::String(overwritable)
            + " can be replaced in place; the others are saved as a new copy next to the original.";
    else
        message += " None of these formats can be replaced in place; every result is a new copy.";
    if (records.size() < selection.size())
        message += " " + juce::String((int) (selection.size() - records.size()))
            + " selected item(s) are skipped because they cannot be normalized here.";
    const auto title = "Normalize " + count + (files.size() == 1 ? " file?" : " files?");
    if (overwritable > 0)
        dialogs::showAlert(*this, title, message,
            { { "Overwrite originals", 1 }, { "Save copies", 2 }, { "Cancel", 0 } },
            [begin](int id)
            {
                if (id == 1) begin(true);
                else if (id == 2) begin(false);
            });
    else
        dialogs::showAlert(*this, title, message,
            { { "Save copies", 2 }, { "Cancel", 0 } },
            [begin](int id) { if (id == 2) begin(false); });
}

bool MainComponent::analysisBlocks(bool pauseIsEnough) const
{
    const auto blocks = [pauseIsEnough](bool running, bool paused)
    {
        return running && ! (pauseIsEnough && paused);
    };
    const auto ip = indexer ? indexer->progress() : IndexingProgress {};
    const auto sp = smart ? smart->progress() : SmartProgress {};
    return blocks(ip.running, ip.paused) || blocks(sp.running, sp.paused);
}

void MainComponent::runWhenAnalysisStops(std::function<void()> retry, int attemptsLeft)
{
    // A cancelled worker stops at its next checkpoint; poll instead of joining on the message
    // thread, then run the operation once the job has really ended.
    if (! analysisBlocks(false)) { if (retry) retry(); return; }
    if (attemptsLeft <= 0)
    {
        status.setText("The job is still stopping; try again in a moment.", juce::dontSendNotification);
        return;
    }
    juce::Timer::callAfterDelay(100, [safe = juce::Component::SafePointer<MainComponent>(this),
                                      retry, attemptsLeft]
    {
        if (safe != nullptr) safe->runWhenAnalysisStops(retry, attemptsLeft - 1);
    });
}

bool MainComponent::analysisAllows(const juce::String& operation, bool pauseIsEnough,
                                   std::function<void()> retry)
{
    if (! analysisBlocks(pauseIsEnough)) return true;
    const bool scanning = indexer && indexer->progress().running;
    const juce::String job = scanning ? "the library scan" : "smart analysis";
    juce::String message = operation + " is unavailable while " + job + " is running.";
    message += pauseIsEnough
        ? " Pause it to continue, or cancel it. A paused job resumes from Pause."
        : " Cancel it to continue, or wait until it finishes.";
    auto safe = juce::Component::SafePointer<MainComponent>(this);
    const auto choose = [safe, scanning, retry](int id)
    {
        if (safe == nullptr || id == 0) return;
        if (id == 1)
        {
            if (scanning && safe->indexer) safe->indexer->setPaused(true);
            else if (safe->smart) safe->smart->setPaused(true);
            if (retry) retry();
            return;
        }
        if (scanning && safe->indexer) safe->indexer->cancel();
        else if (safe->smart) safe->smart->cancel();
        safe->status.setText("Cancelling...", juce::dontSendNotification);
        safe->runWhenAnalysisStops(retry, 100);
    };
    const auto title = operation + " is unavailable";
    const auto cancelLabel = scanning ? juce::String("Cancel scan") : juce::String("Cancel analysis");
    if (pauseIsEnough)
        dialogs::showAlert(*this, title, message,
            { { scanning ? "Pause scan" : "Pause analysis", 1 }, { cancelLabel, 2 }, { "Close", 0 } },
            choose);
    else
        dialogs::showAlert(*this, title, message, { { cancelLabel, 2 }, { "Close", 0 } }, choose);
    return false;
}

bool MainComponent::normalizeJobIdle() const
{
    return ! folderGenreWork.valid() && ! (indexer && indexer->progress().running)
        && ! (smart && smart->progress().running)
        && ! (tagging && tagging->progress().running)
        && ! (autoTag && autoTag->progress().running)
        && ! isSplitRunning() && ! isExtractRunning();
}

void MainComponent::beginNormalize(const std::vector<juce::File>& files,
                                   const std::map<juce::String, int64_t>& sampleIds,
                                   bool overwriteOriginals)
{
    if (normalizer == nullptr || files.empty()) return;
    // The timer may not have noticed that the previous batch ended; finish it now so its catalog
    // rows and copies are not lost when this run replaces the service results.
    if (normalizeWasRunning && ! normalizer->progress().running)
    {
        finishNormalize();
        // The failed start below must not let the timer finish the same batch a second time.
        normalizeWasRunning = false;
        normalizeResults.clear();
    }
    NormalizeOptions options;
    options.overwriteOriginals = overwriteOriginals;
    options.originalDisposal = appSettings.overwriteOriginalDisposal;
    if (! normalizer->start(files, options))
    {
        status.setText("Normalize could not start.", juce::dontSendNotification);
        return;
    }
    normalizeSampleIds = sampleIds;
    normalizeResults.clear();
    normalizeWasRunning = true;
    status.setText("Normalizing " + juce::String((int) files.size()) + " file(s) to -0.1 dBFS...",
                   juce::dontSendNotification);
}

void MainComponent::finishNormalize()
{
    if (normalizer == nullptr) return;
    const auto progress = normalizer->progress();
    normalizeResults = normalizer->results();
    juce::String text = (progress.cancelled ? "Normalize cancelled" : "Normalized")
        + juce::String(" ") + juce::String(progress.normalized) + "/" + juce::String(progress.total)
        + " file(s)";
    if (progress.unchanged > 0) text += ", unchanged " + juce::String(progress.unchanged);
    if (progress.skipped > 0) text += ", skipped " + juce::String(progress.skipped);
    if (progress.failed > 0) text += ", failed " + juce::String(progress.failed);
    juce::StringArray problems;
    for (const auto& result : normalizeResults)
        if (result.state == NormalizeFileState::failed || result.state == NormalizeFileState::skipped)
            problems.add(result.source.getFileName() + ": " + result.error);
        else if (result.state == NormalizeFileState::normalized && result.warning.isNotEmpty())
            problems.add(result.source.getFileName() + ": " + result.warning);
    status.setText(text, juce::dontSendNotification);
    status.setTooltip(problems.isEmpty() ? text : problems.joinIntoString("\n"));
    // The open preview may be showing a file that was just rewritten; reload it so the waveform
    // and the reader agree with the bytes on disk. Files with unsaved edits were skipped above.
    for (const auto& result : normalizeResults)
        if (result.replacedOriginal
            && (preview.loadedFilePath() == result.source
                || preview.loadedFilePath() == result.destination))
        {
            preview.loadFile(result.destination, false);
            break;
        }
    reloadLibrary();
    syncNormalizedCatalog();
}

bool MainComponent::isProtectedExportPath(const juce::File& file) const
{
    // Exports (selection and drag caches) may be referenced by DAW projects. They are project
    // media, not cleanup candidates.
    if (file.isAChildOf(AudioExporter::defaultExportFolder())) return true;
    const auto testDirectory = AppSettings::testSettingsDirectory();
    return testDirectory != juce::File() && file.isAChildOf(testDirectory.getChildFile("exports"));
}
void MainComponent::deleteFilesFromDisk(std::vector<SampleRecord> records)
{
    if (records.empty() || writeRepo == nullptr || readRepo == nullptr || embeddedUi) return;
    if (isSplitRunning() || isExtractRunning() || isBatchRunning() || isNormalizeRunning()
        || cleanupCatalogWork.valid()
        || (indexer && indexer->progress().running) || (smart && smart->progress().running)
        || (tagging && tagging->progress().running) || (autoTag && autoTag->progress().running))
    {
        status.setText("Wait for the running job to finish before deleting files.",
                       juce::dontSendNotification);
        return;
    }
    for (const auto& record : records)
        if (preview.hasUnsavedEditsFor(juce::File(record.filePath)))
        {
            status.setText("A selected file has unsaved edits. Save or discard them first.",
                           juce::dontSendNotification);
            return;
        }
    juce::int64 bytes = 0;
    juce::StringArray lines;
    for (const auto& record : records)
    {
        bytes += juce::File(record.filePath).getSize();
        if (lines.size() < 12) lines.add(record.filePath);
    }
    juce::String message = juce::String((int) records.size())
        + (records.size() == 1 ? " file" : " files") + ", "
        + juce::File::descriptionOfSizeInBytes(bytes) + ":\n\n"
        + lines.joinIntoString("\n");
    if ((int) records.size() > lines.size())
        message += "\nand " + juce::String((int) records.size() - lines.size()) + " more";
    message += "\n\nThey will be moved to Trash / Recycle Bin, not just removed from the library. "
               "Their catalog rows, tags and analysis are removed. DAW projects and other "
               "applications that reference these files will lose them.";
    const auto title = "Delete " + juce::String((int) records.size())
        + (records.size() == 1 ? " file" : " files") + " from disk?";
    dialogs::showAlert(*this, title, message,
        { { "Move to Trash", 1 }, { "Cancel", 0 } },
        [safe = juce::Component::SafePointer<MainComponent>(this), records](int id)
        {
            if (safe != nullptr && id == 1) safe->performDeleteFromDisk(records);
        }, juce::MessageBoxIconType::WarningIcon);
}
void MainComponent::performDeleteFromDisk(std::vector<SampleRecord> records)
{
    if (writeRepo == nullptr) return;
    juce::StringArray trashedPaths, failed, hashes;
    juce::int64 bytes = 0;
    for (const auto& record : records)
    {
        const juce::File file(record.filePath);
        if (preview.loadedFilePath() == file) preview.clearSource();
        const auto outcome = files::disposeFile(file, files::DisposalPolicy::Trash);
        if (! outcome.ok)
        {
            failed.add(outcome.error);
            continue;
        }
        bytes += file.getSize();
        trashedPaths.add(record.filePath);
        if (record.fileHash.isNotEmpty()) hashes.addIfNotAlreadyThere(record.fileHash);
    }
    if (! trashedPaths.isEmpty())
    {
        try
        {
            writeRepo->removeByPaths(trashedPaths);
            if (! hashes.isEmpty()) writeRepo->refreshDuplicateHashes(hashes);
        }
        catch (const std::exception& e)
        {
            failed.add(std::string("Catalog update failed: ") + e.what());
        }
        clearSampleSelection();
        reloadLibrary();
        requestFolderTreeRefresh();
        refreshFavorites();
    }
    juce::String text;
    if (! trashedPaths.isEmpty())
        text = "Moved " + juce::String(trashedPaths.size()) + " file(s), "
            + juce::File::descriptionOfSizeInBytes(bytes) + ", to Trash";
    else
        text = "No file was deleted";
    if (! failed.isEmpty()) text += " \u00b7 " + juce::String(failed.size()) + " failed";
    status.setText(text, juce::dontSendNotification);
    status.setTooltip(failed.isEmpty() ? text : failed.joinIntoString("\n"));
}
void MainComponent::openDuplicateCleanup(const juce::String& folder)
{
    if (embeddedUi || readRepo == nullptr || writeRepo == nullptr) return;
    if (duplicateDialog != nullptr) { duplicateDialog->toFront(true); return; }
    if (cleanupCatalogWork.valid())
    {
        status.setText("Wait for the catalog update to finish.", juce::dontSendNotification);
        return;
    }
    if (isSplitRunning() || isExtractRunning() || isBatchRunning() || isNormalizeRunning())
    {
        status.setText("Wait for the running job to finish before cleaning duplicates.",
                       juce::dontSendNotification);
        return;
    }
    std::vector<SampleRepository::DuplicateGroup> groups;
    try
    {
        // A previous run may have crashed between moving a copy aside and creating its link.
        const auto recovered = files::DuplicateCleanupService::recoverInterrupted(
            files::DuplicateCleanupService::defaultJournalFile());
        juce::ignoreUnused(recovered);
        const auto all = readRepo->duplicateGroups();
        if (folder.isEmpty())
        {
            groups = all;
        }
        else
        {
            // A folder scope only offers copies inside that folder: keeping an outside original
            // here would hide a cross-folder decision the person did not ask for.
            const juce::File root(folder);
            for (const auto& group : all)
            {
                SampleRepository::DuplicateGroup scoped;
                scoped.hash = group.hash;
                scoped.size = group.size;
                for (const auto& member : group.members)
                    if (juce::File(member.path).isAChildOf(root))
                        scoped.members.push_back(member);
                if (scoped.members.size() >= 2) groups.push_back(std::move(scoped));
            }
        }
    }
    catch (const std::exception& e)
    {
        status.setText("Could not read duplicate groups: " + juce::String::fromUTF8(e.what()),
                       juce::dontSendNotification);
        return;
    }
    if (groups.empty())
    {
        status.setText(folder.isEmpty()
                           ? "No byte-identical duplicates were found in this catalog."
                           : "No byte-identical duplicates were found in " + folder + ".",
                       juce::dontSendNotification);
        return;
    }
    auto* dialog = new DuplicateCleanupDialog(std::move(groups), folder);
    const auto previousPreview = preview.loadedFilePath();
    dialog->onAudition = [safe = juce::Component::SafePointer<MainComponent>(this)]
        (const juce::File& file)
    {
        if (safe == nullptr || ! file.existsAsFile()) return;
        const auto play = [safe, file]
        {
            if (safe == nullptr) return;
            // loadFile also restarts a source that is already loaded and stopped.
            if (! safe->preview.loadFile(file, true)) return;
            if (! safe->preview.transportPlaying()) safe->preview.playPause();
        };
        // Auditioning replaces the preview source; never discard unsaved waveform edits silently.
        if (safe->preview.hasUnsavedEdits())
            safe->preview.withUnsavedEditsConfirmed(
                [play](bool proceed) { if (proceed) play(); });
        else
            play();
    };
    dialog->onStopAudition = [safe = juce::Component::SafePointer<MainComponent>(this),
                              previousPreview]
    {
        if (safe == nullptr) return;
        safe->preview.stopPlayback();
        if (previousPreview != juce::File() && previousPreview.existsAsFile())
            safe->preview.loadFile(previousPreview, false);
        else
            safe->preview.clearSource();
    };
    dialog->onRun = [safe = juce::Component::SafePointer<MainComponent>(this)]
    {
        if (safe != nullptr) safe->preview.stopPlayback();
    };
    dialog->onFinished = [safe = juce::Component::SafePointer<MainComponent>(this),
                          catalogPath = dbPath](const juce::StringArray& removed,
                                                const juce::StringArray& linked,
                                                const juce::String& summary)
    {
        if (safe != nullptr && safe->dbPath == catalogPath)
            safe->applyDuplicateCleanupResult(removed, linked, summary);
    };
    dialog->onCancel = [safe = juce::Component::SafePointer<MainComponent>(this)]
    {
        if (safe == nullptr || safe->duplicateDialog == nullptr) return;
        safe->duplicateDialog->exitModalState(0);
        safe->duplicateDialog = nullptr;
    };
    auto* window = new DuplicateDialogWindow();
    window->setContentOwned(dialog, true);
    window->setLookAndFeel(&look);
    window->setUsingNativeTitleBar(false);
    window->setResizable(false, false);
    window->setSize(dialog->getWidth(), dialog->getHeight());
    window->centreAroundComponent(this, window->getWidth(), window->getHeight());
    window->onClose = [safe = juce::Component::SafePointer<MainComponent>(this), dialog]
    {
        if (safe == nullptr || safe->duplicateDialog == nullptr) return;
        // Closing during a run stops it first, so no window disappears under a live thread.
        if (dialog->isRunning()) { dialog->requestStop(); return; }
        safe->duplicateDialog->exitModalState(0);
        safe->duplicateDialog = nullptr;
    };
    duplicateDialog = window;
    window->enterModalState(true, nullptr, true);
}
void MainComponent::applyDuplicateCleanupResult(const juce::StringArray& removedPaths,
                                                const juce::StringArray& linkedPaths,
                                                const juce::String& summary)
{
    pendingCleanupSummary = summary;
    juce::StringArray hashes;
    if (readRepo != nullptr && ! removedPaths.isEmpty())
        for (const auto& path : removedPaths)
            if (const auto record = readRepo->findByPath(path))
                if (record->fileHash.isNotEmpty()) hashes.addIfNotAlreadyThere(record->fileHash);
    if (writeRepo == nullptr || (removedPaths.isEmpty() && linkedPaths.isEmpty()))
    {
        status.setText(summary, juce::dontSendNotification);
        return;
    }
    // Deleting hundreds of rows used to run on the message thread; that work is now off it.
    status.setText("Updating the catalog...", juce::dontSendNotification);
    status.setTooltip({});
    auto* repo = writeRepo.get();
    cleanupCatalogWork = std::async(std::launch::async,
        [repo, removedPaths, linkedPaths, hashes]
        {
            if (! removedPaths.isEmpty()) repo->removeByPaths(removedPaths);
            // The library needs to know these rows are now aliases, both for the Ignore symlinks
            // view and for the next duplicate review.
            if (! linkedPaths.isEmpty()) repo->setSymlinkFlags(linkedPaths, true);
            if (! hashes.isEmpty()) repo->refreshDuplicateHashes(hashes);
        });
}
void MainComponent::finishCleanupCatalogUpdate()
{
    if (! cleanupCatalogWork.valid()
        || cleanupCatalogWork.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;
    try
    {
        cleanupCatalogWork.get();
    }
    catch (const std::exception& e)
    {
        status.setText("Duplicate cleanup finished, but the catalog update failed: "
                       + juce::String::fromUTF8(e.what()), juce::dontSendNotification);
        return;
    }
    clearSampleSelection();
    reloadLibrary();
    requestFolderTreeRefresh();
    refreshFavorites();
    status.setText(pendingCleanupSummary, juce::dontSendNotification);
}
void MainComponent::syncNormalizedCatalog()
{
    if (writeRepo == nullptr) return;
    for (const auto& result : normalizeResults)
    {
        if (result.state != NormalizeFileState::normalized) continue;
        const auto found = normalizeSampleIds.find(result.source.getFullPathName());
        const auto templateId = found != normalizeSampleIds.end() ? found->second : (int64_t) 0;
        enqueueCatalogSync(result.destination, templateId, ! result.replacedOriginal);
    }
}

void MainComponent::showSampleContextMenu(const std::vector<SampleRecord>& selection,
                                          const SampleRecord& lead,
                                          juce::Point<int> screenPosition)
{
    if (selection.empty() || readRepo == nullptr) return;
    std::vector<IgnoredPath> ignoredPaths;
    try { allTags = readRepo->tags(); ignoredPaths = readRepo->ignoredPaths(); }
    catch (const std::exception& e) { status.setText(juce::String::fromUTF8(e.what()),juce::dontSendNotification); return; }
    std::vector<int64_t> ids;
    bool allStarred = true, anyExisting = false;
    for (const auto& record : selection)
    {
        ids.push_back(record.id);
        allStarred = allStarred && record.starred;
        anyExisting = anyExisting || juce::File(record.filePath).existsAsFile();
    }
    bool allIgnored = true, anyInherited = false;
    for (const auto& record : selection)
    {
        const bool ignored = isPathIgnored(record.filePath, ignoredPaths);
        allIgnored = allIgnored && ignored;
        if (ignored && ! readRepo->hasExactIgnore(record.filePath)) anyInherited = true;
    }
    std::vector<juce::File> existingFiles, splitCandidates;
    for (const auto& record : selection)
    {
        const juce::File file(record.filePath);
        if (! file.existsAsFile() || isPathIgnored(record.filePath, ignoredPaths)) continue;
        if (! FileScanner::isSupportedFormat(file)) continue;
        existingFiles.push_back(file);
        if (! FileScanner::isMetadataOnlyFormat(file)) splitCandidates.push_back(file);
    }
    // Physical deletion works only on ordinary files: no container member, no link, no export.
    std::vector<SampleRecord> deleteCandidates;
    juce::StringArray deletePaths;
    for (const auto& record : selection)
    {
        if (record.isDiscEntry() || record.containerPath.isNotEmpty()) continue;
        const juce::File file(record.filePath);
        if (! file.existsAsFile() || file.isSymbolicLink()) continue;
        if (isProtectedExportPath(file)) continue;
        if (deletePaths.contains(file.getFullPathName())) continue;
        deletePaths.add(file.getFullPathName());
        deleteCandidates.push_back(record);
    }
    // Embedded banks and disc images are metadata-only for scanning, and a disc sample row has a
    // synthetic path, so they are collected from the records rather than from the file list.
    const auto extractCandidates = collectExtractCandidates(selection,
        [&ignoredPaths](const juce::String& path) { return isPathIgnored(path, ignoredPaths); });
    const auto normalizeRecords = normalizeCandidates(selection);
    const auto assignments = readRepo->tagsForSamples(ids);
    std::map<int64_t, bool> allHave;
    for (const auto& tag : allTags)
    {
        bool every = true;
        for (auto id : ids)
        {
            bool has = false;
            const auto found = assignments.find(id);
            if (found != assignments.end())
                for (const auto& assigned : found->second)
                    if (assigned.id == tag.id) { has = true; break; }
            if (! has) { every = false; break; }
        }
        allHave[tag.id] = every;
    }

    auto recent = readRepo->recentTags();
    juce::PopupMenu menu, tagsMenu;
    menu.setLookAndFeel(&look);
    std::shared_ptr<juce::Point<int>> roundTagPillCentre;
    if (appSettings.roundTagMenu)
    {
        roundTagPillCentre = std::make_shared<juce::Point<int>>(screenPosition);
        auto pill = std::make_unique<TagsMenuPill>(roundTagPillCentre,
            [safe = juce::Component::SafePointer<MainComponent>(this), roundTagPillCentre,
             ids, catalogPath = dbPath]
            {
                juce::MessageManager::callAsync([safe, roundTagPillCentre, ids, catalogPath]
                {
                    if (safe == nullptr || safe->dbPath != catalogPath) return;
                    juce::PopupMenu::dismissAllActiveMenus();
                    safe->showRoundTagMenuForIds(ids,
                                                 safe->getLocalPoint(nullptr, *roundTagPillCentre));
                });
            });
        menu.addCustomItem(9026, std::move(pill), nullptr, "Tags");
        menu.addSeparator();
    }
    else
    {
        for (int i = 0; i < (int) recent.size(); ++i)
            menu.addItem(20000 + i, recent[(size_t) i].name, true, false,
                         tagDotImage(recent[(size_t) i].colour));
        if (! recent.empty()) menu.addSeparator();
    }
    menu.addItem(9000, "Copy", anyExisting);
    menu.addItem(9009, anyInherited ? "Ignored with folder" : "Ignore",
                 ! anyInherited, allIgnored);
    // File-processing jobs write many files and own the catalog for minutes; in a plugin they
    // stay in the application, where the user started them.
    if (embeddedUi)
    {
        menu.addSectionHeader("Processing");
    }
    else
    {
        menu.addItem(9008, splitCandidates.size() > 1
            ? "Split by silence (" + juce::String((int) splitCandidates.size()) + ")..."
            : juce::String("Split by silence..."), ! splitCandidates.empty());
        menu.addItem(9010, extractCandidates.sourceCount() > 1
            ? "Extract samples from " + juce::String(extractCandidates.sourceCount()) + " item(s)..."
            : juce::String("Extract samples..."), ! extractCandidates.empty());
        menu.addItem(editCommandMenuItem(EditAction::normalizeSelected,
                                         ! normalizeRecords.empty() && ! isNormalizeRunning()));
        menu.addItem(9025, "Batch Processing...", ! existingFiles.empty() && ! isBatchRunning());
        menu.addItem(9030, "Spectral analysis (" + juce::String((int) existingFiles.size()) + ")...",
                     ! existingFiles.empty() && ! isBatchRunning()
                         && (spectralAnalysis == nullptr || ! spectralAnalysis->isRunning()));
    }
    if (! appSettings.roundTagMenu)
    {
        for (int i = 0; i < (int) allTags.size(); ++i)
            tagsMenu.addItem(10000 + i, allTags[(size_t) i].name, true,
                             allHave[allTags[(size_t) i].id], tagDotImage(allTags[(size_t) i].colour));
        if (! allTags.empty()) tagsMenu.addSeparator();
        tagsMenu.addItem(9003, "New tag...");
        menu.addSubMenu("Tag", tagsMenu);
    }

    juce::PopupMenu typeMenu;
    const auto standardTypes = standardSampleTypeNames();
    juce::String commonType;
    bool hasFirst = false;
    bool uniformType = true;
    bool anyTypeOverride = false;
    const auto displays = readRepo->smartDisplays(ids);
    for (size_t i = 0; i < ids.size(); ++i)
    {
        const auto it = displays.find(ids[i]);
        if (it != displays.end())
        {
            if (it->second.typeOverride != OverrideState::None)
                anyTypeOverride = true;
            if (! hasFirst)
            {
                commonType = it->second.type;
                hasFirst = true;
            }
            else if (! commonType.equalsIgnoreCase(it->second.type))
                uniformType = false;
        }
    }

    for (int i = 0; i < standardTypes.size(); ++i)
    {
        const auto& t = standardTypes[i];
        const bool isChecked = uniformType && commonType.equalsIgnoreCase(t);
        typeMenu.addItem(30000 + i, t, true, isChecked);
    }

    auto customList = readRepo->customTypes();
    std::sort(customList.begin(), customList.end(), [](const juce::String& a, const juce::String& b) {
        return a.compareIgnoreCase(b) < 0;
    });
    if (! customList.empty())
    {
        typeMenu.addSeparator();
        for (int i = 0; i < (int) customList.size(); ++i)
        {
            const auto& t = customList[(size_t) i];
            const bool isChecked = uniformType && commonType.equalsIgnoreCase(t);
            typeMenu.addItem(40000 + i, t, true, isChecked);
        }
    }

    typeMenu.addSeparator();
    typeMenu.addItem(9021, "+ Custom type...");
    typeMenu.addItem(9020, "Reset type override", anyTypeOverride);
    menu.addSubMenu("Type", typeMenu);

    bool anyManualBpm = false;
    for (const auto id : ids)
    {
        const auto it = displays.find(id);
        if (it != displays.end() && it->second.bpmOverride == OverrideState::Value) anyManualBpm = true;
    }
    juce::PopupMenu bpmMenu;
    bpmMenu.addItem(25001, "Set BPM...");
    bpmMenu.addItem(25002, "Tap BPM...");
    bpmMenu.addItem(25003, "Clear manual BPM", anyManualBpm);
    menu.addSubMenu("BPM", bpmMenu);

    // Genre: one value per file, with a manual override that survives reanalysis.
    const auto genreNames = readRepo->genreNames();
    bool anyGenreOverride = false, anyGenreUnknown = false;
    juce::String commonGenre;
    bool haveCommonGenre = false, uniformGenre = true;
    for (const auto id : ids)
    {
        const auto it = displays.find(id);
        if (it == displays.end()) { uniformGenre = false; continue; }
        const auto& d = it->second;
        if (d.genreOverride != OverrideState::None) anyGenreOverride = true;
        if (d.genreOverride == OverrideState::Unknown) anyGenreUnknown = true;
        if (! haveCommonGenre) { commonGenre = d.genre; haveCommonGenre = true; }
        else if (! commonGenre.equalsIgnoreCase(d.genre)) uniformGenre = false;
    }
    juce::PopupMenu genreMenu;
    for (int i = 0; i < genreNames.size(); ++i)
        genreMenu.addItem(genreMenuFirstId + i, genreNames[i], true,
                          uniformGenre && commonGenre.equalsIgnoreCase(genreNames[i]));
    genreMenu.addSeparator();
    genreMenu.addItem(26190, "Unknown", true, anyGenreUnknown);
    genreMenu.addItem(26191, "Clear manual genre", anyGenreOverride);
    genreMenu.addItem(26192, "+ Custom genre...");
    menu.addSubMenu("Genre", genreMenu);

    menu.addItem(9001, allStarred ? "Unstar" : "Star", true, false, starMenuImage());
    // Analysis and AutoTag decode audio and write the catalog in batches; a plugin leaves both to
    // the application, like scanning and file processing.
    if (embeddedUi)
    {
        menu.addSectionHeader("Analysis and AutoTag run in the SONICATOR app");
    }
    else
    {
        const bool smartAvailable = smart != nullptr && ! (indexer && indexer->progress().running);
        menu.addItem(9005, selection.size() > 1 ? "Analyze selected (" + juce::String(selection.size()) + ")"
                                                : juce::String("Analyze selected"),
                     smartAvailable);
        const bool autoTagIdle = autoTag != nullptr && ! autoTag->progress().running
            && ! (indexer && indexer->progress().running)
            && ! (smart && smart->progress().running)
            && (tagging == nullptr || ! tagging->progress().running);
        menu.addItem(9006, "AutoTag selected...", autoTagIdle);
        const bool resetIdle = ! ids.empty() && autoTagDialog == nullptr
            && (tagging == nullptr || ! tagging->progress().running)
            && (autoTag == nullptr || ! autoTag->progress().running);
        menu.addItem(9007, "Reset tags...", resetIdle);
    }
#if JUCE_MAC
    const juce::String openLabel = "Open in Finder";
#elif JUCE_WINDOWS
    const juce::String openLabel = "Open in Explorer";
#else
    const juce::String openLabel = "Open folder";
#endif
    const juce::File leadFile(lead.filePath);
    menu.addItem(9002, openLabel, leadFile.existsAsFile() || leadFile.getParentDirectory().isDirectory());
    const auto duplicate = readRepo->duplicateInfo(lead.id);
    if (duplicate.has_value() && duplicate->groupSize > 1)
        menu.addItem(9004, "Show all " + juce::String(duplicate->groupSize) + " copies");
    if (! embeddedUi)
    {
        menu.addSeparator();
        menu.addColouredItem(9028, "Delete from disk...", theme::danger,
                             ! deleteCandidates.empty(), false, juce::Image());
        // Duplicate cleanup is a whole-catalog action; it lives in the library menu and here.
        menu.addItem(9029, "Find duplicates...", ! isSplitRunning() && ! isExtractRunning()
                                                   && ! isNormalizeRunning() && ! isBatchRunning());
    }
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&library).withTargetScreenArea(
                           { screenPosition.x, screenPosition.y, 1, 1 })
                           .withMaximumNumColumns(1)
                           .withPreferredPopupDirection(juce::PopupMenu::Options::PopupDirection::downwards),
        [safe = juce::Component::SafePointer<MainComponent>(this), ids, allStarred, allHave, recent,
         files = selection, leadPath = lead.filePath, leadHash = lead.fileHash,
         deleteCandidates, ignoredPaths, allIgnored, anyInherited, splitCandidates,
         extractCandidates, customList,
          standardTypes, normalizeRecords, genreNames, catalogPath = dbPath,
          roundTagPillCentre](int result)
        {
            if (safe == nullptr || result == 0 || safe->dbPath != catalogPath) return;
            if (result == 9026)
            {
                if (roundTagPillCentre != nullptr)
                    safe->showRoundTagMenuForIds(
                        ids, safe->getLocalPoint(nullptr, *roundTagPillCentre));
                return;
            }
            // Defence in depth: the plugin hides these items, and no path may start them either.
            if (safe->embeddedUi && (result == 9005 || result == 9006 || result == 9007
                                     || result == 9008 || result == 9010 || result == 9025
                                     || result == 9028 || result == 9029))
                return;
            if ((safe->isSplitRunning() || safe->isExtractRunning())
                && result != 9000 && result != 9002 && result != 9004)
            {
                safe->status.setText("Wait for the running job to finish.", juce::dontSendNotification);
                return;
            }
            try
            {
                if (result == 9028)
                {
                    safe->deleteFilesFromDisk(deleteCandidates);
                    return;
                }
                if (result == 9029)
                {
                    safe->openDuplicateCleanup();
                    return;
                }
                if (result == 9025)
                {
                    batch::SourceSelection batchSelection;
                    batchSelection.kind = batch::SourceSelection::Kind::rows;
                    batchSelection.rows = ids;
                    safe->openBatchProcessing(batchSelection);
                    return;
                }
                if (result == 9030)
                {
                    std::vector<juce::File> spectralFiles;
                    for (const auto& record : files)
                    {
                        const juce::File file(record.filePath);
                        if (file.existsAsFile()) spectralFiles.push_back(file);
                    }
                    safe->startSpectralAnalysis(std::move(spectralFiles));
                    return;
                }
                if (result == 25001)
                {
                    juce::String initial = "120";
                    if (ids.size() == 1)
                    {
                        const auto bpmDisplays = safe->readRepo->smartDisplays(ids);
                        if (! bpmDisplays.empty() && bpmDisplays.begin()->second.hasBpm)
                            initial = juce::String(bpmDisplays.begin()->second.bpm, 2);
                    }
                    auto captured = ids;
                    dialogs::showPrompt(*safe, "Set BPM", "Tempo (20 to 300)", initial,
                        [safe, captured](const juce::String& text)
                        {
                            if (safe == nullptr) return;
                            const auto bpm = text.getDoubleValue();
                            if (! (bpm >= 20.0 && bpm <= 300.0))
                            {
                                safe->status.setText("BPM must be between 20 and 300",
                                                     juce::dontSendNotification);
                                return;
                            }
                            safe->setBpmForRows(captured, bpm);
                        });
                    return;
                }
                if (result == 25002)
                {
                    const bool single = files.size() == 1;
                    const auto title = single ? files.front().fileName
                        : juce::String((int) files.size()) + " samples";
                    auto captured = ids;
                    TapTempoDialog::launch(safe.getComponent(), title,
                        single ? std::function<void()>([safe] { if (safe != nullptr) safe->preview.startTapLoop(); })
                               : std::function<void()> {},
                        single ? std::function<void()>([safe] { if (safe != nullptr) safe->preview.stopTapLoop(); })
                               : std::function<void()> {},
                        [safe, captured](double bpm)
                        {
                            if (safe != nullptr) safe->setBpmForRows(captured, bpm);
                        });
                    return;
                }
                if (result == 25003)
                {
                    safe->clearBpmForRows(ids);
                    return;
                }
                if (result >= genreMenuFirstId)
                {
                    const int index = result - genreMenuFirstId;
                    if (index < genreNames.size())
                    {
                        SmartOverride value;
                        value.textValue = genreNames[index];
                        for (auto id : ids)
                            safe->writeRepo->setSmartOverride(id, SmartField::Genre, value);
                        safe->reloadLibrary();
                        safe->status.setText("Genre set to " + genreNames[index] + " for "
                                             + juce::String(ids.size()) + " sample(s)",
                                             juce::dontSendNotification);
                    }
                    return;
                }
                if (result == 26190 || result == 26191)
                {
                    for (auto id : ids)
                    {
                        if (result == 26191)
                        {
                            safe->writeRepo->clearSmartOverride(id, SmartField::Genre);
                        }
                        else
                        {
                            SmartOverride unknown;
                            unknown.cleared = true;
                            safe->writeRepo->setSmartOverride(id, SmartField::Genre, unknown);
                        }
                    }
                    safe->reloadLibrary();
                    safe->status.setText((result == 26191 ? "Manual genre cleared for "
                                                          : "Genre marked unknown for ")
                                         + juce::String(ids.size()) + " sample(s)",
                                         juce::dontSendNotification);
                    return;
                }
                if (result == 26192)
                {
                    dialogs::showPrompt(*safe, "Custom genre", "Genre name",
                                        juce::String(),
                        [safe, ids, catalogPath](const juce::String& name)
                        {
                            if (safe == nullptr || safe->dbPath != catalogPath
                                || name.trim().isEmpty() || safe->writeRepo == nullptr)
                                return;
                            SmartOverride value;
                            value.textValue = name.trim();
                            for (auto id : ids)
                                safe->writeRepo->setSmartOverride(id, SmartField::Genre, value);
                            safe->reloadLibrary();
                        });
                    return;
                }
                if (result == 9020)
                {
                    safe->cancelFeedback = true;
                    if (safe->typeFeedbackWorker && safe->typeFeedbackWorker->joinable())
                        safe->typeFeedbackWorker->join();
                    safe->cancelFeedback = false;
                    for (auto id : ids)
                    {
                        safe->writeRepo->clearSmartOverride(id, SmartField::Type);
                        safe->writeRepo->removeFeedback(id);
                        AcousticClassifierService::getSharedAdaptiveClassifier().removePrototype(id);
                    }
                    safe->reloadLibrary();
                    safe->status.setText("Reset type override for " + juce::String(ids.size()) + " sample(s)", juce::dontSendNotification);
                }
                else if (result == 9021)
                {
                    safe->promptCustomType([safe, ids, files, standardTypes](juce::String name)
                    {
                        if (safe == nullptr || name.trim().isEmpty() || safe->writeRepo == nullptr) return;
                        name = name.trim();
                        for (const auto& st : standardTypes)
                        {
                            if (st.equalsIgnoreCase(name))
                            {
                                safe->applyTypeOverride(ids, files, st);
                                return;
                            }
                        }
                        safe->writeRepo->addCustomType(name);
                        safe->applyTypeOverride(ids, files, name);
                    });
                }
                else if (result >= 40000)
                {
                    const size_t index = (size_t) (result - 40000);
                    if (index < customList.size())
                        safe->applyTypeOverride(ids, files, customList[index]);
                }
                else if (result >= 30000)
                {
                    const size_t index = (size_t) (result - 30000);
                    if (index < (size_t) standardTypes.size())
                        safe->applyTypeOverride(ids, files, standardTypes[(int) index]);
                }
                else if (result >= 20000)
                {
                    const size_t index = (size_t) (result - 20000);
                    if (index >= recent.size()) return;
                    const auto tag = recent[index];
                    const int added = safe->assignTagAndRemember(ids, tag.id);
                    safe->status.setText(added > 0
                        ? "Tagged " + juce::String(added) + " of " + juce::String(ids.size())
                            + " selected file(s) as " + tag.name
                        : "All selected files already have the tag " + tag.name,
                        juce::dontSendNotification);
                    safe->reloadLibrary();
                }
                else if (result == 9000)
                {
                    juce::Array<juce::File> selected;
                    for (const auto& record : files)
                        if (juce::File(record.filePath).existsAsFile()) selected.add(juce::File(record.filePath));
                    if (selected.isEmpty()) { safe->status.setText("No existing files to copy.",juce::dontSendNotification); return; }
                    const bool native = copyFilesToClipboard(selected);
                    safe->status.setText(native ? "Copied " + juce::String(selected.size()) + " file(s)"
                                                : "Copied " + juce::String(selected.size()) + " path(s)",juce::dontSendNotification);
                }
                else if (result == 9008)
                {
                    safe->startSplit(splitCandidates);
                }
                else if (result == 9010)
                {
                    auto files = extractCandidates.embedded;
                    files.insert(files.end(), extractCandidates.images.begin(), extractCandidates.images.end());
                    safe->startExtract(files, extractCandidates.entries);
                }
                else if (const auto* command = findEditCommand(EditAction::normalizeSelected);
                         command != nullptr && result == command->id)
                {
                    safe->startNormalizeSelected(normalizeRecords);
                }
                else if (result == 9009)
                {
                    int changed = 0;
                    if (allIgnored)
                    {
                        std::vector<juce::String> pathsToUnignore;
                        for (const auto& record : files)
                            if (safe->readRepo->hasExactIgnore(record.filePath))
                                pathsToUnignore.push_back(record.filePath);
                        changed = safe->writeRepo->unignorePaths(pathsToUnignore);
                    }
                    else
                    {
                        std::vector<juce::String> pathsToIgnore;
                        for (const auto& record : files)
                            if (! isPathIgnored(record.filePath, ignoredPaths))
                                pathsToIgnore.push_back(record.filePath);
                        changed = safe->writeRepo->ignorePaths(pathsToIgnore, false);
                    }
                    const auto updatedIgnored = safe->readRepo->ignoredPaths();
                    safe->library.setIgnoredPaths(updatedIgnored);
                    safe->folders.setIgnoredPaths(updatedIgnored, safe->filter.showIgnored);
                    safe->favorites.setIgnoredPaths(updatedIgnored, safe->filter.showIgnored);
                    safe->ensureSelectedFolderVisible();
                    safe->reloadLibrary();
                    safe->status.setText((allIgnored ? "Stopped ignoring " : "Ignoring ")
                        + juce::String(changed) + " file(s)", juce::dontSendNotification);
                }
                else if (result == 9001)
                {
                    safe->writeRepo->setStarred(ids, ! allStarred);
                    if (safe->filter.starredOnly)
                    {
                        safe->reloadLibrary();
                    }
                    else
                    {
                        safe->library.setStarredInPlace(ids, ! allStarred);
                        if (safe->selectedSampleId > 0 && std::find(ids.begin(), ids.end(), safe->selectedSampleId) != ids.end())
                        {
                            if (auto selected = safe->readRepo->findById(safe->selectedSampleId))
                                safe->updateSelectionStatus(*selected);
                        }
                    }
                }
                else if (result == 9002)
                {
                    const juce::File file(leadPath);
                    if (file.existsAsFile()) file.revealToUser();
                    else if (file.getParentDirectory().isDirectory()) file.getParentDirectory().revealToUser();
                    else safe->status.setText("Folder is not available.",juce::dontSendNotification);
                }
                else if (result == 9005)
                {
                    if (safe->smart == nullptr) return;
                    std::vector<int64_t> selectedIds;
                    for (const auto& record : files) selectedIds.push_back(record.id);
                    if (! selectedIds.empty() && safe->smart->progress().running)
                    {
                        int queued = 0;
                        for (auto id : selectedIds) if (safe->smart->requestSample(id)) ++queued;
                        safe->status.setText("Queued " + juce::String(queued)
                            + " sample(s) for smart analysis", juce::dontSendNotification);
                    }
                    else if (safe->smart->startSelected(selectedIds))
                        safe->status.setText("Analyzing " + juce::String(selectedIds.size())
                            + " selected sample(s)", juce::dontSendNotification);
                    else
                        safe->status.setText("Smart analysis could not start", juce::dontSendNotification);
                }
                else if (result == 9006)
                {
                    std::vector<int64_t> selectedIds;
                    for (const auto& record : files) selectedIds.push_back(record.id);
                    safe->startAutoTagSelected(selectedIds);
                }
                else if (result == 9007)
                {
                    safe->resetTagsSelected(ids);
                }
                else if (result == 9004)
                {
                    if (leadHash.isEmpty()) return;
                    safe->filter.duplicateHash = leadHash;
                    safe->filter.hideDuplicates = false;
                    safe->filter.text.clear(); safe->filter.includeTags.clear();
                    safe->filter.excludeTags.clear(); safe->filter.matchAnyTag = false;
                    safe->filter.starredOnly = false; safe->selectedFolder.clear();
                    safe->search.clear(); safe->pendingSearch.clear();
                    safe->folders.clearSelection();
                    safe->pageOffset = 0;
                    safe->reloadLibrary();
                }
                else if (result == 9003)
                {
                    safe->promptTagName("New tag", {}, [safe, ids](juce::String name)
                    {
                        if (safe == nullptr || name.trim().isEmpty() || safe->writeRepo == nullptr) return;
                        try
                        {
                            const auto colour = tagPalette[safe->allTags.size() % std::size(tagPalette)].colour;
                            const auto id = safe->writeRepo->createTag(name, colour);
                            safe->assignTagAndRemember(ids, id);
                            safe->reloadLibrary();
                        }
                        catch (const std::exception& e) { safe->status.setText(juce::String::fromUTF8(e.what()),juce::dontSendNotification); }
                    });
                }
                else if (result >= 10000)
                {
                    const size_t index = (size_t) (result - 10000);
                    if (index >= safe->allTags.size()) return;
                    const auto tag = safe->allTags[index];
                    const auto state = allHave.find(tag.id);
                    const bool assign = state == allHave.end() || ! state->second;
                    if (assign) safe->assignTagAndRemember(ids, tag.id);
                    else
                    {
                        safe->writeRepo->setSampleTag(ids, tag.id, false);
                        safe->triggerAdaptiveLearning(ids);
                    }
                    safe->reloadLibrary();
                }
            }
            catch (const std::exception& e) { safe->status.setText(juce::String::fromUTF8(e.what()),juce::dontSendNotification); }
        });
}
void MainComponent::promptTagName(const juce::String& title, const juce::String& initial,
                                  std::function<void(juce::String)> onAccept)
{
    dialogs::showPrompt(*this, title, "Tag name:", initial, std::move(onAccept));
}
void MainComponent::promptCustomType(std::function<void(juce::String)> onAccept)
{
    dialogs::showPrompt(*this, "New custom type", "Type name:", {}, std::move(onAccept));
}
void MainComponent::applyTypeOverride(const std::vector<int64_t>& ids,
                                      const std::vector<SampleRecord>& files,
                                      const juce::String& type)
{
    if (ids.empty() || writeRepo == nullptr) return;
    SmartOverride val;
    val.textValue = type;
    for (auto id : ids)
    {
        writeRepo->setSmartOverride(id, SmartField::Type, val);
    }
    reloadLibrary();
    status.setText("Set type to " + type + " for " + juce::String(ids.size()) + " sample(s)", juce::dontSendNotification);

    triggerAdaptiveLearning(ids);
    juce::ignoreUnused(files);
}
void MainComponent::triggerAdaptiveLearning(const std::vector<int64_t>& ids)
{
    if (ids.empty() || writeRepo == nullptr) return;

    cancelFeedback = true;
    if (typeFeedbackWorker && typeFeedbackWorker->joinable())
        typeFeedbackWorker->join();
    cancelFeedback = false;

    // The contribution thread reads the same repository under its own lock, so a confirmation must
    // not stop it: it sends the rows this worker writes as soon as they are visible.
    juce::Component::SafePointer<MainComponent> safeContribution(this);
    typeFeedbackWorker = std::make_unique<std::thread>([this, sampleIds = ids, repo = writeRepo.get(), safeContribution]()
    {
        if (repo == nullptr) return;
        for (auto id : sampleIds)
        {
            if (cancelFeedback.load()) break;
            const auto record = repo->findById(id);
            if (! record) continue;
            const juce::File f(record->filePath);
            if (! f.existsAsFile()) continue;

            std::vector<float> features;
            int featureSchema = 0;
            for (const auto& fb : repo->allFeedback())
                if (fb.sampleId == id)
                {
                    features = fb.features;
                    featureSchema = fb.featureSchema;
                    break;
                }
            const bool needV1 = features.empty();
            const bool needV2 = featureSchema < 2;
            AnalysisAudio audio;
            if (needV1 || needV2)
            {
                AnalysisAudioOptions opts;
                opts.maxSeconds = 30.0;
                audio = readAnalysisAudio(f, opts);
                if (needV1 && ! audio.ok()) continue;
            }
            if (needV1)
            {
                features = AdaptiveClassifier::extractFeatures(audio.buffer, audio.sampleRate);
                if (features.empty()) continue;
            }
            if (cancelFeedback.load()) break;

            const auto sampleTags = repo->tagsForSample(id);
            juce::StringArray tagNames;
            for (const auto& t : sampleTags) tagNames.add(t.name);

            juce::String type;
            const auto currentOverrides = repo->smartOverrides(id);
            const auto it = currentOverrides.find(SmartField::Type);
            if (it != currentOverrides.end() && ! it->second.cleared && ! it->second.notApplicable)
                type = it->second.textValue;
            else if (const auto res = repo->smartResult(id))
                type = res->type;

            if (tagNames.isEmpty() && type.isEmpty())
            {
                repo->removeFeedback(id);
                AcousticClassifierService::getSharedAdaptiveClassifier().removePrototype(id);
            }
            else
            {
                repo->recordFeedback(id, type, tagNames, features,
                                     SampleRepository::FeedbackOrigin::user);
                std::vector<float> featuresV2;
                if (needV2 && audio.ok())
                {
                    const auto vector = FeatureExtractorV2::extract(audio.buffer, audio.sampleRate);
                    if (vector.ok())
                    {
                        featuresV2 = vector.values;
                        repo->setFeedbackV2(id, featuresV2);
                    }
                }
                AcousticClassifierService::getSharedAdaptiveClassifier().updatePrototype(
                    id, type, tagNames, features, featuresV2);
            }
        }
        // The rows are written; send them now rather than at tomorrow's cycle.
        if (! cancelFeedback.load())
            juce::MessageManager::callAsync([safeContribution]() mutable
            {
                if (safeContribution != nullptr) safeContribution->requestContributionUpload(false);
            });
    });
}
void MainComponent::timerCallback()
{
    pollSpectralAnalysis();
    pollCloudBuild();
    if (cloudVisible && cloudPanel != nullptr && cloudBuilder != nullptr)
    {
        const auto now = juce::Time::getMillisecondCounter();
        if (cloudBuildScheduled && (int32_t) (now - cloudBuildAtMs) >= 0)
        {
            cloudBuildScheduled = false;
            if (! cloudBuilder->isRunning()) cloudBuilder->startCatalog(dbPath);
        }
        // Tag edits arrive from menus, AutoTag jobs and background services; the tag table carries
        // their count and colour, so one cheap poll catches every path without hooking each one.
        if (! cloudBuilder->isRunning() && ! cloudBuildScheduled && readRepo != nullptr)
        {
            auto tags = readRepo->tags();
            bool changed = tags.size() != cloudTagSnapshot.size();
            if (! changed)
                for (size_t i = 0; i < tags.size(); ++i)
                {
                    const auto& a = tags[i];
                    const auto& b = cloudTagSnapshot[i];
                    if (a.id != b.id || a.colour != b.colour || a.sampleCount != b.sampleCount || a.name != b.name)
                    {
                        changed = true;
                        break;
                    }
                }
            if (changed)
            {
                cloudTagSnapshot = std::move(tags);
                requestCloudBuild();
            }
        }
        if (engine.isPlaying() && engine.hover().isPlaying()) engine.hover().stop();
    }
    // The catalog's busy state changes while Settings is open; the page must follow it instead
    // of keeping the snapshot taken when the window was created.
    if (settingsPanel != nullptr) settingsPanel->setBusyState(indexer && indexer->progress().running);
    if (embeddedSettingsPanel != nullptr)
        embeddedSettingsPanel->setBusyState(indexer && indexer->progress().running);
    if (settingsDialog == nullptr) settingsPanel = nullptr;
    if (! indexer) return;
    auto p = indexer->progress();
    const auto s = smart ? smart->progress() : SmartProgress {};
    const auto tg = tagging ? tagging->progress() : TaggingProgress {};
    const auto at = autoTag ? autoTag->progress() : AutoTagProgress {};
    const auto sp = splitter ? splitter->progress() : SplitProgress {};
    const auto ep = extractor ? extractor->progress() : ExtractProgress {};
    const auto np = normalizer ? normalizer->progress() : NormalizeProgress {};
    const bool smartRunning = s.running;
    const bool taggingRunning = tg.running;
    const bool autoTagRunning = at.running;
    const bool splitRunning = sp.running;
    const bool extractRunning = ep.running;
    const bool normalizeRunning = np.running;
    const bool genreRunning = folderGenreWork.valid();
    const bool batchRunning = isBatchRunning();
    const bool previewRunning = batchPreview != nullptr && batchPreview->isRunning();
    const bool busy = genreRunning || p.running || smartRunning || taggingRunning || autoTagRunning
        || splitRunning || extractRunning || normalizeRunning || batchRunning || previewRunning;
    activityDot.setActive(busy);
    pause.setEnabled(! splitRunning && (p.running || smartRunning));
    cancel.setEnabled(! batchRunning && busy && ! genreRunning);
    addFolder.setEnabled(! busy && ! embeddedUi); rescan.setEnabled(! busy && ! embeddedUi);
    reanalyze.setEnabled(! busy && ! embeddedUi); newFiles.setEnabled(! busy && ! embeddedUi);
    smartButton.setEnabled(! busy && ! embeddedUi);
    pause.setButtonText((p.running ? p.paused : s.paused) ? "Resume" : "Pause");
    if (splitRunning || splitWasRunning)
    {
        if (splitRunning && splitContent != nullptr)
            splitContent->setProgress(sp, (juce::Time::getMillisecondCounter() - splitStartMs) / 1000.0);
        status.setText("Splitting " + juce::String(sp.processed) + "/" + juce::String(sp.total)
            + (sp.current.isNotEmpty() ? "  |  " + sp.current : juce::String()),
            juce::dontSendNotification);
        status.setTooltip(status.getText());
    }
    else if (extractRunning || extractWasRunning)
    {
        if (extractRunning && extractContent != nullptr)
            extractContent->setProgress(ep,
                (juce::Time::getMillisecondCounter() - extractStartMs) / 1000.0);
        status.setText("Extracting " + juce::String(ep.processedFiles) + "/"
            + juce::String(ep.totalFiles) + " file(s)  |  "
            + juce::String(ep.written) + " sample(s)"
            + (ep.current.isNotEmpty() ? "  |  " + ep.current : juce::String()),
            juce::dontSendNotification);
        status.setTooltip(status.getText());
    }
    else if (normalizeRunning || normalizeWasRunning)
    {
        status.setText("Normalizing " + juce::String(np.processed) + "/" + juce::String(np.total)
            + " file(s) to -0.1 dBFS"
            + (np.current.isNotEmpty() ? "  |  " + np.current : juce::String()),
            juce::dontSendNotification);
        status.setTooltip(status.getText());
    }
    else if (taggingRunning || taggingWasRunning)
    {
        juce::String stage;
        switch (tg.stage)
        {
            case TaggingStage::Preparing: stage = "Preparing"; break;
            case TaggingStage::Applying: stage = "Applying"; break;
            case TaggingStage::Complete: stage = "Complete"; break;
            case TaggingStage::Cancelled: stage = "Cancelled"; break;
            case TaggingStage::Failed: stage = "Failed"; break;
            case TaggingStage::Idle: break;
        }
        status.setText(tg.operation + (stage.isNotEmpty() ? "  |  " + stage : juce::String())
            + (tg.total > 0 ? "  " + juce::String(tg.processed) + "/" + juce::String(tg.total) : juce::String())
            + (tg.clearing ? "  removed " : "  applied ") + juce::String(tg.applied)
            + "  errors " + juce::String(tg.errors)
            + (tg.running && tg.current.isNotEmpty() ? "  |  " + tg.current : juce::String())
            + (tg.running ? juce::String() : "  |  " + tg.message), juce::dontSendNotification);
        status.setTooltip(status.getText());
    }
    else if (autoTagRunning || autoTagWasRunning)
    {
        status.setText(at.message
            + (at.total > 0 ? "  " + juce::String(at.processed) + "/" + juce::String(at.total) : juce::String())
            + "  proposals " + juce::String(at.proposals) + "  applied " + juce::String(at.applied)
            + "  skipped " + juce::String(at.skipped) + "  errors " + juce::String(at.errors),
            juce::dontSendNotification);
        status.setTooltip(status.getText());
    }
    else if (p.running || wasRunning)
    {
        juce::String text = (p.paused ? "Paused  |  " : juce::String()) + p.message + "  "
            + juce::String(p.processed) + "/" + juce::String(p.total) + "  |  analyzed " + juce::String(p.analyzed)
            + "  skipped " + juce::String(p.skipped) + "  errors " + juce::String(p.failed)
            + "  duplicates " + juce::String(p.duplicates);
        if (p.discImages + p.discSkipped + p.discFailed + p.discEntries > 0)
            text += "  |  disc " + juce::String(p.discEntries) + " entries, "
                + juce::String(p.discImages) + " images"
                + (p.discSkipped > 0 ? ", cached " + juce::String(p.discSkipped) : juce::String())
                + (p.discFailed > 0 ? ", failed " + juce::String(p.discFailed) : juce::String());
        text += p.running ? "  |  " + p.current : juce::String();
        status.setText(text, juce::dontSendNotification);
        status.setTooltip(p.discMessage.isNotEmpty() ? p.discMessage : text);
    }
    else if (smartRunning || smartWasRunning)
    {
        juce::String stage;
        switch (s.stage)
        {
            case SmartStage::Scanning: stage = "Scanning"; break;
            case SmartStage::Verifying: stage = "Verifying"; break;
            case SmartStage::Extracting: stage = "Reading metadata"; break;
            case SmartStage::Publishing: stage = "Saving"; break;
            case SmartStage::Complete: stage = "Complete"; break;
            case SmartStage::Cancelled: stage = "Cancelled"; break;
            case SmartStage::Failed: stage = "Failed"; break;
            case SmartStage::Idle: break;
        }
        status.setText((s.paused ? "Paused  |  " : juce::String()) + stage
            + (s.message.isNotEmpty() ? "  |  " + s.message : juce::String())
            + (s.total > 0 ? "  " + juce::String(s.processed) + "/" + juce::String(s.total) : juce::String())
            + "  errors " + juce::String(s.failed) + "  stale " + juce::String(s.stale)
            + (s.running && s.current.isNotEmpty() ? "  |  " + s.current : juce::String()),juce::dontSendNotification);
        status.setTooltip(status.getText());
    }
    if (batchService != nullptr && batchContent != nullptr)
    {
        const auto bp = batchService->progress();
        batchContent->setProgress(bp);
        if (bp.running)
            status.setText("Batch Processing  |  " + bp.phase
                + (bp.current.isNotEmpty() ? "  |  " + bp.current : juce::String())
                + "  " + juce::String(bp.outputsDone) + "/" + juce::String(bp.outputsTotal),
                juce::dontSendNotification);
        if (batchWasRunning && ! bp.running)
        {
            const auto report = batchService->results();
            batchContent->showReport(report);
            batchWasRunning = false;
            status.setText("Batch Processing finished: " + juce::String(report.completed)
                + " completed, " + juce::String(report.failed) + " failed, "
                + juce::String(report.skipped) + " skipped", juce::dontSendNotification);
            if (report.completed > 0) requestFolderTreeRefresh();
        }
    }
    if (batchPreview != nullptr && batchPreviewWasRunning && ! batchPreview->isRunning())
    {
        const auto previewResult = batchPreview->takeResult();
        batchPreviewWasRunning = false;
        batchPreview.reset();
        const bool discard = pendingPreviewStop || batchClosePending;
        if (! discard && batchContent != nullptr) batchContent->setPreviewReady(previewResult, false);
        if (discard)
        {
            preview.stopPlayback();
            if (batchPreviewAuditioned && batchPreviewRestore.existsAsFile())
            {
                preview.loadFile(batchPreviewRestore, false);
                batchPreviewAuditioned = false;
            }
            pendingPreviewStop = false;
        }
        if (batchPreviewFolder != juce::File())
        {
            batchPreviewFolder.deleteRecursively();
            batchPreviewFolder = juce::File();
        }
        if (pendingBatchLaunch)
        {
            auto request = pendingBatchRequest;
            pendingBatchLaunch = false;
            launchBatch(std::move(request));
        }
    }
    if (batchClosePending && ! isBatchRunning()
        && (batchPreview == nullptr || ! batchPreview->isRunning()))
        closeBatchDialog();
    if (p.running)
    {
        const auto now = juce::Time::getMillisecondCounter();
        if (now - lastRefreshMs >= 1500) { lastRefreshMs = now; refreshIfCatalogChanged(); }
    }
    if (wasRunning && ! p.running)
    {
        // A cancelled scan can still have written complete rows; keep derived groups truthful.
        if (p.cancelled)
        {
            try { writeRepo->refreshDuplicates(); }
            catch (const std::exception& e) { status.setText(juce::String::fromUTF8(e.what()),juce::dontSendNotification); }
        }
        reloadLibrary(); requestFolderTreeRefresh(); refreshFavorites();
        // New or changed audio invalidates its smart result; rebuild metadata in the background.
        if (! p.cancelled && smart && ! smart->progress().running) smart->start();
    }
    wasRunning = p.running;
    if (smartWasRunning && ! smartRunning) reloadLibrary();
    smartWasRunning = smartRunning;
    if (taggingWasRunning && ! taggingRunning) reloadLibrary();
    taggingWasRunning = taggingRunning;
    if (autoTagPreparing && autoTagWasRunning && ! autoTagRunning)
    {
        autoTagPreparing = false;
        AutoTagResult result;
        if (autoTag != nullptr && autoTag->takeResult(result))
        {
            if (result.proposals.empty())
                status.setText(result.message + "  checked " + juce::String(result.checked)
                    + "  no evidence " + juce::String(result.noEvidence), juce::dontSendNotification);
            else
                showAutoTagDialog(std::move(result));
        }
        else
            status.setText(at.message, juce::dontSendNotification);
    }
    autoTagWasRunning = autoTagRunning;
    if (splitWasRunning && ! splitRunning) finishSplit();
    splitWasRunning = splitRunning;
    if (extractWasRunning && ! extractRunning) finishExtract();
    extractWasRunning = extractRunning;
    if (normalizeWasRunning && ! normalizeRunning) finishNormalize();
    normalizeWasRunning = normalizeRunning;
    if (! pendingSplitScan.isEmpty() && splitScanApproved && indexer && ! indexer->progress().running
        && ! (smart && smart->progress().running)
        && ! (tagging && tagging->progress().running)
        && ! (autoTag && autoTag->progress().running)
        && ! isSplitRunning())
    {
        const auto folders = pendingSplitScan;
        pendingSplitScan.clear();
        if (indexer->startFoldersNewOnly(folders))
        {
            wasRunning = true;
            status.setText("Indexing split parts", juce::dontSendNotification);
        }
        else
        {
            pendingSplitScan = folders;
        }
    }
    if (! pendingExtractScan.isEmpty() && ! isExtractRunning() && indexer != nullptr
        && ! indexer->progress().running && ! (smart && smart->progress().running)
        && ! (tagging && tagging->progress().running)
        && ! (autoTag && autoTag->progress().running) && ! isSplitRunning())
    {
        const auto folders = pendingExtractScan;
        pendingExtractScan.clear();
        const auto roots = readRepo != nullptr ? readRepo->roots() : juce::StringArray();
        const auto plan = planExtractFollowUp(folders, roots);
        bool rootsChanged = false;
        if (writeRepo != nullptr)
            for (const auto& root : plan.newRoots)
            {
                writeRepo->addRoot(root);
                rootsChanged = true;
            }
        if (rootsChanged) reloadLibrary();
        if (indexer->startFoldersNewOnly(plan.scanFolders))
        {
            // The standard pass after indexing also runs smart analysis over the new files.
            status.setText("Indexing extracted samples", juce::dontSendNotification);
            wasRunning = true;
        }
        else
        {
            pendingExtractScan.addArray(folders);
        }
    }
    const auto now = juce::Time::getMillisecondCounter();
    if (persistSessionState && sessionDirty && now - lastSessionSaveMs >= 500) saveSessionState();
    if (pendingSearch.trim() != filter.text && now - lastSearchChange >= 250)
    {
        filter.text = pendingSearch.trim();
        pageOffset = 0;
        reloadLibrary();
    }
    finishFolderGenre();
    finishSymlinkFlagRefresh();
    finishCleanupCatalogUpdate();
}
void MainComponent::lookAndFeelChanged()
{
    const bool showBrand = brandedLogo && theme::isDark;
    heading.setVisible(! showBrand); subtitle.setVisible(! showBrand);
    subtitle.setColour(juce::Label::textColourId,theme::muted);
    libraryHeading.setColour(juce::Label::textColourId,theme::muted);
    rootsLabel.setColour(juce::Label::textColourId,theme::muted);
    favoritesLabel.setColour(juce::Label::textColourId,theme::muted);
    favoritesSplitter.repaint();
    status.setColour(juce::Label::textColourId,theme::muted);
    tagsLabel.setColour(juce::Label::textColourId,theme::muted);
    stageLabel.setColour(juce::Label::textColourId,theme::muted);
    search.setTextToShowWhenEmpty("Search: *.rx2, ext:wav, type:pad, key:Am, C4",theme::muted);
    search.setJustification(juce::Justification::centredLeft);
    search.setIndents(10, 0);
    repaint();
}
void MainComponent::darkModeSettingChanged()
{
    if (appSettings.theme == ThemeMode::System) refreshTheme();
}
void MainComponent::refreshTheme()
{
    theme::apply(appSettings.theme);
    look.refreshColours();
    sendLookAndFeelChange();
    repaint();
}
namespace {
juce::String describeContributionOutcome(const contribute::Uploader::Outcome& outcome, bool attempted)
{
    const auto at = juce::Time::getCurrentTime().formatted("%H:%M");
    if (! attempted)
        return "Sending paused: the toggle, consent or install id is missing.";
    if (outcome.sent)
        return "Sent " + juce::String(outcome.recordCount) + " confirmed record(s) at " + at + ".";
    if (outcome.replacedId)
        return "The intake no longer knows this install; a new id was issued and the data is resent.";
    if (outcome.recordCount == 0 && outcome.status == 0)
        return "Nothing new to send at " + at + ".";
    auto message = outcome.status > 0 ? "Not sent at " + at + " - the intake answered "
                                             + juce::String(outcome.status)
                                      : "Not sent at " + at + " - no connection";
    if (outcome.error.isNotEmpty()) message += ": " + outcome.error.substring(0, 120);
    return message + " Retrying in the background.";
}
} // namespace

bool MainComponent::contributionConsentCurrent() const
{
    return appSettings.contributionConsentVersion >= AppSettings::currentContributionConsentVersion;
}

void MainComponent::startContribution()
{
    if (contributionUploader != nullptr || writeRepo == nullptr) return;
    contribute::Uploader::Source source;
    juce::Component::SafePointer<MainComponent> safe(this);
    source.allowed = [safe]()
    {
        auto* self = safe.getComponent();
        if (self == nullptr) return false;
        const juce::ScopedLock lock(self->contributionLock);
        return self->persistSessionState && ! self->embeddedUi
            && self->appSettings.contributeTrainingData && self->contributionConsentCurrent()
            && self->appSettings.contributionInstallId.isNotEmpty();
    };
    source.installId = [safe]()
    {
        if (safe == nullptr) return juce::String();
        const juce::ScopedLock lock(safe->contributionLock);
        return safe->appSettings.contributionInstallId;
    };
    source.storeInstallId = [safe](const juce::String& id)
    {
        if (safe == nullptr) return;
        {
            const juce::ScopedLock lock(safe->contributionLock);
            safe->appSettings.contributionInstallId = id;
        }
        try { safe->settingsStore.update([&id](AppSettings& saved) { saved.contributionInstallId = id; }); }
        catch (...) {}
    };
    source.endpoint = [safe]()
    {
        if (safe == nullptr) return AppSettings::defaultContributionEndpoint();
        return safe->appSettings.contributionEndpoint.isNotEmpty()
            ? safe->appSettings.contributionEndpoint : AppSettings::defaultContributionEndpoint();
    };
    source.watermark = [safe]()
    {
        if (safe == nullptr || safe->writeRepo == nullptr) return (int64_t) 0;
        try { return safe->writeRepo->contributionWatermark(); } catch (...) { return (int64_t) 0; }
    };
    source.commitWatermark = [safe](int64_t value)
    {
        if (safe == nullptr || safe->writeRepo == nullptr) return;
        try { safe->writeRepo->setContributionWatermark(value); } catch (...) {}
    };
    source.fetch = [safe](int64_t after, int limit)
    {
        if (safe == nullptr || safe->writeRepo == nullptr) return std::vector<contribute::Record>();
        std::vector<contribute::Record> out;
        try
        {
            for (auto& row : safe->writeRepo->contributionRecords(after, limit))
            {
                contribute::Record record;
                record.recordId = row.recordId;
                record.type = row.targetType;
                record.typeConfirmed = row.typeConfirmed;
                record.tags = row.tags;
                record.featureSchema = row.featureSchema >= 2 && row.featuresV2.size() == 84 ? 2 : 1;
                record.features = record.featureSchema == 2 ? row.featuresV2 : row.features;
                record.key = row.key;
                record.bpm = row.bpm;
                record.hasBpm = row.hasBpm;
                record.updatedAt = row.updatedAt;
                out.push_back(std::move(record));
            }
        }
        catch (...) {}
        return out;
    };
    source.vocabulary = [safe]()
    {
        contribute::Vocabulary vocabulary;
        // Only names that ship with the app count as standard. A user's custom type must be
        // reported as custom, even though the repository knows it.
        vocabulary.types = standardSampleTypeNames();
        vocabulary.tags = {};
        if (safe != nullptr && safe->readRepo != nullptr)
        {
            try
            {
                vocabulary.tags = safe->readRepo->builtinTagNames();
            }
            catch (...) {}
        }
        return vocabulary;
    };
    // The intake accepts exactly the three names platformName() can return.
    const auto osName = contribute::platformName();
    contributionUploader = std::make_shared<contribute::Uploader>(
        std::make_unique<contribute::WebTransport>(), std::move(source), version(), osName);
    contributionScheduler = std::make_unique<contribute::Scheduler>(
        *contributionUploader, [safe]()
        {
            auto* self = safe.getComponent();
            return self != nullptr && self->appSettings.contributeTrainingData
                && self->contributionConsentCurrent() && self->appSettings.contributionInstallId.isNotEmpty();
        });
    // The scheduler thread owns no UI: marshal the one-line result to the message thread.
    contributionScheduler->onOutcome = [safe](const contribute::Uploader::Outcome& outcome, bool attempted)
    {
        if (safe == nullptr) return;
        const auto message = describeContributionOutcome(outcome, attempted);
        juce::MessageManager::callAsync([safe, message, attempted]() mutable
        {
            if (safe != nullptr) safe->reportContributionOutcome(message, attempted);
        });
    };
    contributionScheduler->start();
}

void MainComponent::requestContributionUpload(bool force)
{
    // Only the application sends; a plugin shares the catalog and leaves the records for it.
    if (embeddedUi || ! persistSessionState) return;
    if (contributionUploader == nullptr) startContribution();
    if (contributionScheduler == nullptr) return;
    // A tag assignment or a type confirmation arrives in bursts; the intake allows 30 requests an
    // hour from one address, so one request a minute is enough and keeps the page honest.
    const auto now = juce::Time::currentTimeMillis();
    const auto last = contributionRequestedAt.load();
    if (! force && last > 0 && now - last < 60 * 1000) return;
    contributionRequestedAt = now;
    contributionScheduler->requestNow();
}

void MainComponent::reportContributionOutcome(const juce::String& message, bool attempted)
{
    // Nothing is tried while the toggle is off; that is not news for the panel.
    if (! attempted && ! appSettings.contributeTrainingData) return;
    contributionStatus = message;
    juce::Logger::writeToLog("contribution: " + message);
    if (settingsPanel != nullptr)
        settingsPanel->setContributionStatus(message);
}

void MainComponent::askContributionConsent()
{
    if (contributionConsentCurrent()) return;
    const auto message = juce::String(
        "Sonicator can send anonymous training data: the type you set, the tags you assign or accept "
        "from an AutoTag suggestion, tempo and key of a sample, plus 14 acoustic measurements as "
        "plain numbers. Audio, file names, folder names and paths are never sent, and nothing "
        "identifies you or your computer.\n\n"
        "Confirmations you make from now on are included. Tag assignments from older versions are "
        "not uploaded, and rows with unknown origin stay on your computer. You can review the exact "
        "data, stop at any time and delete what was sent in Settings > Library.\n\n"
        "Participate in training data collection?");
    juce::Component::SafePointer<MainComponent> safe(this);
    dialogs::showAlert(*this, "Help train the model", message,
        {{ "Not now", 0 }, { "Participate", 1 }},
        [safe](int result)
        {
            if (safe == nullptr) return;
            const bool accepted = result == 1;
            auto& settings = safe->appSettings;
            settings.contributionConsentVersion = AppSettings::currentContributionConsentVersion;
            settings.contributeTrainingData = accepted;
            if (accepted && settings.contributionInstallId.isEmpty())
                settings.contributionInstallId = contribute::Uploader::makeUuid();
            try
            {
                safe->settingsStore.update([&settings](AppSettings& saved)
                {
                    saved.contributionConsentVersion = settings.contributionConsentVersion;
                    saved.contributeTrainingData = settings.contributeTrainingData;
                    saved.contributionInstallId = settings.contributionInstallId;
                });
            }
            catch (...) {}
            if (safe != nullptr && accepted)
            {
                safe->contributionStatus.clear();
                safe->requestContributionUpload(true);
            }
        });
}

void MainComponent::setContributionEnabledFromPanel(bool wanted,
                                                    juce::Component::SafePointer<SettingsPanel> panel)
{
    if (wanted && ! contributionConsentCurrent())
    {
        askContributionConsent();
        if (panel != nullptr) panel->setContributionEnabled(appSettings.contributeTrainingData);
        return;
    }
    appSettings.contributeTrainingData = wanted;
    if (wanted && appSettings.contributionInstallId.isEmpty())
        appSettings.contributionInstallId = contribute::Uploader::makeUuid();
    try
    {
        settingsStore.update([this](AppSettings& saved)
        {
            saved.contributeTrainingData = appSettings.contributeTrainingData;
            saved.contributionInstallId = appSettings.contributionInstallId;
        });
    }
    catch (...) {}
    if (panel != nullptr) panel->setContributionEnabled(appSettings.contributeTrainingData);
    // Turning the toggle on sends what is already waiting instead of waiting out the daily cycle.
    if (appSettings.contributeTrainingData)
    {
        contributionStatus.clear();
        requestContributionUpload(true);
    }
}

void MainComponent::deleteContributionData(std::function<void(bool, juce::String)> done)
{
    if (contributionUploader == nullptr)
    {
        if (done) done(false, "Contribution is not available.");
        return;
    }
    auto uploader = contributionUploader;
    juce::Component::SafePointer<MainComponent> safe(this);
    juce::Thread::launch([uploader, safe, done]() mutable
    {
        const auto outcome = uploader->deleteMine();
        const bool ok = outcome.sent;
        const auto error = outcome.error;
        juce::MessageManager::callAsync([safe, ok, error, done]() mutable
        {
            if (safe != nullptr && ok)
            {
                {
                    const juce::ScopedLock lock(safe->contributionLock);
                    safe->appSettings.contributeTrainingData = false;
                    safe->appSettings.contributionInstallId =
                        safe->settingsStore.load().contributionInstallId;
                }
                try { safe->settingsStore.update([](AppSettings& saved) { saved.contributeTrainingData = false; }); }
                catch (...) {}
            }
            if (done) done(ok, error);
        });
    });
}

void MainComponent::wireContributionPanel(SettingsPanel& panel)
{
    juce::Component::SafePointer<SettingsPanel> safePanel(&panel);
    panel.setContributionEnabled(appSettings.contributeTrainingData && contributionConsentCurrent());
    panel.setContributionStatus(contributionStatus);
    panel.onContributionToggle = [this, safePanel](bool wanted)
    {
        setContributionEnabledFromPanel(wanted, safePanel);
    };
    panel.onContributionPreview = [this]()
    {
        if (contributionUploader == nullptr) startContribution();
        return contributionUploader != nullptr ? contributionUploader->previewJson() : juce::String("{}");
    };
    panel.onContributionDelete = [this, safePanel](std::function<void(bool, juce::String)> done)
    {
        deleteContributionData([safePanel, done](bool ok, juce::String error)
        {
            if (safePanel != nullptr) safePanel->setContributionEnabled(false);
            if (done) done(ok, error);
        });
    };
}

void MainComponent::applySettings(const AppSettings& next)
{
    // The settings dialog edits theme, scale and language only. Other fields (catalog path,
    // filters, page size, preview toggles, window bounds) belong to their own writers and must
    // not be replaced by a stale copy taken when the dialog opened.
    appSettings.theme = next.theme;
    appSettings.scalePercent = AppSettings::clampScale(next.scalePercent);
    appSettings.language = next.language;
    appSettings.alwaysOverwriteOriginalFiles = next.alwaysOverwriteOriginalFiles;
    appSettings.overwriteOriginalDisposal = next.overwriteOriginalDisposal;
    appSettings.roundTagMenu = next.roundTagMenu;
    appSettings.spectralCache = next.spectralCache;
    preview.setSpectralCacheEnabled(appSettings.spectralCache);
    if (! appSettings.roundTagMenu && roundTagMenu != nullptr)
        roundTagMenu->close();
    appSettings.searchHistoryOnClick = next.searchHistoryOnClick;
    search.setHistoryOnClick(appSettings.searchHistoryOnClick);
    search.setTooltip(appSettings.searchHistoryOnClick
        ? "Click for the last 10 searches; Escape dismisses history"
        : "Click to type; the clock button opens the last 10 searches");
    preview.setAlwaysOverwriteOriginalFiles(appSettings.alwaysOverwriteOriginalFiles);
    preview.setOverwriteDisposal(appSettings.overwriteOriginalDisposal);
    refreshTheme();
    // The global scale factor belongs to the whole process: inside a DAW it would resize the
    // host's own windows. The plugin follows the host's scaling instead.
    if (! embeddedUi)
        juce::Desktop::getInstance().setGlobalScaleFactor(
            juce::jlimit(0.75f, 2.0f, appSettings.scalePercent / 100.0f));
    else if (onInterfaceScaleChanged)
        onInterfaceScaleChanged(interfaceScale());
    try
    {
        settingsStore.update([this](AppSettings& saved)
        {
            saved.theme = appSettings.theme;
            saved.scalePercent = appSettings.scalePercent;
            saved.language = appSettings.language;
            saved.alwaysOverwriteOriginalFiles = appSettings.alwaysOverwriteOriginalFiles;
            saved.overwriteOriginalDisposal = appSettings.overwriteOriginalDisposal;
            saved.roundTagMenu = appSettings.roundTagMenu;
            saved.searchHistoryOnClick = appSettings.searchHistoryOnClick;
            saved.spectralCache = appSettings.spectralCache;
        });
    }
    catch (const std::exception& e)
    {
        status.setText("Settings applied but not saved: " + juce::String::fromUTF8(e.what()),juce::dontSendNotification);
    }
}
void MainComponent::showEmbeddedSettings()
{
    if (embeddedSettingsHost != nullptr) { embeddedSettingsHost->toFront(true); return; }
    auto panel = std::make_unique<SettingsPanel>(appSettings,dbPath,indexer && indexer->progress().running);
    panel->setComponentID("settingsPanel");
    panel->onSettingsChanged = [this](AppSettings next) { applySettings(next); };
    wireContributionPanel(*panel);
    wireCommandSettings(*panel);
    panel->setLookAndFeel(&look);
    panel->setPluginMode(true);
    panel->onClose = [safe = juce::Component::SafePointer<MainComponent>(this)]
    {
        if (safe != nullptr) safe->closeEmbeddedSettings();
    };
    auto overlay = std::make_unique<EmbeddedDialogOverlay>(std::move(panel));
    overlay->setComponentID("settingsOverlay");
    overlay->onDismiss = [safe = juce::Component::SafePointer<MainComponent>(this)]
    {
        if (safe != nullptr) safe->closeEmbeddedSettings();
    };
    addAndMakeVisible(*overlay);
    overlay->setBounds(getLocalBounds());
    embeddedSettingsPanel = dynamic_cast<SettingsPanel*>(overlay->getChildComponent(0));
    embeddedSettingsHost = overlay.get();
    overlay.release()->toFront(true);
}
void MainComponent::closeEmbeddedSettings()
{
    embeddedSettingsPanel = nullptr;
    auto* host = embeddedSettingsHost.getComponent();
    embeddedSettingsHost = nullptr;
    resized();
    if (host == nullptr) return;
    // The close button runs inside its own click callback: delete the overlay after the event
    // finishes dispatching, and only while it is still alive.
    juce::Component::SafePointer<juce::Component> safeHost(host);
    juce::MessageManager::callAsync([safeHost]() mutable
    {
        if (safeHost != nullptr) delete safeHost.getComponent();
    });
}
bool MainComponent::showEmbeddedDialog(std::unique_ptr<juce::Component> content)
{
    if (! embeddedUi || content == nullptr) return false;
    // One dialog at a time: a second prompt replaces the first instead of stacking on top of it.
    if (embeddedDialogHost != nullptr) closeEmbeddedDialog();
    if (auto* dialog = dynamic_cast<dialogs::EmbeddedDialogContent*>(content.get()))
        dialog->onClose = [safe = juce::Component::SafePointer<MainComponent>(this)]
        {
            if (safe != nullptr) safe->closeEmbeddedDialog();
        };
    content->setLookAndFeel(&look);
    auto overlay = std::make_unique<EmbeddedDialogOverlay>(std::move(content));
    overlay->setComponentID("embeddedDialog");
    overlay->onDismiss = [safe = juce::Component::SafePointer<MainComponent>(this)]
    {
        if (safe != nullptr) safe->closeEmbeddedDialog();
    };
    addAndMakeVisible(*overlay);
    overlay->setBounds(getLocalBounds());
    embeddedDialogHost = overlay.get();
    overlay.release()->toFront(true);
    // An alert has no field of its own, so the overlay takes the keys: Escape dismisses it. A
    // prompt keeps the focus its text field already took.
    if (embeddedDialogHost != nullptr && embeddedDialogHost->isShowing())
    {
        auto* focused = juce::Component::getCurrentlyFocusedComponent();
        if (focused == nullptr || ! embeddedDialogHost->isParentOf(focused))
            embeddedDialogHost->grabKeyboardFocus();
    }
    return true;
}
void MainComponent::closeEmbeddedDialog()
{
    auto* host = embeddedDialogHost.getComponent();
    embeddedDialogHost = nullptr;
    if (host == nullptr) return;
    juce::Component::SafePointer<juce::Component> safeHost(host);
    juce::MessageManager::callAsync([safeHost]() mutable
    {
        if (safeHost != nullptr) delete safeHost.getComponent();
    });
}
void MainComponent::wireCommandSettings(SettingsPanel& panel)
{
    panel.setCommandRegistry(&commands);
    panel.setShortcutProfile(storedShortcutProfile);
    panel.setShortcutDiagnostics(shortcutDiagnostics);
    panel.onSectionChanged = [this](SettingsSection section)
    {
        appSettings.lastSettingsSection = settingsSectionId(section);
        try
        {
            settingsStore.update([section](AppSettings& saved)
            {
                saved.lastSettingsSection = settingsSectionId(section);
            });
        }
        catch (...) {}
    };
    panel.onShortcutProfileChanged = [this](const ShortcutProfile& candidate)
    {
        return saveShortcutProfile(candidate);
    };
    panel.onResetLayout = [this] { resetLayout(); };
    if (!embeddedUi)
        panel.setAudioSettingsFactory([this]()
        {
            return std::unique_ptr<juce::Component>(new juce::AudioDeviceSelectorComponent(
                engine.deviceManager(), 0, 0, 0, 2, true, false, true, false));
        });
}

void MainComponent::showSettings(SettingsSection section)
{
    if (embeddedUi) { showEmbeddedSettings(); return; }
    if (settingsDialog != nullptr)
    {
        if (settingsPanel != nullptr) settingsPanel->selectSection(section);
        settingsDialog->toFront(true);
        return;
    }
    showSettings();
    if (settingsPanel != nullptr) settingsPanel->selectSection(section);
}

void MainComponent::showSettings()
{
    if (embeddedUi) { showEmbeddedSettings(); return; }
    if (settingsDialog != nullptr) { settingsDialog->toFront(true); return; }
    const bool indexing = indexer && indexer->progress().running;
    auto* panel = new SettingsPanel(appSettings,dbPath,indexing);
    panel->setComponentID("settingsPanel");
    panel->onSettingsChanged = [this](AppSettings next) { applySettings(next); };
    panel->onShowAudio = [this] { showAudioSettings(); };
    wireContributionPanel(*panel);
    wireCommandSettings(*panel);
    panel->onBackupRequested = [this](juce::String& error) -> juce::File
    {
        try { return backupCatalog(); }
        catch (const std::exception& e)
        {
            error = juce::String::fromUTF8(e.what());
            return {};
        }
    };
    panel->onRelocateRequested = [this]
    {
        if (settingsDialog != nullptr) settingsDialog->exitModalState(0);
        juce::MessageManager::callAsync([safe = juce::Component::SafePointer<MainComponent>(this)]
        {
            if (safe != nullptr) safe->showRelocateDialog();
        });
    };
    panel->onDatabaseChosen = [this, safePanel = juce::Component::SafePointer<SettingsPanel>(panel)]
        (const juce::File& file, bool moveCurrent)
        {
            juce::String error;
            if (switchCatalog(file,moveCurrent,error))
            {
                if (safePanel != nullptr)
                {
                    safePanel->setDatabaseFile(dbPath);
                    safePanel->setDatabaseStatus(moveCurrent
                        ? "Catalog copied and switched. The previous file was kept."
                        : "Catalog opened.",false);
                }
                if (! moveCurrent && ! missingRoots().isEmpty() && settingsDialog != nullptr)
                    settingsDialog->exitModalState(0);
            }
            else if (safePanel != nullptr) safePanel->setDatabaseStatus(error,true);
        };
    panel->setLookAndFeel(&look);
    settingsPanel = panel;
    // The navigation column and each page's own viewport keep the window inside the display, so
    // there is no outer scroll area to nest.
    const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(getScreenBounds());
    const auto area = display != nullptr ? display->userArea : juce::Rectangle<int>(0, 0, 1280, 800);
    const int width = juce::jmin(940, juce::jmax(700, area.getWidth() - 80));
    const int height = juce::jmin(760, juce::jmax(460, area.getHeight() - 80));
    panel->setSize(width, height);
    auto* window = new SettingsDialogWindow();
    window->setContentOwned(panel, true);
    window->setLookAndFeel(&look);
    window->setUsingNativeTitleBar(false);
    window->setResizable(true, false);
    window->setResizeLimits(700, 460, area.getWidth(), area.getHeight());
    window->centreAroundComponent(this, window->getWidth(), window->getHeight());
    settingsDialog = window;
    window->enterModalState(true, nullptr, true);
}
bool MainComponent::switchCatalog(const juce::File& target, bool moveCurrent, juce::String& error)
{
    error.clear();
    if (folderGenreWork.valid())
    {
        error = "Wait for folder genre assignment to finish before switching catalogs.";
        return false;
    }
    if (cleanupCatalogWork.valid())
    {
        error = "Wait for the catalog update to finish before switching catalogs.";
        return false;
    }
    if (indexer && indexer->progress().running)
    {
        error = "Stop indexing before switching catalogs.";
        return false;
    }
    if (isSplitRunning())
    {
        error = "Wait for Split to finish before switching catalogs.";
        return false;
    }
    if (target.getFullPathName().isEmpty())
    {
        error = "Choose a database file.";
        return false;
    }
    if (target.getFullPathName() == dbPath.getFullPathName())
    {
        error = "This catalog is already open.";
        return false;
    }
    if (moveCurrent && target.existsAsFile())
    {
        error = "Choose a new file for the catalog copy.";
        return false;
    }
    if (! moveCurrent && ! SampleRepository::isCatalog(target))
    {
        error = target.existsAsFile() ? "That file is not a SONICATOR catalog." : "That catalog does not exist.";
        return false;
    }
    if (moveCurrent)
    {
        try { writeRepo->backupTo(target); }
        catch (const std::exception& e)
        {
            error = "Cannot copy the catalog: " + juce::String::fromUTF8(e.what());
            return false;
        }
    }
    std::unique_ptr<Database> newWriteDb, newReadDb;
    std::unique_ptr<SampleRepository> newWriteRepo, newReadRepo;
    try
    {
        newWriteDb = std::make_unique<Database>(target);
        newWriteRepo = std::make_unique<SampleRepository>(*newWriteDb);
        newWriteRepo->createTables();
        purgeUnsupportedRows(*newWriteRepo);
        newReadDb = std::make_unique<Database>(target);
        newReadRepo = std::make_unique<SampleRepository>(*newReadDb);
    }
    catch (const std::exception& e)
    {
        error = "Cannot open the catalog: " + juce::String::fromUTF8(e.what());
        if (moveCurrent) error += " The copy was kept at " + target.getFullPathName();
        return false;
    }
    if (autoTagDialog != nullptr) { autoTagDialog->exitModalState(0); autoTagDialog = nullptr; }
    closeSplitDialog();
    if (splitter) { splitter->cancel(); splitter->waitUntilFinished(-1); splitter.reset(); }
    pendingSplitScan.clear();
    splitScanApproved = false;
    splitWasRunning = false;
    if (autoTag) { autoTag->cancel(); autoTag->waitUntilFinished(-1); autoTag.reset(); }
    if (tagging) { tagging->cancel(); tagging->waitUntilFinished(-1); tagging.reset(); }
    if (smart) { smart->cancel(); smart->waitUntilFinished(-1); smart.reset(); }
    if (indexer) { indexer->cancel(); indexer->waitUntilFinished(-1); indexer.reset(); }
    if (contributionScheduler != nullptr)
    {
        contributionScheduler->stop();
        contributionScheduler.reset();
    }
    contributionUploader.reset();
    cancelFeedback = true;
    {
        std::lock_guard lock(catalogSyncMutex);
        catalogSyncStop = true;
    }
    if (catalogSyncWorker && catalogSyncWorker->joinable()) catalogSyncWorker->join();
    catalogSyncRequests.clear();
    if (typeFeedbackWorker && typeFeedbackWorker->joinable()) typeFeedbackWorker->join();
    cancelFeedback = false;
    folderIndexRunner.reset();
    writeRepo.reset(); writeDb.reset(); readRepo.reset(); readDb.reset();
    writeDb = std::move(newWriteDb); writeRepo = std::move(newWriteRepo);
    readDb = std::move(newReadDb); readRepo = std::move(newReadRepo);
    lastFolderIndex = {};
    pendingTreeSelection.clear();
    folderIndexRunner = std::make_unique<FolderIndexRunner>(*readRepo);
    AcousticClassifierService::getSharedAdaptiveClassifier().loadFromRepository(*readRepo);
    loadShippedGlobalPrototypes();
    {
        // The queue may run again against the new catalog; requests for the old one were dropped.
        std::lock_guard lock(catalogSyncMutex);
        catalogSyncStop = false;
    }
    indexer = std::make_unique<IndexingService>(*writeRepo);
    smart = std::make_unique<SmartAnalysisService>(*writeRepo);
    tagging = std::make_unique<TaggingService>(*writeRepo);
    tagging->onTagsChanged = [safe = juce::Component::SafePointer<MainComponent>(this)](std::vector<int64_t> ids)
    {
        juce::MessageManager::callAsync([safe, ids = std::move(ids)]
        {
            if (safe != nullptr) safe->triggerAdaptiveLearning(ids);
        });
    };
    autoTag = std::make_unique<AutoTagService>(*writeRepo);
    splitter = std::make_unique<SplitService>();
    taggingWasRunning = false;
    autoTagPreparing = false;
    autoTagWasRunning = false;
    const auto previous = dbPath;
    dbPath = target;
    const auto isDefault = dbPath.getFullPathName() == defaultDatabaseFile().getFullPathName();
    appSettings.databasePath = isDefault ? juce::String() : dbPath.getFullPathName();
    selectedFolder.clear(); selectedSampleId = 0; pageOffset = 0;
    sessionSamplePath.clear(); markSessionStateDirty();
    favorites.clearSelection();
    filter.text.clear(); filter.includeTags.clear(); filter.excludeTags.clear();
    filter.matchAnyTag = false; filter.starredOnly = false;
    filter.hideDuplicates = false; filter.hideEmpty = appSettings.hideEmpty;
    filter.showLoops = appSettings.showLoops; filter.showOneShots = appSettings.showOneShots;
    filter.types.clear();
    updateTypeFilterChip();
    filter.ignoreSymlinks = appSettings.ignoreSymlinks; filter.duplicateHash.clear();
    search.clear(); pendingSearch.clear();
    showAnalysisPlaceholder();
    selectedTags.setTags({});
    writeRepo->refreshDuplicates();
    requestFolderTreeRefresh();
    refreshFavorites();
    reloadLibrary();
    if (filter.ignoreSymlinks && readRepo != nullptr && ! readRepo->symlinkFlagsComplete())
        startSymlinkFlagRefresh();
    juce::String message = "Catalog: " + dbPath.getFullPathName();
    if (moveCurrent) message += "  |  previous catalog kept at " + previous.getFullPathName();
    try
    {
        settingsStore.update([this](AppSettings& saved) { saved.databasePath = appSettings.databasePath; });
    }
    catch (const std::exception& e)
    {
        message += "  |  settings were not saved: " + juce::String::fromUTF8(e.what());
    }
    status.setText(message,juce::dontSendNotification);
    if (! moveCurrent && ! missingRoots().isEmpty())
    {
        juce::MessageManager::callAsync([safe = juce::Component::SafePointer<MainComponent>(this)]
        {
            if (safe != nullptr) safe->showRelocateDialog();
        });
    }
    // The contribution thread was stopped with the old repositories; the new catalog needs its own.
    startContribution();
    return true;
}
void MainComponent::showAudioSettings()
{
    if (embeddedUi) return; // the host owns the audio and MIDI devices
    if (audioDialog != nullptr) { audioDialog->toFront(true); return; }
    auto* selector = new juce::AudioDeviceSelectorComponent(engine.deviceManager(),0,0,0,2,true,false,true,false);
    selector->setLookAndFeel(&look);
    selector->setSize(460,500);
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(selector); options.dialogTitle = "Audio/MIDI Settings";
    options.dialogBackgroundColour = theme::panel; options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = false; options.resizable = false; audioDialog = options.launchAsync();
    if (audioDialog != nullptr) audioDialog->setLookAndFeel(&look);
}
void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(theme::background);
    g.setColour(theme::panel);
    g.fillRoundedRectangle(sidebarBounds.toFloat(), 10.0f);
    g.fillRoundedRectangle(analysisBounds.toFloat(), 10.0f);
    g.setColour(theme::line); g.drawHorizontalLine(headerBounds.getBottom(),0,(float) getWidth());
    if (! filterSeparatorBounds.isEmpty())
        g.fillRect(filterSeparatorBounds);
    if (logoWordmark.isValid() && theme::isDark)
        g.drawImage(logoWordmark,
                    juce::Rectangle<float>((float) studio::spacingWide,
                                           (float) headerBounds.getY() + 6.0f,
                                           (float) studio::brandingWidth - 8.0f,
                                           (float) studio::headerHeight - 12.0f),
                    juce::RectanglePlacement::onlyReduceInSize | juce::RectanglePlacement::centred);
}
void MainComponent::resized()
{
    if (roundTagMenu != nullptr)
    {
        roundTagMenu->setBounds(getLocalBounds());
        roundTagMenu->close();
    }
    if (embeddedSettingsHost != nullptr)
        embeddedSettingsHost->setBounds(getLocalBounds());
    if (embeddedDialogHost != nullptr)
        embeddedDialogHost->setBounds(getLocalBounds());
    auto r = getLocalBounds(); headerBounds = r.removeFromTop(studio::headerHeight);
    auto header = headerBounds.reduced(studio::spacingWide, 14);
    auto branding = header.removeFromLeft(studio::brandingWidth);
    header.removeFromLeft(studio::spacing);
    heading.setBounds(branding.removeFromTop(30)); subtitle.setBounds(branding);
    settings.setBounds(header.removeFromRight(92)
                             .withSizeKeepingCentre(92, studio::controlHeight));
    header.removeFromRight(studio::spacingWide);
    search.setBounds(header.withSizeKeepingCentre(header.getWidth(), 32));
    selectionStatus.setBounds(r.removeFromBottom(24).reduced(12,1));
    // One model decides the split; the dividers only edit its preference. Effective sizes are
    // recomputed here, so shrinking the window never writes a smaller value back to settings.
    layoutBounds = computePanelLayout(r, layoutPreferences);
    const int previewHeight = juce::jlimit(0, juce::jmax(0, r.getHeight() - 40), layoutBounds.preview.getHeight());
    preview.setBounds(r.removeFromBottom(previewHeight));
    previewDivider.setVisible(previewHeight > 0 && r.getHeight() > panelDividerThickness);
    previewDivider.setBounds(r.removeFromBottom(previewDivider.isVisible() ? panelDividerThickness : 0));
    auto footer = r.removeFromBottom(42).reduced(12,5);
    cancel.setBounds(footer.removeFromRight(70)); footer.removeFromRight(6);
    pause.setBounds(footer.removeFromRight(70)); footer.removeFromRight(6);
    activityDot.setBounds(footer.removeFromLeft(12).withSizeKeepingCentre(8, 8));
    footer.removeFromLeft(6);
    status.setBounds(footer);
    sidebarBounds = r.removeFromLeft(layoutBounds.folders.getWidth());
    layoutBounds.folders = sidebarBounds;
    foldersDivider.setVisible(r.getWidth() > panelDividerThickness * 4);
    foldersDivider.setBounds(r.removeFromLeft(foldersDivider.isVisible() ? panelDividerThickness : 0));
    foldersDivider.setColour(juce::ResizableWindow::backgroundColourId, theme::background);
    analysisBounds = r.removeFromRight(layoutBounds.analysis.getWidth());
    layoutBounds.analysis = analysisBounds;
    analysisDivider.setVisible(r.getWidth() > panelDividerThickness * 4);
    analysisDivider.setBounds(r.removeFromRight(analysisDivider.isVisible() ? panelDividerThickness : 0));
    layoutBounds.library = r;
    layoutBounds.preview = preview.getBounds();
    auto sidebar = sidebarBounds.reduced(14,16);
    libraryMenu.setBounds(sidebar.removeFromTop(40)); sidebar.removeFromTop(16);
    if (favorites.isVisible() && ! appSettings.favoriteFolders.isEmpty())
    {
        const int maxHeight = juce::jmax(80, sidebar.getHeight() - 24);
        favoritePanelHeight = juce::jlimit(60, maxHeight, favoritePanelHeight);
        auto area = sidebar.removeFromBottom(favoritePanelHeight);
        favoritesSplitter.setVisible(true);
        favoritesSplitter.setBounds(sidebar.removeFromBottom(10));
        favoritesLabel.setBounds(area.removeFromTop(18));
        favorites.setBounds(area);
    }
    else favoritesSplitter.setVisible(false);
    folders.setBounds(sidebar);
    auto analysis = analysisBounds.reduced(16,16);
    analysisHeading.setBounds(analysis.removeFromTop(30)); analysis.removeFromTop(8);
    editTags.setBounds(analysis.removeFromTop(30)); analysis.removeFromTop(10);
    tagsLabel.setBounds(analysis.removeFromTop(16)); analysis.removeFromTop(4);
    selectedTags.setBounds(analysis.removeFromTop(selectedTags.getPreferredHeight(analysis.getWidth(), 2)));
    analysis.removeFromTop(8);
    const int metadataHeight = getHeight() < 700 ? 58 : 90;
    auto& sources = preview.containerSourceList();
    if (sources.isVisible())
    {
        const int sourceHeight = juce::jmin(220, juce::jmax(70, (analysis.getHeight() - metadataHeight) / 2));
        sources.setBounds(analysis.removeFromBottom(sourceHeight));
        analysis.removeFromBottom(10);
    }
    stageLabel.setBounds(analysis.removeFromBottom(metadataHeight));
    // The hero block uses the free space between the tags and SMART METADATA.
    if (keyBpm.isVisible())
    {
        analysis.removeFromBottom(studio::spacingTight);
        keyBpm.setBounds(analysis.removeFromBottom(KeyBpmBlock::preferredHeight()));
    }
    analysisViewport.setBounds(analysis);
    const int cardWidth = juce::jmax(1, analysis.getWidth() - 14);
    analysisCard.setBounds(0, 0, cardWidth, analysisCard.preferredHeight(cardWidth));
    auto list = r.reduced(14,14); auto bar = list.removeFromTop(32);
    listTab.setBounds(bar.removeFromLeft(58).withSizeKeepingCentre(58, studio::chipSmallHeight));
    bar.removeFromLeft(studio::spacingTight);
    cloudTab.setBounds(bar.removeFromLeft(66).withSizeKeepingCentre(66, studio::chipSmallHeight));
    bar.removeFromLeft(studio::spacing);
    pageSizeBox.setBounds(bar.removeFromRight(92).withSizeKeepingCentre(92, studio::controlHeight));
    bar.removeFromRight(studio::spacingTight);
    nextPage.setBounds(bar.removeFromRight(studio::iconButtonSize));
    bar.removeFromRight(studio::spacingUnit);
    previousPage.setBounds(bar.removeFromRight(studio::iconButtonSize));
    countLabel.setBounds(bar);
    list.removeFromTop(4);
    // Chips measure themselves from their text, so "Tags (2)" and the page count never clip.
    auto chipWidth = [](const juce::Button& button, int extra)
    {
        return juce::jmax(studio::chipMinWidth,
                          juce::GlyphArrangement::getStringWidthInt(studio::controlFont(),
                                                                    button.getButtonText())
                              + 2 * studio::spacing + extra);
    };
    auto chipNeeds = [&chipWidth](const juce::Button& chip, int extra)
    {
        return chipWidth(chip, extra) + studio::spacingTight;
    };
    const int clearNeeds = studio::spacingUnit + 1 + studio::spacingTight + chipWidth(clearFilters, 0);
    // Clear filters is also in the LIBRARY menu, so it is the last control to give up its place.
    // The link chip yields before it; at a narrow width both stay reachable through that menu.
    const int linkNeeds = chipNeeds(ignoreSymlinks, 24);
    const int contentNeeds = chipNeeds(tagFilter, 0) + chipNeeds(starredButton, 0)
                           + chipNeeds(typeFilter, 0);
    const int visibilityNeeds = chipNeeds(hideEmpty, 24) + chipNeeds(showLoops, 24)
                              + chipNeeds(showOneShots, 24);
    // One row when the whole set fits. Otherwise the visibility filters (Hide empty, the structure
    // pair) move to a second row instead of a chip being clipped to nothing: a filter a user cannot
    // see is a filter they will report as broken.
    const bool oneRow = contentNeeds + visibilityNeeds + clearNeeds + linkNeeds <= list.getWidth();
    auto filterBar = list.removeFromTop(oneRow ? studio::chipHeight
                                               : studio::chipHeight * 2 + studio::spacingTight);
    auto placeChip = [&chipWidth](juce::Rectangle<int>& bar, juce::Button& chip, int extra)
    {
        chip.setBounds(bar.removeFromLeft(chipWidth(chip, extra)));
        bar.removeFromLeft(studio::spacingTight);
    };
    auto secondRow = oneRow ? juce::Rectangle<int>()
                            : filterBar.removeFromBottom(studio::chipHeight);
    if (! oneRow) filterBar.removeFromBottom(studio::spacingTight);
    auto contentRow = filterBar;
    placeChip(contentRow, tagFilter, 0);
    placeChip(contentRow, starredButton, 0);
    placeChip(contentRow, typeFilter, 0);
    if (oneRow)
    {
        placeChip(contentRow, hideEmpty, 24);
        placeChip(contentRow, showLoops, 24);
        placeChip(contentRow, showOneShots, 24);
    }
    else
    {
        auto visibilityRow = secondRow;
        placeChip(visibilityRow, hideEmpty, 24);
        placeChip(visibilityRow, showLoops, 24);
        placeChip(visibilityRow, showOneShots, 24);
    }
    filterSeparatorBounds = {};
    ignoreSymlinks.setVisible(contentRow.getWidth() >= linkNeeds + clearNeeds);
    if (ignoreSymlinks.isVisible()) placeChip(contentRow, ignoreSymlinks, 24);
    clearFilters.setVisible(contentRow.getWidth() >= clearNeeds);
    if (clearFilters.isVisible())
    {
        contentRow.removeFromLeft(studio::spacingUnit);
        filterSeparatorBounds = contentRow.removeFromLeft(1).withSizeKeepingCentre(1, 20);
        contentRow.removeFromLeft(1 + studio::spacingTight);
        placeChip(contentRow, clearFilters, 0);
    }
    list.removeFromTop(8); library.setBounds(list);
    if (discBrowser != nullptr) discBrowser->setBounds(library.getBounds());
    if (cloudPanel != nullptr) cloudPanel->setBounds(library.getBounds());
}

void MainComponent::setPluginMatchContext(std::function<PluginMatchPreferences()> getPreferences,
                                          std::function<void (const PluginMatchPreferences&)> setPreferences,
                                          std::function<plugin::HostTempoValue()> hostTempo)
{
    PluginMatchContext context;
    context.getPreferences = std::move (getPreferences);
    context.setPreferences = std::move (setPreferences);
    context.hostTempo = std::move (hostTempo);
    context.sourceInfo = [safe = juce::Component::SafePointer<MainComponent>(this)] (
        const juce::File& file, int64_t& sourceSize, int64_t& sourceModified) -> std::optional<MatchSource>
    {
        if (safe == nullptr) return std::nullopt;
        return safe->buildMatchSource (file, sourceSize, sourceModified);
    };
    preview.setPluginMatchContext (std::move (context));
}

void MainComponent::applyPluginMatchPreferences(const PluginMatchPreferences& preferences)
{
    preview.applyPluginMatchPreferences (preferences);
}

bool MainComponent::matchControlsVisible() const
{
    return preview.matchControlsVisible();
}

bool MainComponent::beginMatchedDrag(const std::vector<SampleRecord>& records,
                                     juce::Component* source)
{
    // No context means the standalone window: the existing drag path is not touched at all.
    if (! preview.matchControlsVisible()) return false;
    const auto prefs = preview.matchPanel()->preferences();
    const bool matchOn = prefs.matchBpm || prefs.matchScale;
    if (! matchOn) return false;
    const bool sameSource = records.size() == 1
        && juce::File(records.front().filePath) == preview.loadedFilePath();
    const auto decision = resolveMatchedDrag(matchOn, (int) records.size(), sameSource,
                                              preview.matchedPreviewIsCurrent(),
                                              preview.matchedPreviewFile(),
                                              preview.matchDragBlockMessage());
    if (decision.startsDrag())
    {
        juce::StringArray files;
        files.add(decision.file.getFullPathName());
        ExternalFileDrag::start(files, source);
        return true;
    }
    status.setText(decision.message, juce::dontSendNotification);
    return true;
}

std::optional<MatchSource> MainComponent::buildMatchSource(const juce::File& file,
                                                           int64_t& sourceSize,
                                                           int64_t& sourceModified)
{
    if (readRepo == nullptr || ! file.existsAsFile()) return std::nullopt;
    const auto record = readRepo->findByPath (file.getFullPathName());
    if (! record.has_value()) return std::nullopt;

    juce::AudioFormatManager formats;
    registerSonicatorFormats (formats);
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    // Sampler banks, tracker modules, disc entries and REX containers have no single ordinary
    // reader here; they are refused explicitly instead of matching only their first part.
    if (reader == nullptr || reader->numChannels < 1 || reader->numChannels > 2
        || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
        return std::nullopt;

    SmartResult result;
    if (const auto stored = readRepo->smartResult (record->id)) result = *stored;
    const auto identity = "catalog:" + juce::String (record->id) + ":" + file.getFullPathName();
    auto source = sourceFromSmartResult (result, identity, 0, reader->lengthInSamples,
                                         reader->sampleRate, (int) reader->numChannels);
    sourceSize = file.getSize();
    sourceModified = file.getLastModificationTime().toMilliseconds();
    return source;
}

void MainComponent::startSpectralAnalysis(std::vector<juce::File> files)
{
    if (embeddedUi || files.empty()) return;
    if (spectralAnalysis == nullptr)
        spectralAnalysis = std::make_unique<SpectralAnalysisService>();
    if (spectralAnalysis->isRunning())
    {
        status.setText("Spectral analysis is already running.", juce::dontSendNotification);
        return;
    }
    // Keep the folder bounded before a large run adds thousands of entries.
    pruneSpectrumCache();
    spectralAnalysis->start(std::move(files));
    spectralStatusMs = 0;
    status.setText("Spectral analysis: starting...", juce::dontSendNotification);
}

void MainComponent::pollSpectralAnalysis()
{
    if (spectralAnalysis == nullptr) return;
    const auto now = juce::Time::getMillisecondCounter();
    SpectralAnalysisService::Progress summary;
    if (spectralAnalysis->takeFinished(summary))
    {
        juce::String text = "Spectral analysis: " + juce::String(summary.done) + " of "
            + juce::String(summary.total) + " file(s)";
        if (summary.skipped > 0) text << ", " << summary.skipped << " already cached";
        if (summary.failed > 0) text << ", " << summary.failed << " failed";
        status.setText(text, juce::dontSendNotification);
        spectralStatusMs = now;
        return;
    }
    if (! spectralAnalysis->isRunning()) return;
    if (spectralStatusMs != 0 && now - spectralStatusMs < 300) return;
    const auto progress = spectralAnalysis->progress();
    juce::String text = "Spectral analysis: " + juce::String(progress.done) + "/"
        + juce::String(progress.total);
    if (progress.current.isNotEmpty()) text << " - " << progress.current;
    status.setText(text, juce::dontSendNotification);
    spectralStatusMs = now;
}
} // namespace sonicator
