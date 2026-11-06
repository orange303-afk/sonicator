#include "PreviewPanel.h"
#include "core/audio/SelectionPlayback.h"
#include "ui/shell/Theme.h"
namespace sonicator {
PreviewPanel::PreviewPanel(AudioEngine& audio,const juce::File& cacheFolder,const juce::File& exportFolder)
    : engine(audio),waveform(cacheFolder),exporter(exportFolder)
{
    for (auto* c : std::array<juce::Component*,12>{&waveform, &filename, &time, &play, &stop, &loop, &volume,
                                                   &midi, &skipSilence, &autoPreview, &clearSelectionButton, &selectionStatus})
        addAndMakeVisible(c);
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
    loop.onClick = [this] { applyTransportLooping(); };
    skipSilence.onClick = [this] { engine.setSkipSilence(skipSilence.getToggleState()); };
    skipSilence.setTooltip("Next trigger starts one frame before the first nonzero frame; original audio is unchanged");
    midi.setTooltip("16 voices; MIDI note 60 = original pitch; 2 ms attack / 20 ms release. Inputs: Audio settings");
    midi.onClick = [this]
    {
        if (! engine.setMidiPreview(midi.getToggleState()))
        { midi.setToggleState(false,juce::dontSendNotification); filename.setText(engine.error(),juce::dontSendNotification); }
        else if (loadedFile != juce::File()) filename.setText(loadedFile.getFileName(),juce::dontSendNotification);
    };
    autoPreview.setComponentID("autoPreview");
    loop.setComponentID("loop");
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
    for (auto* button : std::array<juce::Button*,7>{&play,&stop,&loop,&midi,&skipSilence,&autoPreview,&clearSelectionButton})
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
    loadedFile = success ? file : juce::File();
    loadedSize = success ? file.getSize() : 0;
    loadedModified = success ? file.getLastModificationTime().toMilliseconds() : 0;
    filename.setText(success ? file.getFileName() : engine.error(),juce::dontSendNotification);
    waveform.setFile(success ? file : juce::File());
    if (success && autoPreview.getToggleState()) engine.play();
    return success;
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
    exporter.prepare(loadedFile,range.start,range.end);
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
    time.setText(juce::String(engine.position(),2) + " / " + juce::String(engine.length(),2) + " s",juce::dontSendNotification);
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
    g.fillAll(theme::panel); g.setColour(theme::line); g.drawHorizontalLine(0,0,(float) getWidth());
}
void PreviewPanel::resized()
{
    auto r = getLocalBounds().reduced(16,12);
    auto top = r.removeFromTop(24); filename.setBounds(top.removeFromLeft(juce::jmax(200,top.getWidth()-160)));
    time.setBounds(top);
    r.removeFromTop(8);
    auto controls = r.removeFromRight(330); r.removeFromRight(16); waveform.setBounds(r);
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
