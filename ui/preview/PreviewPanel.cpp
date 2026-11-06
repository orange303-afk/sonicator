#include "PreviewPanel.h"
#include "ui/shell/Theme.h"
namespace sonicator {
PreviewPanel::PreviewPanel(AudioEngine& audio,const juce::File& cacheFolder) : engine(audio),waveform(cacheFolder)
{
    for (auto* c : std::array<juce::Component*,7>{&waveform, &filename, &time, &play, &stop, &loop, &volume})
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
    setWantsKeyboardFocus(true); startTimerHz(30);
}
bool PreviewPanel::loadFile(const juce::File& file)
{
    bool success = engine.loadFile(file);
    filename.setText(success ? file.getFileName() : engine.error(),juce::dontSendNotification);
    waveform.setFile(success ? file : juce::File());
    if (success) engine.play();
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
void PreviewPanel::resized()
{
    auto r = getLocalBounds().reduced(16,12);
    auto top = r.removeFromTop(24); filename.setBounds(top.removeFromLeft(juce::jmax(200,top.getWidth()-160)));
    time.setBounds(top);
    r.removeFromTop(8);
    auto controls = r.removeFromRight(220); r.removeFromRight(16); waveform.setBounds(r);
    auto buttons = controls.removeFromTop(36);
    play.setBounds(buttons.removeFromLeft(78)); buttons.removeFromLeft(6);
    stop.setBounds(buttons.removeFromLeft(60)); loop.setBounds(buttons);
    controls.removeFromTop(12); volume.setBounds(controls.removeFromTop(26));
}
}
