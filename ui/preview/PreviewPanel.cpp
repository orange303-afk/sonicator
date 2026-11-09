#include "PreviewPanel.h"
#include "core/ableton/AbletonPacks.h"
#include "core/audio/StereoPair.h"
#include "core/audio/SelectionPlayback.h"
#include "ui/shell/Theme.h"
#include <cmath>
namespace sonicator {
int PreviewPanel::getNumRows()
{
    if (engine.isReasonMode()) return (int)engine.reasonPatch().references.size();
    if (engine.isTrackerMode()) return trackerBox.getNumItems();
    return ! engine.isSamplerInstrumentMode() || instrumentSamples.empty() ? 0 : sampleBox.getNumItems() + 1;
}
void PreviewPanel::paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (row < 0 || row >= getNumRows()) return;
    if (selected) g.fillAll(theme::selection);
    if (engine.isReasonMode())
    {
        const auto& ref=engine.reasonPatch().references[(size_t)row];
        g.setColour(ref.isLocal() ? theme::text : theme::muted);
        g.setFont(juce::FontOptions(12));
        g.drawText(ref.name + (ref.isLocal() ? "" : "  ·  "+ref.message),8,0,width-16,height,juce::Justification::centredLeft);
        return;
    }
    const bool tracker = engine.isTrackerMode();
    const auto text = tracker ? trackerBox.getItemText(row)
        : (row == 0 ? juce::String("Head") : sampleBox.getItemText(row - 1));
    bool unavailable = ! tracker && row > 0 && row <= (int) instrumentSamples.size()
        && ! instrumentSamples[(size_t) row - 1].isPlayable();
    if (tracker && row > 0 && row <= (int) engine.trackerSlots().size())
    {
        const auto& slot = engine.trackerSlots()[(size_t) row - 1];
        unavailable = slot.fmInstrument || slot.error.isNotEmpty();
    }
    g.setColour(unavailable ? juce::Colour(0xffe05a5a) : theme::text);
    g.setFont(juce::FontOptions(tracker ? juce::Font::getDefaultMonospacedFontName() : juce::Font::getDefaultSansSerifFontName(),
                                12.0f, row == 0 ? juce::Font::bold : juce::Font::plain));
    g.drawText(text, 8, 0, width - 16, height, juce::Justification::centredLeft, ! tracker);
}
void PreviewPanel::selectedRowsChanged(int row)
{
    if (syncingSources || row < 0 || row >= getNumRows()) return;
    if (engine.isReasonMode()) { applyReasonReference(row,true); return; }
    if (engine.isTrackerMode())
    {
        if (row == 0) applyTrackerWholeSong(true);
        else applyTrackerSample(trackerBox.getItemId(row) - 2, true);
    }
    else if (row == 0)
    {
        instrumentHead = true;
        engine.stop();
        midi.setToggleState(engine.setMidiPreview(true), juce::dontSendNotification);
    }
    else
    {
        instrumentHead = false;
        applyInstrumentSample(instrumentSamples[(size_t) row - 1].id, true);
    }
    sourceList.repaint();
}
void PreviewPanel::refreshSourceList()
{
    const juce::ScopedValueSetter<bool> guard(syncingSources, true);
    sourceList.updateContent();
    sourceList.setRowHeight(engine.isTrackerMode() ? 18 : 30);
    int contentWidth = 0;
    if (engine.isTrackerMode())
    {
        const juce::Font font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain));
        for (const auto& slot : engine.trackerSlots())
            contentWidth = juce::jmax(contentWidth, juce::GlyphArrangement::getStringWidthInt(font, slot.displayText) + 16);
    }
    sourceList.setMinimumContentWidth(contentWidth);
    const int row = engine.isReasonMode() ? engine.selectedReasonReference()
        : engine.isTrackerMode() ? trackerBox.getSelectedItemIndex()
        : (instrumentHead ? 0 : sampleBox.getSelectedItemIndex() + 1);
    if (getNumRows() > 0 && row>=0) sourceList.selectRow(row, true, true);
    else sourceList.deselectAllRows();
    sourceList.setVisible(getNumRows() > 0);
    sampleBox.setVisible(false);
    trackerBox.setVisible(false);
    if (onSourcesChanged) onSourcesChanged();
}
PreviewPanel::PreviewPanel(AudioEngine& audio,const juce::File& cacheFolder,const juce::File& exportFolder)
    : engine(audio),waveform(cacheFolder),exporter(exportFolder)
{
    for (auto* c : std::array<juce::Component*,19>{&waveform, &songProgress, &filename, &time, &play, &stop, &loop, &volume,
                                                   &midi, &skipSilence, &autoPreview, &clearSelectionButton, &selectionStatus,
                                                   &presetBox, &sampleBox, &rexGate, &rexInfo, &trackerBox, &sampleDragButton})
        addAndMakeVisible(c);
    presetBox.setVisible(false); sampleBox.setVisible(false); rexGate.setVisible(false); rexInfo.setVisible(false);
    trackerBox.setVisible(false); sampleDragButton.setVisible(false); songProgress.setVisible(false);
    addChildComponent(sourceList);
    addChildComponent(reasonInfo);
    reasonInfo.setComponentID("reasonInfo");
    reasonInfo.setFont(juce::FontOptions(12));
    reasonInfo.setJustificationType(juce::Justification::topLeft);
    reasonInfo.setColour(juce::Label::textColourId,theme::text);
    addChildComponent(abletonInfo);
    abletonInfo.setComponentID("abletonInfo");
    abletonInfo.setFont(juce::FontOptions(12));
    abletonInfo.setJustificationType(juce::Justification::centredLeft);
    abletonInfo.setColour(juce::Label::textColourId,theme::muted);
    sourceList.setComponentID("containerSources");
    sourceList.setRowHeight(30);
    songProgress.setComponentID("songProgress");
    songProgress.onSeek = [this](double seconds) { engine.seek(seconds); };
    rexInfo.setFont(juce::FontOptions(12));
    rexInfo.setColour(juce::Label::textColourId,theme::muted);
    rexInfo.setJustificationType(juce::Justification::centredLeft);
    presetBox.setTooltip("SoundFont preset (bank:program name); selecting one keeps MIDI Preview active");
    presetBox.onChange = [this]
    {
        const int index = presetBox.getSelectedId() - 1;
        if (index < 0) return;
        if (engine.selectSoundFontPreset(index))
        {
            filename.setText(loadedFile.getFileName() + "  —  " + engine.soundFontPresetName(),juce::dontSendNotification);
            refreshInstrumentSamples();
        }
        else
            filename.setText(engine.error(),juce::dontSendNotification);
    };
    sampleBox.setComponentID("instrumentSample");
    sampleBox.setTooltip("Audio source used by the selected preset; ordinary Play and waveform use it, MIDI keeps the preset map");
    sampleBox.onChange = [this]
    {
        const int row = sampleBox.getSelectedId() - 1;
        if (row < 0 || row >= (int) instrumentSamples.size()) return;
        applyInstrumentSample(instrumentSamples[(size_t) row].id, true);
    };
    for (auto* box : std::array<StepComboBox*,3>{&presetBox, &sampleBox, &trackerBox})
    {
        box->setWantsKeyboardFocus(true);
        box->setMouseClickGrabsKeyboardFocus(true);
    }
    trackerBox.setComponentID("trackerSample");
    trackerBox.setTooltip("Whole tracker song or one of its source samples; selecting a sample stops the song");
    trackerBox.onChange = [this]
    {
        const int id = trackerBox.getSelectedId();
        if (id <= 0) return;
        if (id == 1) applyTrackerWholeSong(true);
        else applyTrackerSample(id - 2, true);
    };
    sampleDragButton.setComponentID("dragTrackerSample");
    sampleDragButton.setTooltip("Drags the whole selected sample as WAV; preparation runs first if needed");
    sampleDragButton.onDrag = [this]
    {
        if (engine.isDiscMode())
        {
            const auto state = exporter.state();
            const double whole = engine.length();
            if (state.isReady() && std::abs(state.start) < 1.0e-9 && std::abs(state.end - whole) < 1.0e-6)
            {
                juce::StringArray files;
                files.add(state.file.getFullPathName());
                juce::DragAndDropContainer::performExternalDragDropOfFiles(files, false, &sampleDragButton);
                return;
            }
            prepareWholeDiscSample();
            return;
        }
        if (! engine.isTrackerMode() || ! engine.isTrackerSampleSelected()) return;
        const auto state = exporter.state();
        const double whole = engine.length();
        if (state.isReady() && std::abs(state.start) < 1.0e-9 && std::abs(state.end - whole) < 1.0e-6)
        {
            juce::StringArray files;
            files.add(state.file.getFullPathName());
            juce::DragAndDropContainer::performExternalDragDropOfFiles(files, false, &sampleDragButton);
            return;
        }
        prepareWholeTrackerSample();
    };
    sampleDragButton.setWantsKeyboardFocus(false);
    sampleDragButton.setMouseClickGrabsKeyboardFocus(false);
    rexGate.setTooltip("Gate stops a REX2 slice on NoteOff; off lets the slice ring out (one-shot)");    rexGate.onClick = [this]
    {
        engine.setRexGateMode(rexGate.getToggleState());
        if (onSettingsChanged) onSettingsChanged();
    };
    filename.setText("No sample selected",juce::dontSendNotification);
    filename.setFont(juce::FontOptions(14,juce::Font::bold));
    time.setColour(juce::Label::textColourId,theme::muted);
    volume.setRange(0,1,0.01); volume.setValue(engine.getVolume(),juce::dontSendNotification);
    volume.setSliderStyle(juce::Slider::LinearHorizontal); volume.setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);
    volume.setTooltip("Preview volume");
    volume.onValueChange = [this] { engine.setVolume((float) volume.getValue()); };
    waveform.onSeek = [this](double p) { engine.seek(p); };
    waveform.onSelectionChanged = [this](selection::Range range) { selectionChanged(range); };
    waveform.onExternalDragRequested = [this] { beginExternalDrag(); };
    play.onClick = [this] { togglePlayback(); };
    stop.onClick = [this] { engine.stop(); };
    loop.onClick = [this]
    {
        applyTransportLooping();
        if (onSettingsChanged) onSettingsChanged();
    };
    skipSilence.onClick = [this]
    {
        engine.setSkipSilence(skipSilence.getToggleState());
        if (onSettingsChanged) onSettingsChanged();
    };
    skipSilence.setTooltip("Next trigger starts one frame before the first nonzero frame; original audio is unchanged");
    midi.setTooltip("16 voices for ordinary files; 128 voices and a 2 s release for instruments and a selected tracker sample. Note 60 = original pitch. Inputs: Audio settings");
    midi.onClick = [this]
    {
        if (! engine.setMidiPreview(midi.getToggleState()))
        { midi.setToggleState(false,juce::dontSendNotification); filename.setText(engine.error(),juce::dontSendNotification); }
        else if (loadedFile != juce::File()) filename.setText(loadedFile.getFileName(),juce::dontSendNotification);
        if (onSettingsChanged) onSettingsChanged();
    };
    autoPreview.onClick = [this] { if (onSettingsChanged) onSettingsChanged(); };
    autoPreview.setComponentID("autoPreview");
    loop.setComponentID("loop");
    midi.setComponentID("midiPreview");
    skipSilence.setComponentID("skipSilence");
    rexGate.setComponentID("rexGate");
    autoPreview.setToggleState(true,juce::dontSendNotification);
    autoPreview.setTooltip("Automatically audition newly selected files, independently of MIDI Preview");
    clearSelectionButton.setComponentID("clearSelection");
    clearSelectionButton.setTooltip("Clear the waveform selection");
    clearSelectionButton.setVisible(false);
    clearSelectionButton.onClick = [this] { clearSelection(); };
    selectionStatus.setComponentID("selectionStatus");
    selectionStatus.setColour(juce::Label::textColourId,theme::muted);
    selectionStatus.setFont(juce::FontOptions(12));
    selectionStatus.setJustificationType(juce::Justification::centredLeft);
    for (auto* button : std::array<juce::Button*,8>{&play,&stop,&loop,&midi,&skipSilence,&autoPreview,&clearSelectionButton,&rexGate})
    {
        button->setWantsKeyboardFocus(false); button->setMouseClickGrabsKeyboardFocus(false);
        auto action = button->onClick;
        button->onClick = [this,action] { if (action) action(); if (onAction) onAction(); };
    }
    volume.setWantsKeyboardFocus(false); volume.setMouseClickGrabsKeyboardFocus(false);
    setComponentID("preview");
    setWantsKeyboardFocus(true); startTimerHz(30);
}
bool PreviewPanel::loadFile(const juce::File& file, bool allowAutoPlay)
{
    if (file == loadedFile && file != juce::File() && file.getSize() == loadedSize
        && file.getLastModificationTime().toMilliseconds() == loadedModified) return true;
    // Preview provenance belongs to one loaded file only; never leak it to the next selection.
    abletonInfo.setVisible(false);
    abletonInfo.setText({}, juce::dontSendNotification);
    // A physical L/R pair becomes one stereo source for playback, MIDI and WAV export.
    auto pair = file.hasFileExtension("cmb;repatch;rfl") ? StereoPairResult() : loadStereoPair(file);
    if (pair.paired && pair.ok)
    {
        const auto name = parseStereoName(file.getFileName());
        if (loadDiscSample(file, name.base, pair.audio, false, 0, 0, false, allowAutoPlay))
        {
            loadedSize = file.getSize();
            loadedModified = file.getLastModificationTime().toMilliseconds();
            return true;
        }
    }
    if (pair.paired && pair.error.isNotEmpty())
        selectionStatus.setText(pair.error, juce::dontSendNotification);
    discAudioRef.reset();
    discStem.clear();
    bool success = engine.loadFile(file);
    const bool instrument = success && engine.isSamplerInstrumentMode();
    const bool soundFont = success && engine.isSoundFontMode();
    loadedFile = success ? file : juce::File();
    loadedSize = success ? file.getSize() : 0;
    loadedModified = success ? file.getLastModificationTime().toMilliseconds() : 0;
    const bool rex = success && engine.isRex2Mode();
    const bool tracker = success && engine.isTrackerMode();
    const bool reason = success && engine.isReasonMode();
    reasonInfo.setVisible(reason);
    midi.setEnabled(!reason);
    play.setEnabled(success && !reason);
    if (tracker)
    {
        presetBox.setVisible(false);
        sampleBox.setVisible(false);
        instrumentSamples.clear();
        currentSampleId = invalidSampleId;
        rexGate.setVisible(false);
        rexInfo.setVisible(false);
        trackerBox.setVisible(true);
        fillTrackerChoices();
        sampleDragButton.setVisible(false);
        waveform.setFile(juce::File()); // no song overview yet; never show the first sample instead
        waveform.setVisible(false);
        songProgress.setLength(engine.length());
        songProgress.setPosition(engine.position());
        songProgress.setVisible(true);
        filename.setText(file.getFileName() + "  —  " + engine.trackerSongTitle(),
                         juce::dontSendNotification);
        selectionStatus.setText(trackerSummary(), juce::dontSendNotification);
    }
    else if (instrument)
    {
        trackerBox.setVisible(false);
        sampleDragButton.setVisible(false);
        songProgress.setVisible(false);
        waveform.setVisible(true);
        presetBox.clear(juce::dontSendNotification);
        presetBox.setVisible(soundFont);
        if (soundFont)
        {
            for (int i = 0; i < engine.soundFontPresetCount(); ++i)
                presetBox.addItem(engine.soundFontPresetLabel(i), i + 1);
            presetBox.setSelectedId(engine.soundFontPresetIndex() + 1,juce::dontSendNotification);
        }
        rexGate.setVisible(false);
        rexInfo.setVisible(false);
        filename.setText(file.getFileName() + "  —  " + engine.instrumentName(),juce::dontSendNotification);
        refreshInstrumentSamples();
    }
    else if (rex)
    {
        trackerBox.setVisible(false);
        sampleDragButton.setVisible(false);
        songProgress.setVisible(false);
        waveform.setVisible(true);
        presetBox.setVisible(false);
        sampleBox.setVisible(false);
        instrumentSamples.clear();
        currentSampleId = invalidSampleId;
        rexGate.setVisible(true);
        rexInfo.setVisible(true);
        rexGate.setToggleState(engine.rexGateMode(),juce::dontSendNotification);
        const int last = engine.rexStartNote() + engine.rexSliceCount() - 1;
        filename.setText(file.getFileName(),juce::dontSendNotification);
        rexInfo.setText(juce::String(engine.rexSliceCount()) + " slices  ·  MIDI "
            + juce::String(engine.rexStartNote()) + "–" + juce::String(last),
            juce::dontSendNotification);
    }
    else
    {
        trackerBox.setVisible(false);
        sampleDragButton.setVisible(false);
        songProgress.setVisible(false);
        waveform.setVisible(true);
        presetBox.setVisible(false);
        sampleBox.setVisible(false);
        instrumentSamples.clear();
        currentSampleId = invalidSampleId;
        rexGate.setVisible(false);
        rexInfo.setVisible(false);
        filename.setText(success ? file.getFileName()
                                 : file.getFileName() + "  —  " + engine.error(),juce::dontSendNotification);
        if (success)
        {
            const auto preview = abletonPreviewInfo(file);
            if (preview.isPreview)
            {
                filename.setText(file.getFileName() + "  —  Ableton Preview", juce::dontSendNotification);
                abletonInfo.setText("Ableton Preview of " + preview.source + " (" + preview.kind
                    + ")  ·  rendered demo audio, not the pack's original sample",
                    juce::dontSendNotification);
                abletonInfo.setVisible(true);
            }
        }
    }
    if (reason)
    {
        const auto& patch=engine.reasonPatch();
        filename.setText(file.getFileName()+"  —  "+patch.name,juce::dontSendNotification);
        const auto text=patch.message + (patch.devices.isEmpty() ? "" : "\n"+patch.devices.joinIntoString(" · "));
        reasonInfo.setText(text,juce::dontSendNotification);
        reasonInfo.setTooltip(text);
        waveform.setFile(juce::File());
        waveform.setVisible(false);
        selectionStatus.setText({},juce::dontSendNotification);
    }
    else if (rex)
    {
        if (auto audio = engine.rexAudio())
            waveform.setPeaks(audio->left, audio->sampleRate, engine.rexSliceStarts());
        else
            waveform.setFile(juce::File());
    }
    else if (! instrument && ! tracker)
        waveform.setFile(success ? file : juce::File());
    midi.setToggleState(engine.isMidiPreview(),juce::dontSendNotification);
    resized(); // the tracker/sample selectors appear or disappear with the loaded asset
    instrumentHead = true;
    refreshSourceList();
    if (allowAutoPlay && success && ! soundFont && !reason && autoPreview.getToggleState()) engine.play();
    return success;
}
bool PreviewPanel::applyReasonReference(int index, bool autoPlay)
{
    const bool ok=engine.selectReasonReference(index);
    waveform.setFile(juce::File());
    if (ok)
    {
        if (engine.isRex2Mode())
        {
            if (auto audio=engine.rexAudio()) waveform.setPeaks(audio->left,audio->sampleRate,engine.rexSliceStarts());
        }
        else waveform.setFile(engine.reasonPatch().references[(size_t)index].file);
    }
    waveform.setVisible(ok);
    selectionStatus.setText(ok ? juce::String() : engine.error(),juce::dontSendNotification);
    midi.setEnabled(ok);
    midi.setToggleState(engine.isMidiPreview(),juce::dontSendNotification);
    play.setEnabled(ok);
    if (ok && autoPlay && autoPreview.getToggleState()) engine.play();
    refreshSourceList(); resized();
    return ok;
}
void PreviewPanel::refreshInstrumentSamples()
{
    instrumentSamples = engine.instrumentSamples();
    sampleBox.clear(juce::dontSendNotification);
    int selectedRow = 0;
    for (int i = 0; i < (int) instrumentSamples.size(); ++i)
    {
        const auto& descriptor = instrumentSamples[(size_t) i];
        juce::String label = juce::String(i + 1) + ". "
            + (descriptor.name.isNotEmpty() ? descriptor.name : juce::String("Sample"));
        if (! descriptor.zones.empty())
        {
            const auto& zone = descriptor.zones.front();
            label << "  ·  keys " << zone.keyLow << "–" << zone.keyHigh
                  << "  ·  vel " << zone.velocityLow << "–" << zone.velocityHigh;
        }
        if (! descriptor.isPlayable())
            label << "  ·  " << (descriptor.error.isNotEmpty() ? descriptor.error : juce::String("unavailable"));
        sampleBox.addItem(label, i + 1);
        if (descriptor.id == engine.instrumentSampleId()) selectedRow = i;
    }
    sampleBox.setVisible(! instrumentSamples.empty());
    sampleBox.setSelectedId(selectedRow + 1, juce::dontSendNotification);
    currentSampleId = engine.instrumentSampleId();
    if (auto audio = engine.instrumentSampleAudio())
        waveform.setPeaks(audio->left, audio->sampleRate, {});
    else
        waveform.setFile(juce::File());
    juce::String midiHint, tips;
    for (const auto& descriptor : instrumentSamples)
    {
        if (descriptor.id == currentSampleId && ! descriptor.zones.empty())
        {
            int lo = 127, hi = 0;
            for (const auto& zone : descriptor.zones) { lo = juce::jmin(lo, zone.keyLow); hi = juce::jmax(hi, zone.keyHigh); }
            midiHint = "  ·  MIDI " + juce::String(lo) + "–" + juce::String(hi);
        }
        tips << descriptor.name << "  keys ";
        for (const auto& zone : descriptor.zones) tips << zone.keyLow << "–" << zone.keyHigh << " ";
        tips << "\n";
    }
    sampleBox.setTooltip(tips.trim());
    refreshSourceList();
    if (loadedFile != juce::File())
        filename.setText(loadedFile.getFileName() + "  —  " + engine.instrumentName() + midiHint,
                         juce::dontSendNotification);
    const auto warnings = engine.instrumentWarnings();
    if (! waveform.selection().valid())
    {
        if (! warnings.isEmpty())
            selectionStatus.setText(warnings.joinIntoString(" · ").substring(0, 180), juce::dontSendNotification);
        else if (engine.lastInstrumentMidiNote() < 0)
            selectionStatus.setText({}, juce::dontSendNotification);
    }
}
bool PreviewPanel::applyInstrumentSample(SampleId id, bool manual)
{
    if (! engine.selectInstrumentSample(id))
    {
        selectionStatus.setText(engine.error(), juce::dontSendNotification);
        return false;
    }
    currentSampleId = id;
    for (int i = 0; i < (int) instrumentSamples.size(); ++i)
        if (instrumentSamples[(size_t) i].id == id)
        {
            sampleBox.setSelectedId(i + 1, juce::dontSendNotification);
            break;
        }
    if (auto audio = engine.instrumentSampleAudio())
        waveform.setPeaks(audio->left, audio->sampleRate, {});
    else
        waveform.setFile(juce::File());
    selectionStatus.setText({}, juce::dontSendNotification);
    if (manual && autoPreview.getToggleState())
    {
        engine.seek(0);
        engine.play();
    }
    if (manual) instrumentHead = false;
    refreshSourceList();
    return true;
}
void PreviewPanel::refreshMidiFollow()
{
    if (! engine.isSamplerInstrumentMode() || waveform.selection().valid()) return;
    const auto trigger = engine.lastInstrumentTrigger();
    if (! trigger.valid || trigger.generation != engine.instrumentGeneration()
        || trigger.sourceId == currentSampleId) return;
    for (const auto& descriptor : instrumentSamples)
        if (descriptor.id == trigger.sourceId && descriptor.isPlayable())
        {
            applyInstrumentSample(descriptor.id, false);
            return;
        }
}
juce::String PreviewPanel::instrumentSampleStem() const
{
    auto stem = loadedFile.getFileNameWithoutExtension();
    for (const auto& descriptor : instrumentSamples)
        if (descriptor.id == currentSampleId && descriptor.name.isNotEmpty())
        {
            stem << "_" << descriptor.name;
            break;
        }
    return stem;
}

