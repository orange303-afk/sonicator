#include "TrackerFormats.h"
#include <juce_core/juce_core.h>
namespace sonicator {
juce::String trackerFormatName(TrackerFormat format)
{
    switch (format)
    {
        case TrackerFormat::mod: return "MOD";
        case TrackerFormat::xm: return "XM";
        case TrackerFormat::s3m: return "S3M";
        case TrackerFormat::xi: return "XI";
    }
    return {};
}

juce::String TrackerBytes::text(size_t n, bool preserveWhitespace)
{
    const auto start = position;
    if (! has(n)) { failed = true; return {}; }
    position += n;
    auto end = n;
    while (end > 0 && source[start + end - 1] == 0) --end;
    juce::String text;
    for (size_t i = 0; i < end; ++i)
    {
        const auto c = source[start + i];
        if (c == 0) break;
        if (c == 0x1A) break;
        text << juce::String::charToString(c < 0x20 ? 0x20 : (juce::juce_wchar) c);
    }
    return preserveWhitespace ? text : text.trim();
}

bool readWholeFile(const juce::File& file, int64_t maxBytes, std::vector<uint8_t>& out,
                   juce::String& error)
{
    out.clear();
    const auto size = file.getSize();
    if (size <= 0) { error = "The file is empty"; return false; }
    if (size > maxBytes)
    {
        error = "Tracker module exceeds the " + juce::String(maxBytes / (1024 * 1024)) + " MiB limit";
        return false;
    }
    juce::FileInputStream stream(file);
    if (! stream.openedOk()) { error = "Cannot open the module"; return false; }
    out.resize((size_t) size);
    const auto read = stream.read(out.data(), (int) size);
    if (read != (int) size || stream.getStatus().failed())
    {
        out.clear();
        error = "Cannot read the whole module";
        return false;
    }
    return true;
}
} // namespace sonicator
