#include "SpectralAnalysisService.h"
#include "core/audio/SonicatorFormats.h"
#include <algorithm>

namespace sonicator
{
namespace
{
// Two workers keep a large selection moving without turning the machine into a space heater.
constexpr int workerCount = 2;
constexpr int overviewColumns = 8192;
constexpr int overviewRows = 256;

SpectrogramGrid buildOverview(const juce::File& file)
{
    juce::AudioFormatManager formats;
    registerSonicatorFormats(formats);
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
        return {};
    const int channels = juce::jlimit(1, 2, (int) reader->numChannels);
    auto* pointer = reader.get();
    const auto read = [pointer, channels](int64_t start, int count, int requested,
                                          juce::AudioBuffer<float>& out)
    {
        if (requested != channels) return false;
        out.clear();
        pointer->read(&out, 0, count, start, channels >= 1, channels >= 2);
        return true;
    };
    SpectrogramParams params;
    params.fftSize = 2048;
    params.rows = overviewRows;
    params.columns = overviewColumns;
    params.maxFrames = overviewColumns;
    params.logFrequency = true;
    params.minFrequency = 20.0;
    params.floorDb = -90.0;
    return computeSpectrogram(read, (int) std::lround(reader->sampleRate), channels, 0,
                              reader->lengthInSamples, params);
}
} // namespace

SpectralAnalysisService::SpectralAnalysisService(juce::File cacheFolder)
    : folder(std::move(cacheFolder))
{
}

SpectralAnalysisService::~SpectralAnalysisService()
{
    cancel();
    if (worker.joinable()) worker.join();
}

void SpectralAnalysisService::start(std::vector<juce::File> files)
{
    if (running.load()) return;
    if (worker.joinable()) worker.join();
    done = 0;
    total = (int) files.size();
    skipped = 0;
    failed = 0;
    cancelling = false;
    {
        const std::lock_guard<std::mutex> guard(mutex);
        current.clear();
        finished = false;
        lastSummary = {};
    }
    running = true;
    worker = std::thread([this, files = std::move(files)]() mutable { run(std::move(files)); });
}

void SpectralAnalysisService::cancel()
{
    cancelling = true;
}

SpectralAnalysisService::Progress SpectralAnalysisService::progress() const
{
    Progress p;
    p.running = running.load();
    p.done = done.load();
    p.total = total.load();
    p.skipped = skipped.load();
    p.failed = failed.load();
    p.bytes = spectrumCacheBytes(folder);
    const std::lock_guard<std::mutex> guard(mutex);
    p.current = current;
    return p;
}

bool SpectralAnalysisService::takeFinished(Progress& summary)
{
    const std::lock_guard<std::mutex> guard(mutex);
    if (! finished) return false;
    finished = false;
    summary = lastSummary;
    return true;
}

void SpectralAnalysisService::run(std::vector<juce::File> files)
{
    std::atomic<size_t> next { 0 };
    const auto work = [this, &files, &next]
    {
        for (;;)
        {
            if (cancelling.load()) return;
            const auto index = next.fetch_add(1);
            if (index >= files.size()) return;
            const auto file = files[index];
            {
                const std::lock_guard<std::mutex> guard(mutex);
                current = file.getFileName();
            }
            if (spectrumCacheIsFresh(file, folder))
                ++skipped;
            else
            {
                const auto grid = buildOverview(file);
                if (! (grid.valid() && writeSpectrumCache(file, grid, folder)))
                    ++failed;
            }
            ++done;
        }
    };

    std::vector<std::thread> workers;
    const int count = juce::jlimit(1, workerCount, (int) juce::jmax<size_t>(1, files.size()));
    for (int i = 0; i < count; ++i) workers.emplace_back(work);
    for (auto& thread : workers) thread.join();

    Progress summary;
    summary.running = false;
    summary.done = done.load();
    summary.total = total.load();
    summary.skipped = skipped.load();
    summary.failed = failed.load();
    summary.bytes = spectrumCacheBytes(folder);
    running = false;
    const std::lock_guard<std::mutex> guard(mutex);
    current.clear();
    lastSummary = summary;
    finished = true;
}
} // namespace sonicator
