#include "AudioEngine.h"
#include "core/audio/SonicatorFormats.h"
#include "ContainerValidation.h"
#include "core/legacy/LegacyFormatProbe.h"
#include <algorithm>
#include <cmath>
namespace sonicator {
bool AudioEngine::selectReasonReference(int index)
{
    if (!reasonMode || index<0 || index>=(int)reasonMetadata.references.size()) return false;
    auto metadata=reasonMetadata;
    const auto ref=metadata.references[(size_t)index];
    // Reuse ordinary WAV/AIFF/RX2 playback, keeping the metadata document separate.
    // An unavailable reference also unloads the previous source: Play must not replay it.
    const bool ok=loadFile(ref.isLocal() ? ref.file : juce::File());
    reasonMode=true; reasonMetadata=std::move(metadata); reasonReference=index;
    if (!ref.isLocal()) lastError=ref.message;
    return ok;
}
AudioEngine::AudioEngine(bool openDevice) : deviceMode(openDevice)
{
    registerSonicatorFormats(formats);
    if (deviceMode)
    {
        readAhead.startThread();
        lastError = devices.initialiseWithDefaultDevices(0,2);
        player.setSource(&output);
        devices.addAudioCallback(&player);
    }
    source.setGain(0.7f);
}
AudioEngine::~AudioEngine()
{
    devices.removeMidiInputDeviceCallback({},&output.collector);
    devices.removeAudioCallback(&player);
    detachRender();
    player.setSource(nullptr);
    source.setSource(nullptr);
    reader.reset();
    readAhead.stopThread(-1);
    devices.closeAudioDevice();
}
void AudioEngine::startHostAudio(double sampleRate, int maximumBlockSize)
{
    const auto rate = sampleRate > 0.0 ? sampleRate : 44100.0;
    const auto blockSize = juce::jmax(1, maximumBlockSize);
    if (hostMode)
    {
        // The host may change the session sample rate or block size without recreating the
        // plugin; re-prepare so preview audio follows the new rate.
        if (rate == hostSampleRate && blockSize == hostBlockSize) return;
        hostSampleRate = rate;
        hostBlockSize = blockSize;
        output.prepareToPlay(blockSize, rate);
        return;
    }
    hostMode = true;
    hostSampleRate = rate;
    hostBlockSize = blockSize;
    hostRenderAttached = true;
    if (! readAhead.isThreadRunning()) readAhead.startThread();
    output.prepareToPlay(blockSize, hostSampleRate);
}
void AudioEngine::stopHostAudio()
{
    if (! hostMode) return;
    stopTransport();
    detachRender();
    hostMode = false;
    hostRenderAttached = false;
    source.setSource(nullptr);
    readAhead.stopThread(2000);
}
void AudioEngine::processHostBlock(juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi)
{
    lastHostBlockMs.store(juce::Time::getMillisecondCounter());
    const juce::ScopedLock callbackGuard(devices.getAudioCallbackLock());
    if (! hostMode || ! hostRenderAttached)
    {
        buffer.clear();
        return;
    }
    for (const auto metadata : midi)
        output.collector.push(metadata.getMessage());
    output.getNextAudioBlock({ &buffer, 0, buffer.getNumSamples() });
}
void AudioEngine::detachRender()
{
    if (deviceMode) { player.setSource(nullptr); return; }
    if (! hostMode) return;
    const juce::ScopedLock callbackGuard(devices.getAudioCallbackLock());
    hostRenderAttached = false;
}
void AudioEngine::attachRender()
{
    if (deviceMode) { player.setSource(&output); return; }
    if (! hostMode) return;
    const juce::ScopedLock callbackGuard(devices.getAudioCallbackLock());
    hostRenderAttached = true;
}
bool AudioEngine::stopTransport()
{
    // AudioTransportSource::stop() waits up to a second for the audio callback to set its
    // "stopped" flag. With neither a device nor a host block arriving there is nobody to answer,
    // so detach the source instead of paying that timeout on the message thread.
    const bool callbackAcknowledges = deviceMode
        || (hostMode && juce::Time::getMillisecondCounter() - lastHostBlockMs.load() < 250);
    if (callbackAcknowledges) { source.stop(); return false; }
    source.setSource(nullptr);
    if (reader != nullptr) attach(); // a stopped transport on the same reader, without the wait
    return true;
}
void AudioEngine::preparePreviewBlock(int blockSize)
{
    output.prepareToPlay(blockSize, deviceSampleRate());
}
void AudioEngine::renderPreviewBlock(juce::AudioBuffer<float>& buffer, int numSamples)
{
    output.getNextAudioBlock({ &buffer, 0, numSamples });
}
void AudioEngine::attach()
{
    const bool streaming = deviceMode || hostMode;
    // Looping reads straight from the reader. The read-ahead buffer keeps its own position modulo
    // the file length while the reader also wraps, and the two disagree after the first pass: the
    // buffer stops serving audio and the loop appears to stick. A live range loop has the same
    // reason to bypass it. Everything else keeps the read-ahead.
    const int readAheadSamples = streaming && ! readerRangeLoop && ! looping ? 32768 : 0;
    jassert (readAheadSamples == 0 || ! looping);
    source.setSource(reader.get(), readAheadSamples,
                     readAheadSamples > 0 ? &readAhead : nullptr, inputRate);
}
bool AudioEngine::loadFile(const juce::File& file)
{
    const bool midi = isMidiPreview();
    // Transport stop needs the still-connected callback to acknowledge its fade-out.
    stopTransport();
    detachRender();
    sampler.clear();
    clearContainers();
    loadedFile = juce::File(); audibleStart = -1; startAtNextPlay = true;
    source.setSource(nullptr); reader.reset(); readerRangeLoop = false;
    if (file.hasFileExtension("cmb;repatch;rfl"))
    {
        reasonMetadata=readReasonPatch(file);
        lastError=reasonMetadata.error;
        reasonMode=lastError.isEmpty();
        output.midi=false;
        attachRender();
        return reasonMode;
    }
    lastError = validateContainer(file);
    if (lastError.isNotEmpty()) { attachRender(); return false; }
    const auto extension = file.getFileExtension().toLowerCase();
    if (extension == ".sf2")
    {
        attachRender();
        return previewSoundFont(file);
    }
    if (extension == ".rx2")
    {
        attachRender();
        return previewRex2(file);
    }
    if (extension == ".exs" || extension == ".nki" || extension == ".krz"
        || extension == ".k25" || extension == ".k26" || extension == ".sfz")
    {
        attachRender();
        return previewFileInstrument(file);
    }
    if (isLegacyTrackerExtension(file))
    {
        attachRender();
        return previewTracker(file);
    }
    std::unique_ptr<juce::AudioFormatReader> decoded(formats.createReaderFor(file));
    if (! decoded || decoded->sampleRate <= 0 || decoded->lengthInSamples <= 0)
    {
        lastError = "Cannot decode " + file.getFileName(); attachRender(); return false;
    }
    inputRate = decoded->sampleRate;
    reader = std::make_unique<PreviewReaderSource>(
        std::unique_ptr<juce::AudioFormatReader>(decoded.release()));
    reader->setLooping(looping);
    attach();
    loadedFile = file;
    lastError.clear();
    if (midi) lastError = sampler.load(file);
    attachRender();
    if (lastError.isNotEmpty()) return false;
    return true;
}
void AudioEngine::play()
{
    if (trackerMode)
    {
        if (trackerSampleChosen) { sf2Audition.play(looping); return; }
        // Restarting after the end flushes the renderer ring, so the consumer must be detached.
        if (trackerSong.isFinished())
        {
            detachRender();
            trackerSong.stop();
            trackerSong.play();
            attachRender();
        }
        else trackerSong.play();
        return;
    }
    if (auditionMode()) { sf2Audition.play(looping); return; }
    if (rexMode) { rexSampler.startAudition(looping); return; }
    if (! reader) return;
    if (source.hasStreamFinished() || startAtNextPlay)
    {
        if (skipSilence && audibleStart < 0)
        {
            std::unique_ptr<juce::AudioFormatReader> decoded(formats.createReaderFor(loadedFile));
            audibleStart = decoded ? firstAudibleFrame(*decoded)/inputRate : 0;
        }
        source.setPosition(skipSilence ? juce::jmax(0.0,audibleStart) : 0);
        startAtNextPlay = false;
    }
    source.start();
}
void AudioEngine::clearContainers()
{
    reasonMode=false;
    reasonMetadata={};
    reasonReference=-1;
    sf2Mode = false;
    output.sf2Mode = false;
    sf2Sampler.reset();
    sf2Bank.clear();
    sf2Samples.clear();
    sf2SampleId = invalidSampleId;
    sf2Sample.reset();
    sf2Audition.clear();
    externalMode = false;
    output.externalMode = false;
    externalSampler.reset();
    externalInstrument = {};
    externalSampleId = invalidSampleId;
    externalSample.reset();
    rexMode = false;
    output.rexMode = false;
    rexSampler.reset();
    trackerMode = false;
    trackerSampleChosen = false;
    output.trackerSongMode = false;
    output.trackerSampleMidi = false;
    trackerSong.unload();
    trackerDocument.reset();
    trackerSampleIdValue = invalidSampleId;
    trackerSample.reset();
    discMode = false;
    output.discMidi = false;
    discSample.reset();
    sampleVoices.clear();
    midiSelectionStart = midiSelectionEnd = 0;
    sampler.clearRange();
    sampleVoices.clearRange();
}
void AudioEngine::applyMidiSelection()
{
    sampler.clearRange();
    sampleVoices.clearRange();
    if (! (midiSelectionEnd > midiSelectionStart)) return;
    if (trackerMode || discMode) sampleVoices.setRange(midiSelectionStart, midiSelectionEnd);
    else if (! sf2Mode && ! externalMode && ! rexMode)
        sampler.setRange(midiSelectionStart, midiSelectionEnd);
}
void AudioEngine::setMidiSelection(double startSeconds, double endSeconds)
{
    if (std::isfinite(startSeconds) && std::isfinite(endSeconds) && endSeconds > startSeconds)
    {
        midiSelectionStart = startSeconds;
        midiSelectionEnd = endSeconds;
    }
    else midiSelectionStart = midiSelectionEnd = 0;
    applyMidiSelection();
}
double AudioEngine::deviceSampleRate() const
{
    if (hostMode) return hostSampleRate;
    if (deviceMode)
        if (auto* device = devices.getCurrentAudioDevice())
            return device->getCurrentSampleRate();
    return 44100.0;
}
juce::String AudioEngine::instrumentProgramLabel(int index) const
{
    if (index < 0 || index >= (int) externalInstrument.programs.size()) return {};
    const auto& program = externalInstrument.programs[(size_t) index];
    return juce::String(program.number) + "  " + program.name;
}
bool AudioEngine::selectInstrumentProgram(int index)
{
    if (! externalMode) { lastError = "Instrument is not loaded"; return false; }
    detachRender();
    juce::String error;
    if (! sonicator::selectInstrumentProgram(externalInstrument, index, error))
    {
        lastError = error;
        attachRender();
        return false;
    }
    lastError.clear();
    if (! externalSampler.load(externalInstrument, error))
    {
        lastError = error;
        attachRender();
        return false;
    }
    externalSampler.prepare(deviceSampleRate());
    ++externalGeneration;
    externalSampler.setGeneration(externalGeneration);
    refreshExternalSamples(true);
    attachRender();
    return true;
}
juce::String AudioEngine::soundFontPresetLabel(int index) const
{
    if (index < 0 || index >= (int) sf2Bank.presets().size()) return {};
    const auto& preset = sf2Bank.presets()[(size_t) index];
    return juce::String(preset.bank) + ":" + juce::String(preset.program) + "  " + preset.name;
}
bool AudioEngine::selectSoundFontPreset(int index)
{
    if (! sf2Mode) return false;
    detachRender();
    juce::String error;
    const bool ok = sf2Sampler.load(sf2Bank, index, error);
    if (! ok) lastError = error;
    else
    {
        sf2Sampler.prepare(deviceSampleRate());
        output.sf2Mode = true;
        lastError.clear();
        ++sf2Generation;
        sf2Sampler.setGeneration(sf2Generation);
        refreshSoundFontSamples(true);
    }
    attachRender();
    return ok;
}
std::vector<SampleDescriptor> AudioEngine::soundFontSamples(juce::String& error) const
{
    error.clear();
    if (! sf2Mode) { error = "SoundFont preset is not loaded"; return {}; }
    return sf2Samples;
}
bool AudioEngine::refreshSoundFontSamples(bool selectFirstPlayable)
{
    sf2Samples.clear();
    sf2SampleId = invalidSampleId;
    sf2Sample.reset();
    sf2Audition.clear();
    juce::String error;
    sf2Samples = sf2Bank.usedSamples(sf2Sampler.presetIndex(), error);
    if (! error.isEmpty()) { lastError = error; return false; }
    if (! selectFirstPlayable) return true;
    const auto first = std::find_if(sf2Samples.begin(), sf2Samples.end(),
        [] (const SampleDescriptor& descriptor) { return descriptor.isPlayable(); });
    if (first != sf2Samples.end()) return selectSoundFontSample(first->id);
    if (! sf2Samples.empty()) lastError = sf2Samples.front().error;
    else lastError = "SoundFont preset has no usable samples";
    return false;
}
bool AudioEngine::selectSoundFontSample(SampleId id)
{
    if (! sf2Mode) { lastError = "SoundFont preset is not loaded"; return false; }
    const auto found = std::find_if(sf2Samples.begin(), sf2Samples.end(),
        [id] (const SampleDescriptor& descriptor) { return descriptor.id == id; });
    if (found == sf2Samples.end()) { lastError = "SoundFont sample is not part of this preset"; return false; }
    if (! found->isPlayable()) { lastError = found->error; return false; }
    juce::String error;
    auto audio = sf2Bank.sampleAudio(id, error);
    if (audio == nullptr) { lastError = error.isNotEmpty() ? error : "Cannot decode SoundFont sample"; return false; }
    {
        // Publish only the audition source. Reattaching PreviewOutput calls prepareToPlay(),
        // which resets every MIDI voice (including the note that caused MIDI-follow).
        const juce::ScopedLock callbackGuard(devices.getAudioCallbackLock());
        sf2Audition.setAudio(audio);
        sf2Audition.prepare(deviceSampleRate());
    }
    sf2SampleId = id;
    sf2Sample = std::move(audio);
    lastError.clear();
    return true;
}
bool AudioEngine::previewFileInstrument(const juce::File& file)
{
    detachRender();
    clearContainers();
    const auto extension = file.getFileExtension().toLowerCase();
    const auto format = extension == ".exs" ? FileInstrumentFormat::exs24
        : (extension == ".krz" || extension == ".k25" || extension == ".k26" ? FileInstrumentFormat::krz
        : (extension == ".sfz" ? FileInstrumentFormat::sfz : FileInstrumentFormat::nki));
    juce::String error;
    if (! loadFileInstrument(file, format, externalInstrument, error))
    {
        externalInstrument = {};
        lastError = error;
        attachRender();
        return false;
    }
    if (! externalSampler.load(externalInstrument, error))
    {
        externalInstrument = {};
        lastError = error;
        attachRender();
        return false;
    }
    externalSampler.prepare(deviceSampleRate());
    externalMode = true;
    output.externalMode = true;
    ++externalGeneration;
    externalSampler.setGeneration(externalGeneration);
    loadedFile = file;
    lastError.clear();
    refreshExternalSamples(true);
    if (! isMidiPreview()) setMidiPreview(true);
    attachRender();
    return true;
}
bool AudioEngine::refreshExternalSamples(bool selectFirstPlayable)
{
    externalSampleId = invalidSampleId;
    externalSample.reset();
    sf2Audition.clear();
    if (! selectFirstPlayable) return true;
    const auto first = std::find_if(externalInstrument.sources.begin(), externalInstrument.sources.end(),
        [] (const SampleDescriptor& descriptor) { return descriptor.isPlayable(); });
    if (first != externalInstrument.sources.end()) return selectExternalSample(first->id);
    lastError = externalInstrument.sources.empty() ? "Instrument has no samples"
                                                   : "Instrument has no usable samples";
    return false;
}
bool AudioEngine::selectExternalSample(SampleId id)
{
    if (! externalMode) { lastError = "Instrument is not loaded"; return false; }
    if (id < 0 || id >= (int) externalInstrument.sources.size())
    { lastError = "Instrument sample index is out of range"; return false; }
    const auto& descriptor = externalInstrument.sources[(size_t) id];
    if (! descriptor.isPlayable())
    {
        lastError = descriptor.error.isNotEmpty() ? descriptor.error : "Instrument sample is unavailable";
        return false;
    }
    if (id >= (int) externalInstrument.audio.size() || externalInstrument.audio[(size_t) id] == nullptr)
    { lastError = "Instrument sample has no decoded audio"; return false; }
    {
        const juce::ScopedLock callbackGuard(devices.getAudioCallbackLock());
        sf2Audition.setAudio(externalInstrument.audio[(size_t) id]);
        sf2Audition.prepare(deviceSampleRate());
    }
    externalSampleId = id;
    externalSample = externalInstrument.audio[(size_t) id];
    lastError.clear();
    return true;
}
bool AudioEngine::previewTracker(const juce::File& file)
{
    detachRender();
    clearContainers();
    juce::String error;
    auto document = TrackerDocument::open(file, error);
    if (document == nullptr)
    {
        lastError = error.isNotEmpty() ? error : "Cannot open this tracker module";
        attachRender();
        return false;
    }
    // XI is a single instrument, not a module: libxmp has no song to render, so it enters the
    // same tracker document/sample path with its one source selected.
    if (document->info().format == TrackerFormat::xi)
    {
        trackerDocument = std::move(document);
        trackerMode = true;
        trackerSampleChosen = false;
        trackerSampleIdValue = invalidSampleId;
        output.trackerSongMode = false;
        output.trackerSampleMidi = false;
        ++trackerGenerationValue;
        loadedFile = file;
        lastError.clear();
        if (! selectTrackerSample(0))
        {
            if (lastError.isEmpty()) lastError = "The XI instrument has no playable sample";
            attachRender();
            return false;
        }
        attachRender();
        return true;
    }
    if (! trackerSong.load(file, deviceSampleRate(), error))
    {
        lastError = error.isNotEmpty() ? error : "Cannot start the tracker player";
        attachRender();
        return false;
    }
    trackerDocument = std::move(document);
    trackerSong.setLooping(looping);
    trackerMode = true;
    trackerSampleChosen = false;
    trackerSampleIdValue = invalidSampleId;
    output.trackerSongMode = true;
    output.trackerSampleMidi = false;
    ++trackerGenerationValue;
    loadedFile = file;
    lastError.clear();
    attachRender();
    return true;
}

const TrackerDocumentInfo& AudioEngine::trackerInfo() const
{
    static const TrackerDocumentInfo empty;
    return trackerDocument != nullptr ? trackerDocument->info() : empty;
}

bool AudioEngine::trackerHasSong() const
{
    return trackerMode && trackerDocument != nullptr
        && trackerDocument->info().format != TrackerFormat::xi;
}

const std::vector<TrackerSampleSlot>& AudioEngine::trackerSlots() const
{
    static const std::vector<TrackerSampleSlot> empty;
    return trackerDocument != nullptr ? trackerDocument->slots() : empty;
}

bool AudioEngine::selectTrackerSample(SampleId id)
{
    if (! trackerMode || trackerDocument == nullptr)
    { lastError = "No tracker module is loaded"; return false; }
    const auto& slots = trackerDocument->slots();
    if (id < 0 || id >= (int) slots.size()) { lastError = "Sample slot is out of range"; return false; }
    const auto& slot = slots[(size_t) id];
    if (slot.empty) { lastError = "Sample slot is empty"; return false; }
    if (slot.fmInstrument) { lastError = "AdLib instrument has no PCM sample"; return false; }
    if (slot.error.isNotEmpty()) { lastError = slot.error; return false; }
    juce::String error;
    auto audio = trackerDocument->sampleAudio((int) id, error);
    if (audio == nullptr)
    { lastError = error.isNotEmpty() ? error : "Cannot decode this sample"; return false; }
    // The song stops here; its position is kept by the renderer so returning to it is explicit.
    trackerSong.pause();
    discMode = false;
    output.discMidi = false;
    discSample.reset();
    {
        const juce::ScopedLock callbackGuard(devices.getAudioCallbackLock());
        midiSelectionStart = midiSelectionEnd = 0;
        sampleVoices.clearRange();
        sf2Audition.setAudio(audio);
        sf2Audition.prepare(deviceSampleRate());
        if (slot.looping) sf2Audition.setLoopRange(slot.loopStart, slot.loopEnd, slot.pingPong);
        sampleVoices.setSource(audio, slot.looping, slot.loopStart, slot.loopEnd, slot.pingPong);
    }
    trackerSample = audio;
    trackerSampleIdValue = id;
    trackerSampleChosen = true;
    output.trackerSongMode = false;
    output.trackerSampleMidi = true;
    ++trackerGenerationValue;
    lastError.clear();
    return true;
}

bool AudioEngine::selectTrackerWholeSong()
{
    if (! trackerMode) { lastError = "No tracker module is loaded"; return false; }
    if (! trackerHasSong())
    {
        lastError = "An XI file contains one instrument, not a song";
        return false;
    }
    trackerSampleChosen = false;
    trackerSampleIdValue = invalidSampleId;
    trackerSample.reset();
    {
        const juce::ScopedLock callbackGuard(devices.getAudioCallbackLock());
        midiSelectionStart = midiSelectionEnd = 0;
        sampleVoices.clearRange();
        sf2Audition.stop();
        sf2Audition.clear();
        sampleVoices.reset();
    }
    output.trackerSongMode = true;
    output.trackerSampleMidi = false;
    ++trackerGenerationValue;
    lastError.clear();
    return true;
}

bool AudioEngine::setDiscSample(const SampleAudioPtr& audio, bool looping, int64_t loopStart,
                                int64_t loopEnd, bool pingPong)
{
    if (audio == nullptr || audio->frames <= 0 || audio->sampleRate <= 0)
    {
        lastError = "The decoded disc sample is empty";
        return false;
    }
    detachRender();
    clearContainers();
    {
        const juce::ScopedLock callbackGuard(devices.getAudioCallbackLock());
        sf2Audition.setAudio(audio);
        sf2Audition.prepare(deviceSampleRate());
        if (looping) sf2Audition.setLoopRange(loopStart, loopEnd, pingPong);
        sampleVoices.setSource(audio, looping, loopStart, loopEnd, pingPong);
    }
    discMode = true;
    discSample = audio;
    ++discGenerationValue;
    output.discMidi = true;
    lastError.clear();
    attachRender();
    return true;
}

void AudioEngine::clearDiscSample()
{
    detachRender();
    {
        const juce::ScopedLock callbackGuard(devices.getAudioCallbackLock());
        sf2Audition.stop();
        sf2Audition.clear();
        sampleVoices.reset();
        sampleVoices.clear();
    }
    discMode = false;
    output.discMidi = false;
    discSample.reset();
    ++discGenerationValue;
    lastError.clear();
    attachRender();
}

bool AudioEngine::previewRex2(const juce::File& file, int startNote)
{
    detachRender();
    clearContainers();
    juce::String error;
    if (! rexSampler.load(file, startNote, error))
    {
        lastError = error;
        attachRender();
        return false;
    }
    rexSampler.prepare(deviceSampleRate());
    rexMode = true;
    output.rexMode = true;
    loadedFile = file;
    lastError.clear();
    if (! isMidiPreview()) setMidiPreview(true);
    attachRender();
    return true;
}
bool AudioEngine::previewSoundFont(const juce::File& file, int presetIndex)
{
    detachRender();
    clearContainers();
    juce::String error;
    if (! sf2Bank.load(file, error))
    {
        lastError = error;
        attachRender();
        return false;
    }
    int index = presetIndex;
    if (index < 0)
    {
        index = sf2Bank.firstDrumPreset();
        if (index < 0) index = 0; // a bank without a drum kit opens its first melodic preset
    }
    if (index >= (int) sf2Bank.presets().size() || ! sf2Sampler.load(sf2Bank, index, error))
    {
        lastError = error.isNotEmpty() ? error : "Cannot load SoundFont preset";
        attachRender();
        return false;
    }
    sf2Sampler.prepare(deviceSampleRate());
    sf2Mode = true;
    output.sf2Mode = true;
    ++sf2Generation;
    sf2Sampler.setGeneration(sf2Generation);
    loadedFile = file;
    lastError.clear();
    refreshSoundFontSamples(true);
    if (! isMidiPreview()) setMidiPreview(true);
    attachRender();
    return true;
}
void AudioEngine::pause()
{
    if (trackerMode)
    {
        if (trackerSampleChosen) sf2Audition.pause();
        else trackerSong.pause();
        return;
    }
    if (auditionMode()) { sf2Audition.pause(); return; }
    if (rexMode) { rexSampler.pauseAudition(); return; }
    // Pause keeps the position. Without a running callback stopTransport() rebuilds the
    // transport instead of waiting for an acknowledgement, which rewinds it.
    const auto positionBefore = position();
    if (stopTransport() && reader != nullptr)
        source.setPosition(positionBefore);
}
void AudioEngine::stop()
{
    if (auditionMode()) sf2Audition.stop();
    if (rexMode) rexSampler.stopAudition();
    if (trackerMode && trackerSampleChosen) sf2Audition.stop();
    stopTransport(); source.setPosition(0); startAtNextPlay = true;
    detachRender();
    sampler.reset();
    sf2Sampler.reset();
    externalSampler.reset();
    rexSampler.reset();
    sampleVoices.reset();
    // Stop always returns the composition to its beginning, including when a sample is selected.
    if (trackerMode) trackerSong.stop();
    attachRender();
}
void AudioEngine::seek(double seconds)
{
    if (! std::isfinite(seconds)) return;
    if (trackerMode)
    {
        if (trackerSampleChosen) { sf2Audition.seek(juce::jlimit(0.0, length(), seconds)); return; }
        const double target = juce::jlimit(0.0, length(), seconds);
        // The renderer flushes its ring, so the consumer must be detached while it happens.
        detachRender();
        trackerSong.seek(target);
        attachRender();
        return;
    }
    if (auditionMode()) { sf2Audition.seek(juce::jlimit(0.0, length(), seconds)); return; }
    if (rexMode) { rexSampler.seekAudition(juce::jlimit(0.0, length(), seconds)); return; }
    source.setPosition(juce::jlimit(0.0, length(), seconds)); startAtNextPlay = false;
}
void AudioEngine::setVolume(float gain)
{
    source.setGain(std::isfinite(gain) ? juce::jlimit(0.0f,1.0f,gain) : 0.0f);
    output.gain = source.getGain();
}
void AudioEngine::setLooping(bool enabled)
{
    if (looping == enabled) return;
    looping = enabled;
    sampler.loop = enabled;
    sampleVoices.setLooping(enabled);
    if (trackerMode)
    {
        if (trackerSampleChosen) sf2Audition.setLooping(enabled);
        else trackerSong.setLooping(enabled);
        return;
    }
    if (auditionMode()) { sf2Audition.setLooping(enabled); return; }
    if (rexMode) { rexSampler.setAuditionLoop(enabled); return; }
    if (! reader) return;
    auto positionBefore = position(); bool resume = isPlaying();
    stopTransport(); source.setSource(nullptr);
    reader->setLooping(looping); // read-ahead is detached before mutating its source.
    attach(); seek(positionBefore);
    if (resume) play();
}
void AudioEngine::setSelectionLoop(bool enabled, double startSeconds, double endSeconds)
{
    if (reader == nullptr) return;
    const bool valid = std::isfinite(startSeconds) && std::isfinite(endSeconds) && endSeconds > startSeconds;
    const bool active = enabled && valid;
    if (valid) reader->setLoopRange(startSeconds, endSeconds);
    else reader->clearLoopRange();
    reader->setRangeLooping(active);
    if (active == readerRangeLoop) return;
    // The read-ahead buffer is attached or removed here. Live range edits never rebuild: only the
    // first and the last active edit do, so a mouse drag keeps producing continuous audio.
    readerRangeLoop = active;
    const double positionBefore = position();
    const bool resume = isPlaying();
    source.setSource(nullptr);
    attach();
    seek(positionBefore);
    if (resume) play();
}
bool AudioEngine::setMidiPreview(bool enabled)
{
    if (enabled == isMidiPreview()) return true;
    lastError.clear();
    const bool wantsSample = enabled && loadedFile != juce::File() && ! sf2Mode && ! externalMode
        && ! rexMode && ! trackerMode && ! discMode;
    MidiSampler::Decoded decoded;
    // The whole file is decoded before any state is touched, so ordinary preview continues to play
    // while the MIDI sample is prepared. Only the buffer swap needs the callback lock.
    if (wantsSample) lastError = MidiSampler::decode(loadedFile, decoded);
    MidiSampler::Decoded previous;
    {
        const juce::ScopedLock callbackGuard(devices.getAudioCallbackLock());
        previous = sampler.release();
        sampleVoices.reset();
        if (wantsSample && lastError.isEmpty()) sampler.adopt(std::move(decoded));
        applyMidiSelection(); // a waveform selection survives a MIDI toggle
        output.midi = enabled && lastError.isEmpty();
    }
    previous = {}; // release the old decoded PCM only after the audio callback can run again
    if (deviceMode)
    {
        if (enabled && ! midiInputsConnected)
        {
            for (const auto& input : juce::MidiInput::getAvailableDevices())
                devices.setMidiInputDeviceEnabled(input.identifier,true);
            devices.addMidiInputDeviceCallback({},&output.collector); midiInputsConnected = true;
        }
        player.setSource(&output);
    }
    attachRender(); // re-attach after detachRender() also in host mode
    return lastError.isEmpty();
}
}
