// Dumps one JSON line per audio file: the feature vector of featureSchema 2 plus the metadata a
// training set needs to group and split records. Labels come from the dataset's own metadata, so
// this tool knows nothing about them.

#include "core/dsp/AnalysisAudio.h"
#include "core/dsp/FeatureExtractorV2.h"
#include "core/dsp/FeatureJson.h"
#include <juce_core/juce_core.h>
#include <iostream>

namespace {

struct Options
{
    juce::String input;
    juce::String output;
    bool walk = false;
    int limit = 0;
};

void usage()
{
    std::cerr << "usage: sonicator-features --in <file|directory> [--out <file.jsonl>] [--walk] [--limit N]\n";
}

bool parse(Options& options, int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
    {
        const juce::String argument(argv[i]);
        const auto value = [&]() -> juce::String
        {
            if (i + 1 >= argc) return {};
            return juce::String(argv[++i]);
        };
        if (argument == "--in") options.input = value();
        else if (argument == "--out") options.output = value();
        else if (argument == "--walk") options.walk = true;
        else if (argument == "--limit") options.limit = value().getIntValue();
        else
        {
            std::cerr << "unknown argument: " << argument << "\n";
            return false;
        }
    }
    if (options.input.isEmpty())
    {
        std::cerr << "--in is required\n";
        return false;
    }
    return true;
}

bool isAudio(const juce::File& file)
{
    return file.hasFileExtension("wav;aif;aiff;flac;ogg;mp3;w64;caf");
}

} // namespace

int main(int argc, char** argv)
{
    Options options;
    if (! parse(options, argc, argv))
    {
        usage();
        return 2;
    }

    const juce::File input(options.input);
    if (! input.exists())
    {
        std::cerr << "no such file or directory: " << input.getFullPathName() << "\n";
        return 2;
    }

    juce::Array<juce::File> files;
    if (input.isDirectory())
        input.findChildFiles(files, juce::File::findFiles, options.walk, "*");
    else
        files.add(input);
    files.removeIf([&](const juce::File& file) { return input.isDirectory() && ! isAudio(file); });
    files.sort();

    std::unique_ptr<juce::FileOutputStream> stream;
    if (options.output.isNotEmpty())
    {
        const juce::File output(options.output);
        output.deleteFile();
        stream = std::make_unique<juce::FileOutputStream>(output);
        if (! stream->openedOk())
        {
            std::cerr << "cannot write " << output.getFullPathName() << "\n";
            return 2;
        }
    }

    int written = 0, failed = 0;
    for (const auto& file : files)
    {
        if (options.limit > 0 && written >= options.limit) break;

        sonicator::AnalysisAudioOptions audioOptions;
        audioOptions.maxSeconds = 10.0;
        const auto audio = sonicator::readAnalysisAudio(file, audioOptions, {});
        if (! audio.ok())
        {
            ++failed;
            std::cerr << "skipped " << file.getFullPathName() << ": " << audio.error << "\n";
            continue;
        }

        const auto vector = sonicator::FeatureExtractorV2::extract(audio.buffer, audio.sampleRate);
        if (! vector.ok())
        {
            ++failed;
            std::cerr << "skipped " << file.getFullPathName() << ": " << vector.error << "\n";
            continue;
        }

        const double duration = static_cast<double>(audio.buffer.getNumSamples()) / audio.sampleRate;
        const auto line = sonicator::featureJsonLine(vector, file.getFullPathName(), audio.sampleRate, duration,
                                                    audio.sampleRate * 0.5);
        if (stream != nullptr) stream->writeText(line + "\n", false, false, "\n");
        else std::cout << line << "\n";
        ++written;
    }

    if (stream != nullptr) stream->flush();
    std::cerr << "wrote " << written << " lines, " << failed << " skipped\n";
    return failed == 0 || written > 0 ? 0 : 1;
}
