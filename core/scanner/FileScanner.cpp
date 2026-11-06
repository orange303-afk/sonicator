#include "FileScanner.h"
#include <stdexcept>
namespace sonicator {
bool FileScanner::isSupportedFormat(const juce::File& f)
{
    return f.hasFileExtension("wav;aif;aiff;flac;mp3");
}
std::vector<AudioFileInfo> FileScanner::scanDirectory(const juce::File& dir, CancelCheck cancel)
{
    if (! dir.isDirectory()) throw std::runtime_error("Library folder is unavailable");
    std::vector<AudioFileInfo> out;
    for (const auto& entry : juce::RangedDirectoryIterator(dir, true, "*", juce::File::findFiles,
                                                         juce::File::FollowSymlinks::no))
    {
        if (cancel && cancel()) break;
        auto f = entry.getFile();
        if (isSupportedFormat(f)) out.push_back({f, f.getSize(), f.getLastModificationTime().toMilliseconds()});
    }
    return out;
}
juce::String FileScanner::computeFileHash(const juce::File& f)
{
    auto stream = f.createInputStream();
    if (! stream) throw std::runtime_error("Cannot open file for hashing");
    auto hash = juce::SHA256(*stream).toHexString();
    if (stream->getStatus().failed()) throw std::runtime_error("File read failed during hashing");
    return hash;
}
}
