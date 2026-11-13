#include "FileScanner.h"
#include "core/ableton/AbletonPacks.h"
#include "core/reason/ReasonPatch.h"
#include "core/kurzweil/KrzFile.h"
#include <stdexcept>
namespace sonicator {
bool FileScanner::isMetadataOnlyFormat(const juce::File& f)
{
    // Tracker modules are indexed by identity and header metadata; their PCM is decoded only
    // when the module is opened, never during a library scan.
    return f.hasFileExtension("rx2;sf2;exs;nki;sfz;mod;xm;s3m;xi;iso;img;bin;cue;mdx;repatch;krz;k25;k26");
}
bool FileScanner::isSupportedFormat(const juce::File& f)
{
    // A Kurzweil set is one row: its later parts are data only and are joined on load.
    if (krz::continuationPartNumber(f) >= 2) return false;
    if (isMetadataOnlyFormat(f)) return true;
    if (! f.hasFileExtension("wav;aif;aiff;flac;mp3;ogg;ebl;voc")) return false;
    // Ableton factory AIFC samples use the closed "able" codec and only Live can decode them.
    if (isAbletonProtectedSample(f)) return false;
    if (f.getFileName().startsWith("._"))
        if (auto input = f.createInputStream())
            if (input->readIntBigEndian() == 0x00051607) return false; // AppleDouble, not audio.
    return true;
}
std::vector<AudioFileInfo> FileScanner::scanDirectory(const juce::File& dir, CancelCheck cancel,
                                                      const std::function<bool(const juce::File&)>& shouldSkip)
{
    if (! dir.isDirectory()) throw std::runtime_error("Library folder is unavailable");
    std::vector<AudioFileInfo> out;
    if (isReasonPackPreview(dir)) return out;
    std::vector<juce::File> pending { dir };
    while (! pending.empty())
    {
        if (cancel && cancel()) return out;
        auto folder = pending.back(); pending.pop_back();
        for (const auto& entry : juce::RangedDirectoryIterator(folder, false, "*", juce::File::findFilesAndDirectories,
                                                             juce::File::FollowSymlinks::no))
        {
            if (cancel && cancel()) return out;
            auto f = entry.getFile();
            if (shouldSkip && shouldSkip(f)) continue;
            if (f.isDirectory()) { if (! f.isSymbolicLink() && !isReasonPackPreview(f)) pending.push_back(f); }
            else if (isSupportedFormat(f)) out.push_back({f,f.getSize(),f.getLastModificationTime().toMilliseconds()});
        }
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