void PreviewPanel::fillTrackerChoices()
{
    trackerBox.clear(juce::dontSendNotification);
    trackerBox.addItem("Whole song", 1);
    for (const auto& slot : engine.trackerSlots())
    {
        const auto label = slot.displayText.isNotEmpty() ? slot.displayText : juce::String(" ");
        trackerBox.addItem(label, slot.slot + 2);
    }
    trackerBox.setSelectedId(engine.isTrackerSampleSelected()
                                 ? engine.selectedTrackerSampleId() + 2 : 1,
                             juce::dontSendNotification);
}

juce::String PreviewPanel::trackerSummary() const
{
    const auto& info = engine.trackerInfo();
    juce::String text = engine.trackerSongFormat().isNotEmpty() ? engine.trackerSongFormat()
                                                               : info.formatName;
    if (engine.trackerSongChannels() > 0) text << " · " << engine.trackerSongChannels() << " ch";
    text << " · " << (int) engine.trackerSlots().size() << " sample slots";
    if (engine.length() > 0) text << " · " << juce::String(engine.length(), 1) << " s";
    for (const auto& warning : info.warnings) text << " · " << warning;
    return text.substring(0, 220);
}

juce::String PreviewPanel::trackerSampleSummary() const
{
    const auto& slots = engine.trackerSlots();
    const auto id = engine.selectedTrackerSampleId();
    if (id < 0 || id >= (int) slots.size()) return {};
    const auto& slot = slots[(size_t) id];
    juce::String text = "Sample " + juce::String(id + 1) + ": "
        + (slot.name.isNotEmpty() ? slot.name : juce::String("unnamed"))
        + " · " + juce::String(slot.frames) + " frames"
        + " · " + juce::String(slot.sampleRate) + " Hz"
        + " · " + juce::String(slot.bitsPerSample) + "-bit";
    if (slot.looping)
        text << " · loop " << slot.loopStart << "–" << slot.loopEnd
             << (slot.pingPong ? " ping-pong" : "");
    return text.substring(0, 220);
}

