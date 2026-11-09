#include "EmbeddedSamples.h"
#include "core/sf2/Sf2Bank.h"
#include "core/tracker/TrackerDocument.h"
#include <xmp.h>
#include <cmath>
namespace sonicator {
namespace {
constexpr int libxmpC4 = 61; // libxmp maps period 428 / tracker C-4 to note 61.
std::vector<int> firstNotesPerSample(const juce::File& file, int sampleCount)
{
    std::vector<int> notes((size_t) juce::jmax(0, sampleCount), 0);
    xmp_context context = xmp_create_context();
    if (context == nullptr) return notes;
    if (xmp_load_module(context, file.getFullPathName().toRawUTF8()) != 0)
    {
        xmp_free_context(context);
        return notes;
    }
    xmp_module_info info {};
    xmp_get_module_info(context, &info);
    if (info.mod != nullptr)
    {
        const auto& module = *info.mod;
        const auto record = [&notes, &module](int instrumentIndex, int note)
        {
            if (instrumentIndex < 0 || instrumentIndex >= module.ins) return;
            const auto& instrument = module.xxi[instrumentIndex];
            if (instrument.sub == nullptr) return;
            for (int sub = 0; sub < instrument.nsm; ++sub)
            {
                const int sid = instrument.sub[sub].sid;
                if (sid >= 0 && sid < (int) notes.size() && notes[(size_t) sid] == 0)
                    notes[(size_t) sid] = note;
            }
        };
        for (int order = 0; order < module.len; ++order)
        {
            const int patternIndex = module.xxo[order];
            if (patternIndex < 0 || patternIndex >= module.pat) continue;
            const auto* pattern = module.xxp[patternIndex];
            if (pattern == nullptr) continue;
            for (int channel = 0; channel < module.chn; ++channel)
            {
                const int trackIndex = pattern->index[channel];
                if (trackIndex < 0 || trackIndex >= module.trk) continue;
                const auto* track = module.xxt[trackIndex];
                if (track == nullptr) continue;
                for (int row = 0; row < track->rows; ++row)
                {
                    const auto& event = track->event[row];
                    if (event.note >= 1 && event.note <= 96 && event.ins >= 1)
                        record(event.ins - 1, event.note);
                }
            }
        }
    }
    xmp_release_module(context);
    xmp_free_context(context);
    return notes;
}
} // namespace
SampleAudioPtr resampleForPlayback(const SampleAudio& audio, double ratio)
{
    if (! std::isfinite(ratio) || ratio <= 0.0) return {};
    if (std::abs(ratio - 1.0) < 1e-9) return std::make_shared<SampleAudio>(audio);
    const int channels = audio.channels > 1 && ! audio.right.empty() ? 2 : 1;
    if (audio.sampleRate <= 0 || audio.frames <= 0
        || (int64_t) audio.left.size() < audio.frames
        || (channels > 1 && (int64_t) audio.right.size() < audio.frames))
        return {};
    const double step = juce::jlimit(1.0 / 16.0, 16.0, ratio);
    const int64_t outFrames = juce::jmax<int64_t>(1,
        (int64_t) std::llround((double) audio.frames / step));
    auto out = std::make_shared<SampleAudio>();
    out->sampleRate = audio.sampleRate;
    out->channels = channels;
    out->bitsPerSample = audio.bitsPerSample;
    out->floatingPoint = audio.floatingPoint;
    out->frames = outFrames;
    out->left.resize((size_t) outFrames);
    const auto sampleChannel = [&audio, step](const std::vector<float>& source, int64_t frame)
    {
        const double position = (double) frame * step;
        const auto index = (int64_t) position;
        if (index >= audio.frames - 1) return source[(size_t) audio.frames - 1];
        const double fraction = position - (double) index;
        return (float) ((double) source[(size_t) index] * (1.0 - fraction)
                        + (double) source[(size_t) index + 1] * fraction);
    };
    for (int64_t frame = 0; frame < outFrames; ++frame)
        out->left[(size_t) frame] = sampleChannel(audio.left, frame);
    if (channels > 1)
    {
        out->right.resize((size_t) outFrames);
        for (int64_t frame = 0; frame < outFrames; ++frame)
            out->right[(size_t) frame] = sampleChannel(audio.right, frame);
    }
    return out;
}
namespace {
juce::String entryName(const juce::String& raw, int ordinal)
{
    auto name = raw.trim();
    if (name.isEmpty()) return "Sample " + juce::String(ordinal);
    return name;
}
} // namespace
EmbeddedSampleSet::EmbeddedSampleSet() = default;
EmbeddedSampleSet::~EmbeddedSampleSet() = default;
bool isEmbeddedSampleSource(const juce::File& file)
{
    return file.hasFileExtension("mod;xm;s3m;sf2");
}
bool isTrackerModuleSource(const juce::File& file)
{
    return file.hasFileExtension("mod;xm;s3m");
}
std::vector<juce::File> embeddedSources(const std::vector<juce::File>& files)
{
    std::vector<juce::File> out;
    for (const auto& file : files)
        if (file.existsAsFile() && isEmbeddedSampleSource(file)) out.push_back(file);
    return out;
}
bool EmbeddedSampleSet::open(const juce::File& file, juce::String& error)
{
    error.clear();
    kind = Kind::none;
    tracker.reset();
    bank.reset();
    listed.clear();
    firstNotes.clear();
    empties = 0;
    if (! file.existsAsFile())
    {
        error = "File is missing";
        return false;
    }
    if (file.hasFileExtension("mod;xm;s3m"))
    {
        auto document = TrackerDocument::open(file, error);
        if (document == nullptr) return false;
        for (const auto& slot : document->slots())
        {
            if (slot.empty || slot.fmInstrument || slot.frames <= 0) { ++empties; continue; }
            listed.push_back({ entryName(slot.name.isNotEmpty() ? slot.name : slot.displayText,
                                         (int) listed.size() + 1),
                               slot.slot });
        }
        kind = Kind::tracker;
        tracker = std::move(document);
        firstNotes = firstNotesPerSample(file, (int) tracker->slots().size());
        return true;
    }
    if (file.hasFileExtension("sf2"))
    {
        auto loaded = std::make_unique<Sf2Bank>();
        if (! loaded->load(file, error)) return false;
        const auto& samples = loaded->samples();
        for (int i = 0; i < (int) samples.size(); ++i)
        {
            const auto& sample = samples[(size_t) i];
            // A right-channel header is decoded through its left partner, which is listed instead.
            if (sample.sampleType == 2) continue;
            if (sample.end <= sample.start) { ++empties; continue; }
            listed.push_back({ entryName(sample.name, (int) listed.size() + 1), i });
        }
        kind = Kind::sf2;
        bank = std::move(loaded);
        return true;
    }
    error = "Extract samples supports only MOD, XM, S3M and SF2";
    return false;
}
SampleAudioPtr EmbeddedSampleSet::read(int index, juce::String& error) const
{
    error.clear();
    if (kind == Kind::tracker && tracker != nullptr) return tracker->sampleAudio(index, error);
    if (kind == Kind::sf2 && bank != nullptr) return bank->sampleAudio((SampleId) index, error);
    error = "No embedded sample set is open";
    return {};
}
double EmbeddedSampleSet::playbackRatio(int index) const
{
    if (kind != Kind::tracker || index < 0 || index >= (int) firstNotes.size()
        || firstNotes[(size_t) index] == 0)
        return 1.0;
    const double ratio = std::pow(2.0, ((double) firstNotes[(size_t) index] - libxmpC4) / 12.0);
    return juce::jlimit(1.0 / 16.0, 16.0, ratio);
}
} // namespace sonicator
