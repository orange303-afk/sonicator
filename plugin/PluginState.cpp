#include "plugin/PluginState.h"
#include <limits>

namespace sonicator::plugin
{
namespace
{
constexpr const char* rootTag = "SONICATOR_PLUGIN";
constexpr const char* versionAttribute = "version";
constexpr const char* folderAttribute = "lastFolder";
constexpr const char* sampleAttribute = "lastSamplePath";
constexpr const char* widthAttribute = "width";
constexpr const char* heightAttribute = "height";

int clampedWidth(int width)
{
    return juce::jlimit(PluginState::minimumWidth, PluginState::maximumWidth, width);
}
int clampedHeight(int height)
{
    return juce::jlimit(PluginState::minimumHeight, PluginState::maximumHeight, height);
}
} // namespace

juce::MemoryBlock PluginState::toMemoryBlock() const
{
    juce::XmlElement xml(rootTag);
    xml.setAttribute(versionAttribute, version);
    xml.setAttribute(folderAttribute, lastFolder);
    xml.setAttribute(sampleAttribute, lastSamplePath);
    xml.setAttribute(widthAttribute, clampedWidth(windowWidth));
    xml.setAttribute(heightAttribute, clampedHeight(windowHeight));
    // UTF-8 keeps non-ASCII library paths exact across hosts that store the block verbatim.
    const auto text = xml.toString();
    juce::MemoryBlock block;
    block.append(text.toRawUTF8(), text.getNumBytesAsUTF8());
    return block;
}

PluginState PluginState::fromMemoryBlock(const void* data, size_t sizeInBytes)
{
    if (data == nullptr || sizeInBytes == 0) return {};
    if (sizeInBytes > (size_t) std::numeric_limits<int>::max()) return {};
    const auto text = juce::String::fromUTF8(static_cast<const char*>(data), (int) sizeInBytes);
    const auto xml = juce::XmlDocument::parse(text);
    if (xml == nullptr || xml->getTagName() != rootTag) return {};

    PluginState state;
    // A newer writer may add fields: keep its version and restore the fields this build knows.
    state.version = juce::jmax(0, xml->getIntAttribute(versionAttribute, currentVersion));
    state.lastFolder = xml->getStringAttribute(folderAttribute);
    state.lastSamplePath = xml->getStringAttribute(sampleAttribute);
    state.windowWidth = clampedWidth(xml->getIntAttribute(widthAttribute, defaultWidth));
    state.windowHeight = clampedHeight(xml->getIntAttribute(heightAttribute, defaultHeight));
    return state;
}
} // namespace sonicator::plugin