juce::String PreviewPanel::trackerSampleStem() const
{
    const auto id = engine.selectedTrackerSampleId();
    juce::String stem = loadedFile.getFileNameWithoutExtension() + "_s" + juce::String(id + 1);
    const auto& slots = engine.trackerSlots();
    if (id >= 0 && id < (int) slots.size() && slots[(size_t) id].name.isNotEmpty())
        stem << "_" << slots[(size_t) id].name;
    return stem;
}

bool PreviewPanel::applyTrackerSample(SampleId id, bool manual)
{
    if (! engine.selectTrackerSample(id))
    {
        selectionStatus.setText(engine.error(), juce::dontSendNotification);
        return false;
    }
    trackerBox.setSelectedId((int) id + 2, juce::dontSendNotification);
    refreshSourceList();
    currentSampleId = id;
    if (auto audio = engine.trackerSampleAudio())
        waveform.setPeaks(audio->left, audio->sampleRate, {});
    else
        waveform.setFile(juce::File());
    sampleDragButton.setVisible(true);
    sampleDragButton.setButtonText("Drag sample");
    songProgress.setVisible(false);
    waveform.setVisible(true);
    resized();
    selectionStatus.setText(trackerSampleSummary(), juce::dontSendNotification);
    if (manual && autoPreview.getToggleState())
    {
        engine.seek(0);
        engine.play();
    }
    return true;
}

