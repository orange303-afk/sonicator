#include "MidiSampler.h"
#include "ContainerValidation.h"
#include <cmath>
namespace sonicator {
void MidiEventQueue::push(const juce::MidiMessage& message)
{
    const int size = message.getRawDataSize();
    if (message.isSysEx() || size <= 0 || size > 3) return;
    if (busy.test_and_set(std::memory_order_acquire)) { panic = true; return; }
    if (count == capacity) panic = true;
    else
    {
        auto& e = events[(size_t) count++]; e.size = size;
        std::copy_n(message.getRawData(),size,e.bytes.begin());
    }
    busy.clear(std::memory_order_release);
}
bool MidiEventQueue::drain(juce::MidiBuffer& destination)
{
    if (busy.test_and_set(std::memory_order_acquire)) return false;
    const bool overflow = panic.exchange(false);
    if (! overflow)
        for (int i = 0; i < count; ++i)
        {
            const auto& e = events[(size_t) i];
            destination.addEvent(juce::MidiMessage(e.bytes.data(),e.size,0.0),0);
        }
    count = 0;
    busy.clear(std::memory_order_release);
    return overflow;
}
int64_t firstAudibleFrame(juce::AudioFormatReader& reader)
{
    if (reader.numChannels == 0 || reader.numChannels > 64) return 0;
    juce::AudioBuffer<float> block((int) reader.numChannels,2048);
    for (int64_t start = 0; start < reader.lengthInSamples; start += 2048)
    {
        const int count = (int) juce::jmin<int64_t>(2048,reader.lengthInSamples-start);
        if (! reader.read(&block,0,count,start,true,true)) return 0;
        for (int i = 0; i < count; ++i)
            for (int ch = 0; ch < block.getNumChannels(); ++ch)
            {
                const float value = block.getSample(ch,i);
                if (std::isfinite(value) && value != 0) return juce::jmax<int64_t>(0,start+i-1);
            }
    }
    return 0; // Fully silent files remain playable, not an out-of-range offset.
}
juce::String MidiSampler::load(const juce::File& file)
{
    clear();
    auto error = validateContainer(file);
    if (error.isNotEmpty()) return error;
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (! reader || reader->lengthInSamples <= 0 || ! std::isfinite(reader->sampleRate) || reader->sampleRate <= 0) return "Cannot decode MIDI sample";
    if (reader->numChannels < 1 || reader->numChannels > 2) return "MIDI Preview supports mono/stereo samples";
    constexpr int64_t maxBytes = 128 * 1024 * 1024;
    if (reader->lengthInSamples > maxBytes / (reader->numChannels * sizeof(float)))
        return "Sample exceeds 128 MiB decoded MIDI limit; use ordinary preview";
    samples.setSize((int) reader->numChannels,(int) reader->lengthInSamples);
    if (! reader->read(&samples,0,samples.getNumSamples(),0,true,true)) { clear(); return "Cannot read MIDI sample"; }
    inputRate = reader->sampleRate;
    bool found = false;
    for (int i = 0; i < samples.getNumSamples(); ++i)
    {
        for (int ch = 0; ch < samples.getNumChannels(); ++ch)
        {
            float& value = samples.getWritePointer(ch)[i];
            if (! std::isfinite(value)) value = 0;
            if (! found && value != 0) { onset = juce::jmax(0,i-1); found = true; }
        }
    }
    return {};
}
void MidiSampler::clear() { reset(); samples.setSize(0,0); onset = 0; }
void MidiSampler::prepare(double rate) { outputRate = std::isfinite(rate) && rate > 0 ? rate : 44100; reset(); }
void MidiSampler::reset() { for (auto& v : voices) v = {}; sustain.fill(false); }
int MidiSampler::activeVoices() const { int n = 0; for (const auto& v : voices) if (v.active) ++n; return n; }
void MidiSampler::release(Voice& v)
{
    if (v.releasing) return;
    v.sustained = false; v.releasing = true; v.releaseStep = v.envelope / (float) v.releaseFrames;
}
void MidiSampler::event(const juce::MidiMessage& message)
{
    const int channel = message.getChannel();
    if (channel < 1 || channel > 16) return;
    if (message.isAllSoundOff()) { for (auto& v : voices) if (v.channel == channel) v.active = false; return; }
    if (message.isAllNotesOff()) { for (auto& v : voices) if (v.channel == channel) release(v); return; }
    if (message.isController() && message.getControllerNumber() == 64)
    {
        const bool down = message.getControllerValue() >= 64;
        sustain[(size_t) channel-1] = down;
        if (! down) for (auto& v : voices) if (v.channel == channel && v.sustained) release(v);
    }
    if (message.isNoteOff())
    {
        for (auto& v : voices) if (v.active && v.note == message.getNoteNumber() && v.channel == channel)
        { if (sustain[(size_t) channel-1]) v.sustained = true; else release(v); }
    }
    if (! message.isNoteOn() || samples.getNumSamples() == 0) return;
    Voice* chosen = nullptr;
    // Never hard-steal an audible voice: at the 16-voice limit ignore new notes, avoiding a click.
    for (auto& v : voices) if (! v.active) { chosen = &v; break; }
    if (! chosen) return;
    auto& v = *chosen; v = {}; v.active = true; v.note = message.getNoteNumber(); v.channel = channel;
    v.velocity = message.getFloatVelocity(); v.startFrame = skipSilence.load() ? (int) onset : 0;
    v.position = v.startFrame; v.looping = loop.load();
    v.increment = inputRate/outputRate * std::pow(2.0,(v.note-60)/12.0);
    v.attackFrames = juce::jmax(1,(int) std::lround(outputRate*0.002));
    v.releaseFrames = juce::jmax(1,(int) std::lround(outputRate*0.020));
}
void MidiSampler::renderRange(juce::AudioBuffer<float>& out,int start,int count)
{
    const int length = samples.getNumSamples();
    if (length == 0) return;
    for (auto& v : voices)
    {
        if (! v.active) continue;
        for (int i = 0; i < count && v.active; ++i)
        {
            if (v.position >= length)
            {
                if (! v.looping) { v.active = false; break; }
                v.position = v.startFrame + std::fmod(v.position-v.startFrame,(double) length-v.startFrame);
            }
            const int frame = (int) v.position;
            const int next = juce::jmin(length-1,frame+1);
            const float fraction = (float) (v.position-frame);
            // Fade toward EOF as well as NoteOff, preventing a hard cut on finite one-shots.
            const float endGain = v.looping
                ? (float) juce::jlimit(0.0,1.0,juce::jmin(length-v.position,v.position-v.startFrame)/(v.increment*v.attackFrames))
                : (float) juce::jlimit(0.0,1.0,(length-v.position)/(v.increment*v.releaseFrames));
            const float gain = v.envelope * v.velocity * endGain * 0.25f;
            for (int ch = 0; ch < out.getNumChannels(); ++ch)
            {
                const auto* data = samples.getReadPointer(juce::jmin(ch,samples.getNumChannels()-1));
                out.addSample(ch,start+i,(data[frame]+fraction*(data[next]-data[frame]))*gain);
            }
            v.position += v.increment;
            if (v.releasing) { v.envelope = juce::jmax(0.0f,v.envelope-v.releaseStep); if (v.envelope <= 0) v.active = false; }
            else v.envelope = juce::jmin(1.0f,v.envelope+1.0f/v.attackFrames);
        }
    }
}
void MidiSampler::render(juce::AudioBuffer<float>& out,int start,int count,const juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    out.clear(start,count);
    int position = 0;
    for (const auto metadata : midi)
    {
        const int frame = juce::jlimit(position,count,metadata.samplePosition);
        renderRange(out,start+position,frame-position); position = frame;
        // Ignore SysEx before constructing MidiMessage (long messages would allocate on the audio thread).
        if (metadata.numBytes <= 3) event(metadata.getMessage());
    }
    renderRange(out,start+position,count-position);
}
void PreviewOutput::prepareToPlay(int blockSize,double rate)
{
    transport.prepareToPlay(blockSize,rate); sampler.prepare(rate); sf2.prepare(rate); rex.prepare(rate); audition.prepare(rate); external.prepare(rate); messages.ensureSize(65536);
    midiBuffer.setSize(2,juce::jmax(1,blockSize));
    messages.clear(); collector.drain(messages); messages.clear();
}
void PreviewOutput::getNextAudioBlock(const juce::AudioSourceChannelInfo& info)
{
    constexpr float midiPreviewGain = 1.2f;
    messages.clear();
    if (collector.drain(messages))
    {
        sampler.reset(); // Also clear sustain state if pedal-up was among the dropped events.
        // A dropped NoteOff must not leave instrument voices stuck. Send panic through the
        // current MIDI route, without resetting ordinary transport or whole-loop audition.
        messages.clear();
        for (int channel = 1; channel <= 16; ++channel)
            messages.addEvent(juce::MidiMessage::allSoundOff(channel),0);
    }
    const bool midiEnabled = midi.load();
    const bool kitEnabled = sf2Mode.load() && midi.load();
    const bool externalEnabled = externalMode.load() && midi.load();
    const bool sliceEnabled = rexMode.load() && midi.load();
    const bool auditionEnabled = audition.hasAudio();
    // Chunk both paths: JUCE's resampler can otherwise resize its internal buffer for
    // callbacks larger than prepareToPlay's block size, even though our MIDI scratch fits.
    for (int offset = 0; offset < info.numSamples;)
    {
        const int count = juce::jmin(midiBuffer.getNumSamples(),info.numSamples-offset);
        transport.getNextAudioBlock({info.buffer,info.startSample+offset,count});
        if (kitEnabled) sf2.render(midiBuffer,0,count,messages);
        else if (externalEnabled) external.render(midiBuffer,0,count,messages);
        // REX mixes whole-loop audition internally, so boost only its MIDI voices there.
        else if (sliceEnabled) rex.render(midiBuffer,0,count,messages,midiPreviewGain);
        else if (midiEnabled) sampler.render(midiBuffer,0,count,messages);
        else midiBuffer.clear();
        if (! sliceEnabled && midiEnabled) midiBuffer.applyGain(0,count,midiPreviewGain);
        if (auditionEnabled) audition.render(midiBuffer,0,count);
        if (midiEnabled || kitEnabled || externalEnabled || sliceEnabled || auditionEnabled)
        {
            for (int ch = 0; ch < info.buffer->getNumChannels(); ++ch)
                info.buffer->addFrom(ch,info.startSample+offset,midiBuffer,juce::jmin(ch,1),0,count,gain.load());
        }
        messages.clear(); // Input is block-quantised; never retrigger notes for later chunks.
        offset += count;
    }
    lastOutputPeak = info.buffer->getMagnitude(0, info.startSample, info.numSamples);
    if (info.buffer->getNumChannels() > 1)
        lastOutputPeak = juce::jmax(lastOutputPeak.load(),
            info.buffer->getMagnitude(1, info.startSample, info.numSamples));
    if (! midiEnabled && ! kitEnabled && ! externalEnabled && ! sliceEnabled && ! auditionEnabled) return;
    // Smooth safety knee for dense chords; no hard clipping at the hardware output.
    for (int ch = 0; ch < info.buffer->getNumChannels(); ++ch)
        for (int i = info.startSample; i < info.startSample+info.numSamples; ++i)
        {
            auto& x = info.buffer->getWritePointer(ch)[i];
            if (std::abs(x) > 0.9f) x = std::copysign(0.9f+0.1f*std::tanh((std::abs(x)-0.9f)*10.0f),x);
        }
}
}
