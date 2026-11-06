#include "PreviewPanel.h"
#include "ui/shell/Theme.h"
namespace sonicator {
PreviewPanel::PreviewPanel(AudioEngine& audio,const juce::File& cacheFolder) : engine(audio),waveform(cacheFolder)
{
    for (auto* c : std::array<juce::Component*,10>{&waveform, &filename, &time, &play, &stop, &loop, &volume, &midi, &skipSilence, &autoPreview})
        addAndMakeVisible(c);
    filename.setText("No sample selected",juce::dontSendNotification);
    filename.setFont(juce::FontOptions(14,juce::Font::bold));
    time.setColour(juce::Label::textColourId,theme::muted);
    volume.setRange(0,1,0.01); volume.setValue(engine.getVolume(),juce::dontSendNotification);
    volume.setSliderStyle(juce::Slider::LinearHorizontal); volume.setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);
    volume.setTooltip("Preview volume");
    volume.onValueChange = [this] { engine.setVolume((float) volume.getValue()); };
    waveform.onSeek = [this](double p) { engine.seek(p); };
    play.onClick = [this] { togglePlayback(); };
    stop.onClick = [this] { engine.stop(); };
    loop.onClick = [this] { engine.setLooping(loop.getToggleState()); };
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
    autoPreview.setToggleState(true,juce::dontSendNotification);
    autoPreview.setTooltip("Automatically audition newly selected files, independently of MIDI Preview");
    for (auto* button : std::array<juce::Button*,6>{&play,&stop,&loop,&midi,&skipSilence,&autoPreview})
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
void PreviewPanel::togglePlayback() { if (engine.isPlaying()) engine.pause(); else engine.play(); }
bool PreviewPanel::keyPressed(const juce::KeyPress& k)
{
    if (k.getKeyCode() == juce::KeyPress::spaceKey) { togglePlayback(); return true; }
    if (k.getKeyCode() == juce::KeyPress::leftKey) { engine.seek(engine.position()-1); return true; }
    if (k.getKeyCode() == juce::KeyPress::rightKey) { engine.seek(engine.position()+1); return true; }
    if (k.getKeyCode() == juce::KeyPress::escapeKey) { engine.stop(); return true; }
    return false;
}
void PreviewPanel::timerCallback()
{
    waveform.setPosition(engine.position());
    time.setText(juce::String(engine.position(),2) + " / " + juce::String(engine.length(),2) + " s",juce::dontSendNotification);
    play.setButtonText(engine.isPlaying() ? "Pause" : "Play");
}
void PreviewPanel::paint(juce::Graphics& g)
{
    g.fillAll(theme::panel); g.setColour(theme::line); g.drawHorizontalLine(0,0,(float) getWidth());
}
void PreviewPanel::lookAndFeelChanged() { time.setColour(juce::Label::textColourId,theme::muted); repaint(); }
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
}
}