bool PreviewPanel::selectTrackerSlot(SampleId id) { return applyTrackerSample(id, true); }

void PreviewPanel::selectTrackerWholeSong() { applyTrackerWholeSong(true); }

void PreviewPanel::applyTrackerWholeSong(bool manual)
{
    if (! engine.selectTrackerWholeSong()) return;
    trackerBox.setSelectedId(1, juce::dontSendNotification);
    refreshSourceList();
    waveform.setFile(juce::File());
    waveform.setVisible(false);
    songProgress.setLength(engine.length());
    songProgress.setPosition(engine.position());
    songProgress.setVisible(true);
    sampleDragButton.setVisible(false);
    resized();
    currentSampleId = invalidSampleId;
    selectionStatus.setText(trackerSummary(), juce::dontSendNotification);
    if (manual && autoPreview.getToggleState()) engine.play();
}

void PreviewPanel::prepareWholeTrackerSample()
{    auto audio = engine.trackerSampleAudio();
    if (audio == nullptr) return;
    const auto stem = trackerSampleStem();
    const double end = audio->sampleRate > 0 ? (double) audio->frames / (double) audio->sampleRate : 0.0;
    exporter.prepare(loadedFile, 0.0, end,
        [audio, stem](const juce::File&, double start, double end, const juce::File& folder,
                      CancelCheck cancel)
        {
            return AudioExporter().exportSampleSelection(*audio, stem, start, end, folder, cancel);
        });
    sampleDragButton.setButtonText("Preparing...");
}

