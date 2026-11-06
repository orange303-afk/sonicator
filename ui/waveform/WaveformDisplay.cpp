#include "WaveformDisplay.h"
#include "ui/shell/Theme.h"
namespace sonicator {
WaveformDisplay::WaveformDisplay(const juce::File& folder) : cacheFile(folder.getChildFile("waveforms.cache"))
{
    formats.registerBasicFormats(); thumbnail.addChangeListener(this);
    folder.createDirectory();
    if (auto stream = cacheFile.createInputStream()) cache.readFromStream(*stream);
}
WaveformDisplay::~WaveformDisplay()
{
    thumbnail.removeChangeListener(this);
    thumbnail.setSource(nullptr);
    juce::TemporaryFile temporary(cacheFile);
    if (auto stream = temporary.getFile().createOutputStream())
    {
        cache.writeToStream(*stream); stream->flush();
        if (stream->getStatus().wasOk()) { stream.reset(); temporary.overwriteTargetFileWithTemporary(); }
    }
}
void WaveformDisplay::setFile(const juce::File& file)
{
    position = 0;
    if (file.existsAsFile()) thumbnail.setSource(new juce::FileInputSource(file,true));
    else thumbnail.setSource(nullptr);
    repaint();
}
void WaveformDisplay::paint(juce::Graphics& g)
{
    g.setColour(theme::background); g.fillRoundedRectangle(getLocalBounds().toFloat(),7);
    if (thumbnail.getTotalLength() <= 0)
    {
        g.setColour(theme::muted); g.drawText("Select a sample to preview",getLocalBounds(),juce::Justification::centred); return;
    }
    g.setColour(theme::accent);
    thumbnail.drawChannels(g,getLocalBounds().reduced(6,10),0,thumbnail.getTotalLength(),0.85f);
    float x = 6 + (float) juce::jlimit(0.0,1.0,position/thumbnail.getTotalLength()) * (getWidth()-12);
    g.setColour(theme::text); g.drawVerticalLine((int) x,5,(float) getHeight()-5);
}
void WaveformDisplay::mouseDown(const juce::MouseEvent& e)
{
    if (thumbnail.getTotalLength() > 0 && onSeek)
        onSeek(juce::jlimit(0.0,1.0,(e.position.x-6)/juce::jmax(1.0,getWidth()-12.0)) * thumbnail.getTotalLength());
}
}
