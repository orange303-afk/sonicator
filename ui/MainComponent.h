#pragma once
#include "ui/shell/Theme.h"
#include "ui/library/FileListPanel.h"
#include "ui/preview/PreviewPanel.h"
#include "core/indexing/IndexingService.h"
namespace sonicator {
class MainComponent final : public juce::Component, private juce::Timer
{
public:
    static juce::File defaultDatabaseFile();
    explicit MainComponent(const juce::File& databaseFile = defaultDatabaseFile(), bool openDevice = true);
    ~MainComponent() override;
    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& k) override { return preview.keyPressed(k); }
    void beginImport(const juce::File&);
private:
    SonicatorLookAndFeel look;
    juce::File dbPath;
    AudioEngine engine;
    PreviewPanel preview;
    FileListPanel library;
    juce::TextEditor search;
    juce::TextButton addFolder { "+ Add folder" }, rescan { "Rescan library" }, reanalyze { "Reanalyze all" };
    juce::TextButton pause { "Pause" }, cancel { "Cancel" }, settings { "Audio settings" };
    juce::TextButton previousPage { "<" }, nextPage { ">" };
    juce::Label heading, subtitle, libraryHeading, rootsLabel, status, countLabel, analysisHeading, analysisDetails;
    juce::Viewport analysisViewport;
    juce::Label stageLabel;
    std::unique_ptr<Database> readDb, writeDb;
    std::unique_ptr<SampleRepository> readRepo, writeRepo;
    std::unique_ptr<IndexingService> indexer;
    std::unique_ptr<juce::FileChooser> chooser;
    int pageOffset = 0, catalogCount = 0;
    bool wasRunning = false;
    juce::Rectangle<int> sidebarBounds, analysisBounds, headerBounds;
    void timerCallback() override;
    void chooseFolder();
    void scanRoots(bool force);
    void reloadLibrary();
    void selectSample(const SampleRecord&);
    void showAudioSettings();
};
}