bool PreviewPanel::loadDiscSample(const juce::File& container, const juce::String& stem,
                                  SampleAudioPtr audio, bool looping, int64_t loopStart,
                                  int64_t loopEnd, bool pingPong, bool allowAutoPlay)
{
    if (! engine.setDiscSample(audio, looping, loopStart, loopEnd, pingPong))
    {
        selectionStatus.setText(engine.error(), juce::dontSendNotification);
        return false;
    }
    discAudioRef = std::move(audio);
    reasonInfo.setVisible(false);
    play.setEnabled(true);
    midi.setEnabled(true);
    discStem = stem;
    loadedFile = container;
    currentSampleId = invalidSampleId;
    presetBox.setSelectedId(0, juce::dontSendNotification);
    sampleBox.setSelectedId(0, juce::dontSendNotification);
    trackerBox.setSelectedId(0, juce::dontSendNotification);
    presetBox.setVisible(false);
    sampleBox.setVisible(false);
    trackerBox.setVisible(false);
    waveform.setPeaks(discAudioRef->left, discAudioRef->sampleRate, {});
    refreshSourceList();
    waveform.setVisible(true);
    songProgress.setVisible(false);
    sampleDragButton.setVisible(true);
    sampleDragButton.setButtonText("Drag sample");
    filename.setText(stem + "  —  " + container.getFileName(), juce::dontSendNotification);
    resized();
    selectionStatus.setText(discSampleSummary(), juce::dontSendNotification);
    if (allowAutoPlay && autoPreview.getToggleState())
    {
        engine.seek(0);
        engine.play();
    }
    return true;
}

