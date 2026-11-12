#include "BasicAnalyzer.h"
#include "core/emu/EblReader.h"
#include "core/audio/SonicatorFormats.h"
#include "core/audio/ContainerValidation.h"
#include <array>
#include <cmath>
namespace sonicator {
SampleRecord BasicAnalyzer::analyze(const juce::File& file, CancelCheck cancel) const
{
    SampleRecord r;
    r.filePath = file.getFullPathName(); r.fileName = file.getFileName();
    // A bank file name is a bare sequence ("Orbit 8080SL289.ebl"); the sample's own name is in
    // the header, and that is what a user searches for. An empty header keeps the file name.
    if (isEblFile(file))
    {
        const auto headerName = readEblName(file);
        if (headerName.isNotEmpty()) r.fileName = headerName;
    }
    r.format = file.getFileExtension().trimCharactersAtStart(".").toUpperCase();
    r.fileSize = file.getSize(); r.dateModified = file.getLastModificationTime().toMilliseconds();
    r.error = validateContainer(file, &r.compatibility);
    if (r.error.isNotEmpty()) return r;
    juce::AudioFormatManager formats; registerSonicatorFormats(formats);
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (! reader || reader->sampleRate <= 0 || reader->lengthInSamples <= 0
        || reader->numChannels == 0 || reader->numChannels > 64)
    {
        // A missing reader may be an unsupported codec or a corrupt file. Guessing would either
        // hide a real format gap or label a broken file unsupported, so it stays unknown.
        r.error = "Unsupported, empty or corrupt audio file"; return r;
    }
    r.duration = reader->lengthInSamples / reader->sampleRate;
    r.sampleRate = reader->sampleRate; r.numChannels = (int) reader->numChannels;
    r.bitsPerSample = r.format == "MP3" ? 0 : (int) reader->bitsPerSample;
    constexpr int n = 2048, bins = n / 2 + 1;
    juce::dsp::FFT fft(11);
    juce::dsp::WindowingFunction<float> window(n, juce::dsp::WindowingFunction<float>::hann, false);
    juce::AudioBuffer<float> buffer(r.numChannels, n);
    std::array<float, n * 2> transform {};
    std::array<double, bins> spectrum {};
    double sumSquares = 0;
    for (int64_t offset = 0; offset < reader->lengthInSamples; offset += n)
    {
        if (cancel && cancel()) { r.error = "Cancelled"; return r; }
        int count = (int) juce::jmin<int64_t>(n, reader->lengthInSamples - offset);
        buffer.clear();
        if (! reader->read(&buffer, 0, count, offset, true, true))
        {
            r.error = "Audio decode failed"; r.compatibility = AssetCompatibility::invalid; return r;
        }
        for (int channel = 0; channel < r.numChannels; ++channel)
        {
            auto* data = buffer.getReadPointer(channel);
            for (int i = 0; i < count; ++i)
            {
                double value = data[i];
                if (! std::isfinite(value))
                {
                    r.error = "Non-finite audio sample"; r.compatibility = AssetCompatibility::invalid; return r;
                }
                r.peakLevel = juce::jmax(r.peakLevel, std::abs(value));
                sumSquares += value * value;
            }
            transform.fill(0);
            std::copy(data, data + count, transform.begin());
            window.multiplyWithWindowingTable(transform.data(), n);
            fft.performFrequencyOnlyForwardTransform(transform.data());
            // Average channel powers rather than downmixing: anti-phase stereo must not cancel.
            for (int k = 0; k < bins; ++k) spectrum[k] += (double) transform[k] * transform[k];
        }
    }
    r.rmsLevel = std::sqrt(sumSquares / ((double) reader->lengthInSamples * r.numChannels));
    double total = 0, weighted = 0, logSum = 0;
    for (int k = 0; k < bins; ++k)
    {
        total += spectrum[k]; weighted += spectrum[k] * (k * r.sampleRate / n);
        logSum += std::log(juce::jmax(spectrum[k], 1.0e-30));
    }
    if (total > 1.0e-20)
    {
        r.spectralCentroid = weighted / total;
        double cumulative = 0;
        for (int k = 0; k < bins; ++k)
        {
            cumulative += spectrum[k];
            if (cumulative >= 0.85 * total) { r.spectralRolloff = k * r.sampleRate / n; break; }
        }
        r.spectralFlatness = juce::jlimit(0.0, 1.0, std::exp(logSum / bins) / (total / bins));
    }
    r.dateAnalyzed = juce::Time::currentTimeMillis(); r.version = basicFeaturesVersion;
    r.compatibility = AssetCompatibility::supported;
    return r;
}
SampleRecord BasicAnalyzer::analyzeAndHash(const juce::File& file, CancelCheck cancel) const
{
    auto r = analyze(file,cancel);
    if (r.error.isNotEmpty()) return r;
    try
    {
        r.fileHash = FileScanner::computeFileHash(file,cancel);
        if (file.getSize() != r.fileSize || file.getLastModificationTime().toMilliseconds() != r.dateModified)
            r.error = "File changed during analysis; rescan to retry";
    }
    catch (const std::exception& e) { r.error = juce::String::fromUTF8(e.what()); }
    if (r.error.isNotEmpty()) { r.version = 0; r.fileHash.clear(); }
    return r;
}
}
