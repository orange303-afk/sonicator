#include "PreviewPanel.h"
#include "core/audio/SelectionPlayback.h"
#include "ui/shell/Theme.h"
namespace sonicator {
PreviewPanel::PreviewPanel(AudioEngine& audio,const juce::File& cacheFolder,const juce::File& exportFolder)
    : engine(audio),waveform(cacheFolder),exporter(exportFolder)
{
    for (auto* c : std::array<juce::Component*,15>{&waveform, &filename, &time, &play, &stop, &loop, &volume,
                                                   &midi, &skipSilence, &autoPreview, &clearSelectionButton, &selectionStatus,
                                                   &presetBox, &rexGate, &rexInfo})
        addAndMakeVisible(c);
    presetBox.setVisible(false); rexGate.setVisible(false); rexInfo.setVisible(false);
    rexInfo.setFont(juce::FontOptions(12));
    rexInfo.setColour(juce::Label::textColourId,theme::muted);
    rexInfo.setJustificationType(juce::Justification::centredLeft);
    presetBox.setTooltip("SoundFont preset (bank:program name); selecting one keeps MIDI Preview active");
    presetBox.onChange = [this]
    {
        const int index = presetBox.getSelectedId() - 1;
        if (index < 0) return;
        if (engine.selectSoundFontPreset(index))
            filename.setText(loadedFile.getFileName() + "  —  " + engine.soundFontPresetName(),juce::dontSendNotification);
        else
            filename.setText(engine.error(),juce::dontSendNotification);
    };
    rexGate.setTooltip("Gate stops a REX2 slice on NoteOff; off lets the slice ring out (one-shot)");
    rexGate.onClick = [this]
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
    midi.setTooltip("16 voices; MIDI note 60 = original pitch; 2 ms attack / 20 ms release. Inputs: Audio settings");
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
    setWantsKeyboardFocus(true); startTimerHz(30);
}
bool PreviewPanel::loadFile(const juce::File& file)
{
    if (file == loadedFile && file != juce::File() && file.getSize() == loadedSize
        && file.getLastModificationTime().toMilliseconds() == loadedModified) return true;
    bool success = engine.loadFile(file);
    const bool soundFont = success && engine.isSoundFontMode();
    loadedFile = success ? file : juce::File();
    loadedSize = success ? file.getSize() : 0;
    loadedModified = success ? file.getLastModificationTime().toMilliseconds() : 0;
    const bool rex = success && engine.isRex2Mode();
    if (soundFont)
    {
        presetBox.clear(juce::dontSendNotification);
        for (int i = 0; i < engine.soundFontPresetCount(); ++i)
            presetBox.addItem(engine.soundFontPresetLabel(i), i + 1);
        presetBox.setSelectedId(engine.soundFontPresetIndex() + 1,juce::dontSendNotification);
        presetBox.setVisible(true);
        rexGate.setVisible(false);
        rexInfo.setVisible(false);
        filename.setText(file.getFileName() + "  —  " + engine.soundFontPresetName(),juce::dontSendNotification);
    }
    else if (rex)
    {
        presetBox.setVisible(false);
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
        presetBox.setVisible(false);
        rexGate.setVisible(false);
        rexInfo.setVisible(false);
        filename.setText(success ? file.getFileName() : engine.error(),juce::dontSendNotification);
    }
    if (rex)
    {
        if (auto audio = engine.rexAudio())
            waveform.setPeaks(audio->left, audio->sampleRate, engine.rexSliceStarts());
        else
            waveform.setFile(juce::File());
    }
    else
        waveform.setFile(success && ! soundFont ? file : juce::File());
    midi.setToggleState(engine.isMidiPreview(),juce::dontSendNotification);
    if (success && ! soundFont && autoPreview.getToggleState()) engine.play();
    return success;
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
    if (engine.isRex2Mode())
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
        if (waveform.selection().valid()) clearSelection();
        engine.stop();
        return true;
    }
    return false;
}
void PreviewPanel::timerCallback()
{
    updateSelectionPlayback();
    refreshSelectionStatus();
    waveform.setPosition(engine.position());
    if (engine.isRex2Mode()) waveform.setActiveSlices(engine.rexActiveSlices());
    time.setText(juce::String(engine.position(),2) + " / " + juce::String(engine.length(),2) + " s",juce::dontSendNotification);
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
}
void PreviewPanel::lookAndFeelChanged()
{
    time.setColour(juce::Label::textColourId,theme::muted);
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
    auto controls = r.removeFromRight(330); r.removeFromRight(16); waveform.setBounds(r);
    auto extra = controls.removeFromTop(26);
    presetBox.setBounds(extra);
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
    clearSelectionButton.setBounds(selection.removeFromRight(58)); selection.removeFromRight(8);
    selectionStatus.setBounds(selection);
}
} // namespace sonicator