void PreviewPanel::clearDiscSource()
{
    if (engine.isReasonMode())
    {
        engine.loadFile(juce::File()); // unload the locally auditioned Reason source too
        loadedFile=juce::File(); loadedSize=0; loadedModified=0;
        reasonInfo.setVisible(false);
        refreshSourceList();
        midi.setEnabled(true);
    }
    engine.clearDiscSample();
    discAudioRef.reset();
    discStem.clear();
    exporter.cancel();
    waveform.setFile(juce::File());
    sampleDragButton.setVisible(false);
    sampleDragButton.setButtonText("Drag sample");
    selectionStatus.setText({}, juce::dontSendNotification);
}

juce::String PreviewPanel::discSampleSummary() const
{
    if (discAudioRef == nullptr || discAudioRef->sampleRate <= 0) return {};
    const double seconds = (double) discAudioRef->frames / (double) discAudioRef->sampleRate;
    return juce::String(discAudioRef->sampleRate) + " Hz  ·  "
        + (discAudioRef->channels == 2 ? "stereo" : "mono") + "  ·  "
        + juce::String(seconds, 3) + " s  ·  source PCM, no transpose";
}

void PreviewPanel::prepareWholeDiscSample()
{
    auto audio = engine.discSampleAudio();
    if (audio == nullptr) return;
    const auto stem = discStem;
    const double end = audio->sampleRate > 0 ? (double) audio->frames / (double) audio->sampleRate : 0.0;
    exporter.prepare(loadedFile, 0.0, end,
        [audio, stem](const juce::File&, double start, double end, const juce::File& folder,
                      CancelCheck cancel)
        {
            return AudioExporter().exportSampleSelection(*audio, stem, start, end, folder, cancel);
        });
    sampleDragButton.setButtonText("Preparing...");
}
void PreviewPanel::applyPersistedSettings(const AppSettings& settings)
{
    autoPreview.setToggleState(settings.autoPreview, juce::dontSendNotification);
    loop.setToggleState(settings.loopPreview, juce::dontSendNotification);
    skipSilence.setToggleState(settings.skipSilence, juce::dontSendNotification);
    engine.setSkipSilence(settings.skipSilence);
    rexGate.setToggleState(settings.rexGate, juce::dontSendNotification);
    engine.setRexGateMode(settings.rexGate);
    midi.setToggleState(settings.midiPreview, juce::dontSendNotification);
    if (! engine.setMidiPreview(settings.midiPreview))
        midi.setToggleState(engine.isMidiPreview(), juce::dontSendNotification);
    applyTransportLooping();
}
void PreviewPanel::selectRange(double start, double end)
{
    waveform.setSelection(selection::orderedRange(start,end,engine.length()));
}
void PreviewPanel::selectionChanged(selection::Range range)
{
    if (! range.valid())
    {
        clearSelectionButton.setVisible(false);
        selectionStatus.setText({},juce::dontSendNotification);
        exporter.cancel();
        applyTransportLooping();
        return;
    }
    clearSelectionButton.setVisible(true);
    selectionStatus.setText(juce::String::fromUTF8("Preparing selection…"),juce::dontSendNotification);
    if (engine.isTrackerMode())
    {
        // Only a selected tracker sample owns a waveform selection. In whole-song mode the
        // selection controls are irrelevant because the song itself is not exported here.
        if (auto audio = engine.trackerSampleAudio())
        {
            const auto stem = trackerSampleStem();
            exporter.prepare(loadedFile, range.start, range.end,
                [audio, stem](const juce::File&, double start, double end,
                              const juce::File& folder, CancelCheck cancel)
                {
                    return AudioExporter().exportSampleSelection(*audio, stem, start, end, folder, cancel);
                });
        }
        else exporter.cancel();
    }
    else if (engine.isDiscMode())
    {
        // The physical image is the export identity; the virtual entry never becomes a path.
        if (auto audio = engine.discSampleAudio())
        {
            const auto stem = discStem;
            exporter.prepare(loadedFile, range.start, range.end,
                [audio, stem](const juce::File&, double start, double end,
                              const juce::File& folder, CancelCheck cancel)
                {
                    return AudioExporter().exportSampleSelection(*audio, stem, start, end, folder, cancel);
                });
        }
        else exporter.cancel();
    }
    else if (engine.isSamplerInstrumentMode())
    {
        // The selected sample is immutable PCM with no AudioFormatReader. Retaining the shared
        // snapshot keeps the worker safe across bank/preset changes and MIDI-follow updates.
        if (auto audio = engine.instrumentSampleAudio())
        {
            const auto stem = instrumentSampleStem();
            exporter.prepare(loadedFile, range.start, range.end,
                [audio, stem](const juce::File&, double start, double end,
                              const juce::File& folder, CancelCheck cancel)
                {
                    return AudioExporter().exportSampleSelection(*audio, stem, start, end, folder, cancel);
                });
        }
        else exporter.cancel();
    }
    else if (engine.isRex2Mode())
    {
        // REX2 has no AudioFormatReader: render the selected time range from the PCM that the
        // preview already decoded. The shared pointer keeps the samples alive on the worker.
        if (auto audio = engine.rexAudio())
        {
            exporter.prepare(loadedFile, range.start, range.end,
                [audio](const juce::File& source, double start, double end,
                        const juce::File& folder, CancelCheck cancel)
                {
                    return AudioExporter().exportRexSelection(*audio, source, start, end, folder, cancel);
                });
        }
        else exporter.prepare(loadedFile, range.start, range.end);
    }
    else if (engine.isReasonMode() && engine.selectedReasonReference()>=0)
        exporter.prepare(engine.reasonPatch().references[(size_t)engine.selectedReasonReference()].file,range.start,range.end);
    else exporter.prepare(loadedFile,range.start,range.end);
    applyTransportLooping();
    refreshSelectionStatus();
}
void PreviewPanel::refreshSelectionStatus()
{
    const auto range = waveform.selection();
    if (! range.valid()) return;
    const auto state = exporter.state();
    if (state.status == SelectionExporter::Status::preparing)
        selectionStatus.setText(juce::String::fromUTF8("Preparing selection…"),juce::dontSendNotification);
    else if (state.status == SelectionExporter::Status::ready)
        selectionStatus.setText("Ready - drag the selection into a DAW",juce::dontSendNotification);
    else if (state.status == SelectionExporter::Status::failed)
        selectionStatus.setText(state.error,juce::dontSendNotification);
}
bool PreviewPanel::isSelectionReady() const
{
    return waveform.selection().valid() && exporter.state().isReady();
}
bool PreviewPanel::waitForSelectionPreparation(int timeoutMs)
{
    const bool finished = exporter.waitUntilFinished(timeoutMs);
    refreshSelectionStatus();
    return finished && exporter.state().status != SelectionExporter::Status::preparing;
}
void PreviewPanel::beginExternalDrag()
{
    const auto range = waveform.selection();
    if (! range.valid()) return;
    const auto state = exporter.state();
    if (state.status != SelectionExporter::Status::ready || ! state.file.existsAsFile())
    {
        selectionStatus.setText(state.status == SelectionExporter::Status::failed
            ? state.error : juce::String::fromUTF8("Preparing selection…"),juce::dontSendNotification);
        return;
    }
    juce::StringArray files;
    files.add(state.file.getFullPathName());
    juce::DragAndDropContainer::performExternalDragDropOfFiles(files,false,&waveform);
}
void PreviewPanel::togglePlayback()
{
    if (engine.isPlaying()) { engine.pause(); return; }
    const auto range = waveform.selection();
    if (range.valid()) engine.seek(range.start);
    engine.play();
}
void PreviewPanel::applyTransportLooping()
{
    // Whole-file looping would wrap at EOF before the timer can see the selection boundary.
    // A selection owns its own loop; the transport loop is restored when it is cleared.
    const bool wanted = loop.getToggleState() && ! waveform.selection().valid();
    if (engine.isLooping() != wanted) engine.setLooping(wanted);
}
void PreviewPanel::updateSelectionPlayback()
{
    const auto range = waveform.selection();
    if (! range.valid()) return;
    const bool loopWanted = loop.getToggleState();
    if (! engine.isPlaying())
    {
        if (loopWanted && engine.transport().hasStreamFinished())
        {
            engine.seek(range.start);
            engine.play();
        }
        return;
    }
    const auto action = selectionBoundary(engine.position(),range.start,range.end,loopWanted);
    if (action == SelectionBoundary::restart) engine.seek(range.start);
    else if (action == SelectionBoundary::stop) engine.pause();
}
bool PreviewPanel::keyPressed(const juce::KeyPress& k)
{
    if (k.getKeyCode() == juce::KeyPress::spaceKey) { togglePlayback(); return true; }
    if (k.getKeyCode() == juce::KeyPress::leftKey) { engine.seek(engine.position()-1); return true; }
    if (k.getKeyCode() == juce::KeyPress::rightKey) { engine.seek(engine.position()+1); return true; }
    if (k.getKeyCode() == juce::KeyPress::escapeKey)
    {
        if (engine.isSamplerInstrumentMode())
        {
            // Escape lifts the pin; the MIDI instrument keeps sounding. Clear is the same action.
            if (waveform.selection().valid()) clearSelection();
            return true;
        }
        if (waveform.selection().valid()) clearSelection();
        engine.stop();
        return true;
    }
    return false;
}
void PreviewPanel::timerCallback()
{
    refreshMidiFollow();
    updateSelectionPlayback();
    refreshSelectionStatus();
    waveform.setPosition(engine.position());
    if (songProgress.isVisible())
    {
        songProgress.setLength(engine.length());
        songProgress.setPosition(engine.position());
    }
    if (engine.isRex2Mode()) waveform.setActiveSlices(engine.rexActiveSlices());
    time.setText(juce::String(engine.position(),2) + " / " + juce::String(engine.length(),2) + " s",juce::dontSendNotification);
    if (! waveform.selection().valid() && engine.isSamplerInstrumentMode())
    {
        const int note = engine.lastInstrumentMidiNote();
        if (note >= 0)
        {
            const int layers = engine.lastInstrumentMidiLayers();
            juce::String range;
            for (const auto& descriptor : instrumentSamples)
                if (descriptor.id == currentSampleId && ! descriptor.zones.empty())
                {
                    int lo = 127, hi = 0;
                    for (const auto& zone : descriptor.zones) { lo = juce::jmin(lo, zone.keyLow); hi = juce::jmax(hi, zone.keyHigh); }
                    range = " (keys " + juce::String(lo) + "–" + juce::String(hi) + ")";
                }
            selectionStatus.setText(layers > 0
                ? "MIDI " + juce::String(note) + "  ·  " + juce::String(layers) + " layer(s) started"
                : "MIDI " + juce::String(note) + "  ·  no zone" + range,
                juce::dontSendNotification);
        }
    }
    if (rexInfo.isVisible() && engine.isRex2Mode())
    {
        const int first = engine.rexStartNote();
        const int last = first + engine.rexSliceCount() - 1;
        const int received = engine.rexLastMidiNote();
        juce::String text = juce::String(engine.rexSliceCount()) + " slices  ·  MIDI "
            + juce::String(first) + "–" + juce::String(last);
        if (received >= 0)
            text += "  ·  last " + juce::String(received)
                  + (received < first || received > last ? " (outside)" : "")
                  + "  ·  voices " + juce::String(engine.rexActiveVoices())
                  + "  ·  vel " + juce::String(engine.rexLastVelocity(),2)
                  + "  ·  mix " + juce::String(engine.rexMixPeak(),3)
                  + "  ·  env " + juce::String(engine.rexFirstEnvelope(),2)
                  + "  ·  pos " + juce::String(engine.rexFirstPosition(),0)
                  + "  ·  out " + juce::String(engine.outputPeak(),3)
                  + "  ·  gain " + juce::String(engine.outputGain(),2)
                  + "  ·  peak " + juce::String(engine.rexDecodedPeak(),2)
                  + "  ·  vol " + juce::String(engine.getVolume(),2);
        if (rexInfo.getText() != text) rexInfo.setText(text,juce::dontSendNotification);
    }
    play.setButtonText(engine.isPlaying() ? "Pause" : "Play");
    if (sampleDragButton.isVisible())
    {
        const auto state = exporter.state();
        const juce::String text = state.status == SelectionExporter::Status::preparing ? "Preparing..."
            : state.status == SelectionExporter::Status::failed ? "Prepare failed" : "Drag sample";
        if (sampleDragButton.getButtonText() != text)
            sampleDragButton.setButtonText(text);
    }
}
void PreviewPanel::lookAndFeelChanged()
{
    time.setColour(juce::Label::textColourId,theme::muted);
    reasonInfo.setColour(juce::Label::textColourId,theme::text);
    abletonInfo.setColour(juce::Label::textColourId,theme::muted);
    selectionStatus.setColour(juce::Label::textColourId,theme::muted);
    repaint();
}
void PreviewPanel::paint(juce::Graphics& g)
{
    juce::Path background;
    auto bounds = getLocalBounds().toFloat();
    background.addRoundedRectangle(bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(),
                                   12.0f, 12.0f, true, true, false, false);
    g.setColour(theme::panel);
    g.fillPath(background);
}
void PreviewPanel::resized()
{
    auto r = getLocalBounds().reduced(16,12);
    auto top = r.removeFromTop(24); filename.setBounds(top.removeFromLeft(juce::jmax(200,top.getWidth()-160)));
    time.setBounds(top);
    r.removeFromTop(8);
    if (abletonInfo.isVisible()) abletonInfo.setBounds(r.removeFromTop(18));
    auto controls = r.removeFromRight(330); r.removeFromRight(16); waveform.setBounds(r); songProgress.setBounds(r);
    if (reasonInfo.isVisible())
    {
        reasonInfo.setBounds(r.removeFromTop(waveform.isVisible() ? 48 : r.getHeight()));
        waveform.setBounds(r);
    }
    auto extra = presetBox.isVisible() || rexGate.isVisible() ? controls.removeFromTop(26) : juce::Rectangle<int>();
    if (presetBox.isVisible())
    {
        presetBox.setBounds(extra);
    }
    else
    {
        trackerBox.setBounds(extra);
        sampleBox.setBounds(extra);
    }
    rexGate.setBounds(extra.removeFromLeft(90));
    rexInfo.setBounds(extra);
    auto modes = controls.removeFromTop(28);
    midi.setBounds(modes.removeFromLeft(145)); skipSilence.setBounds(modes);
    auto options = controls.removeFromTop(26);
    autoPreview.setBounds(options.removeFromLeft(145)); loop.setBounds(options);
    auto buttons = controls.removeFromTop(30);
    play.setBounds(buttons.removeFromLeft(78)); buttons.removeFromLeft(6);
    stop.setBounds(buttons.removeFromLeft(60)); buttons.removeFromLeft(12); volume.setBounds(buttons);
    auto selection = controls.removeFromTop(26);
    if (sampleDragButton.isVisible()) { sampleDragButton.setBounds(selection.removeFromRight(96)); selection.removeFromRight(6); }
    clearSelectionButton.setBounds(selection.removeFromRight(58)); selection.removeFromRight(8);
    selectionStatus.setBounds(selection);
}
} // namespace sonicator
