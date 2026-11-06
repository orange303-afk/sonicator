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
juce::String FileScanner::computeFileHash(const juce::File& f, CancelCheck cancel)
{
    auto stream = f.createInputStream();
    if (! stream) throw std::runtime_error("Cannot open file for hashing");
    class CheckedStream final : public juce::InputStream
    {
    public:
        CheckedStream(juce::InputStream& s, CancelCheck c) : input(s), check(std::move(c)) {}
        int64_t getTotalLength() override { return input.getTotalLength(); }
        int64_t getPosition() override { return input.getPosition(); }
        bool setPosition(int64_t p) override { return input.setPosition(p); }
        bool isExhausted() override { return interrupted || input.isExhausted(); }
        int read(void* dest, int bytes) override
        {
            if (check && check()) { interrupted = true; return 0; }
            return input.read(dest, juce::jmin(bytes, 65536));
        }
        bool interrupted = false;
    private:
        juce::InputStream& input;
        CancelCheck check;
    } checked(*stream, std::move(cancel));
    auto hash = juce::SHA256(checked).toHexString();
    if (checked.interrupted) throw std::runtime_error("Cancelled");
    if (stream->getStatus().failed()) throw std::runtime_error("File read failed during hashing");
    return hash;
}
}
