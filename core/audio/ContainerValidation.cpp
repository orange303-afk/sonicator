#include "ContainerValidation.h"
#include <limits>
namespace sonicator {
juce::String validateContainer(const juce::File& file)
{
    auto stream = file.createInputStream();
    if (! stream) return "Cannot open audio file";
    char header[12] {};
    if (stream->read(header,12) != 12) return "Truncated audio header";
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
            return "Invalid RF64 size header";
        rf64DataSize = juce::ByteOrder::littleEndianInt64(ds64+16);
    }
    // Real sample exporters often miscount RIFF/FORM or append metadata without updating it.
    // Reject a physically incomplete AUDIO payload, not a harmless outer size discrepancy.
    for (uint64_t offset = 12; length-offset >= 8;)
    {
        stream->setPosition((int64_t) offset);
        char chunk[8] {};
        if (stream->read(chunk,8) != 8) return "Truncated audio chunk header";
        uint64_t size = u32(chunk+4);
        if (rf64 && size == std::numeric_limits<uint32_t>::max())
        {
            if (! is(chunk,"data")) return "Unsupported RF64 extended chunk table";
            size = rf64DataSize;
        }
        bool audioPayload = (wav && is(chunk,"data")) || (aiff && is(chunk,"SSND"));
        if (size > length-offset-8)
            return audioPayload ? "Truncated audio chunk payload" : juce::String();
        uint64_t padded = size + (size & 1);
        if (padded > length-offset-8) break; // tolerate an omitted final alignment byte
        offset += 8 + padded;
    }
    return stream->getStatus().failed() ? "Audio container read failed" : juce::String();
}
}
