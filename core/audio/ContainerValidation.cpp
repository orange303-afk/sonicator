#include "ContainerValidation.h"
#include <limits>
namespace sonicator {
juce::String validateContainer(const juce::File& file, AssetCompatibility* compatibility)
{
    auto fail = [compatibility](juce::String message, AssetCompatibility verdict)
    {
        if (compatibility != nullptr) *compatibility = verdict;
        return message;
    };
    auto stream = file.createInputStream();
    if (! stream) return fail("Cannot open audio file", AssetCompatibility::unknown);
    char header[12] {};
    if (stream->read(header,12) != 12) return fail("Truncated audio header", AssetCompatibility::invalid);
    auto is = [](const char* bytes,const char* tag) { return std::memcmp(bytes,tag,4) == 0; };
    bool wav = (is(header,"RIFF") || is(header,"RIFX") || is(header,"RF64")) && is(header+8,"WAVE");
    bool aiff = is(header,"FORM") && (is(header+8,"AIFF") || is(header+8,"AIFC"));
    if (! wav && ! aiff) return {};
    bool big = aiff || is(header,"RIFX"), rf64 = is(header,"RF64");
    auto u32 = [big](const void* p) -> uint32_t
    { return big ? juce::ByteOrder::bigEndianInt(p) : juce::ByteOrder::littleEndianInt(p); };
    uint64_t length = (uint64_t) stream->getTotalLength();
    uint64_t rf64DataSize = 0;
    if (rf64)
    {
        char ds64[36] {};
        if (stream->read(ds64,36) != 36 || ! is(ds64,"ds64") || u32(ds64+4) < 28)
            return fail("Invalid RF64 size header", AssetCompatibility::invalid);
        rf64DataSize = juce::ByteOrder::littleEndianInt64(ds64+16);
    }
    // Real sample exporters often miscount RIFF/FORM or append metadata without updating it.
    // Reject a physically incomplete AUDIO payload, not a harmless outer size discrepancy.
    for (uint64_t offset = 12; length-offset >= 8;)
    {
        stream->setPosition((int64_t) offset);
        char chunk[8] {};
        if (stream->read(chunk,8) != 8) return fail("Truncated audio chunk header", AssetCompatibility::invalid);
        uint64_t size = u32(chunk+4);
        if (rf64 && size == std::numeric_limits<uint32_t>::max())
        {
            if (! is(chunk,"data")) return fail("Unsupported RF64 extended chunk table", AssetCompatibility::unsupported);
            size = rf64DataSize;
        }
        bool audioPayload = (wav && is(chunk,"data")) || (aiff && is(chunk,"SSND"));
        if (size > length-offset-8)
            return audioPayload ? fail("Truncated audio chunk payload", AssetCompatibility::invalid)
                                : juce::String();
        uint64_t padded = size + (size & 1);
        if (padded > length-offset-8) break; // tolerate an omitted final alignment byte
        offset += 8 + padded;
    }
    return stream->getStatus().failed()
        ? fail("Audio container read failed", AssetCompatibility::unknown) : juce::String();
}
} // namespace sonicator
