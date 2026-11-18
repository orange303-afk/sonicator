#include "AppIcon.h"
#include "BinaryData.h"

namespace sonicator
{
juce::Image appWindowIcon()
{
    return juce::ImageFileFormat::loadFrom(BinaryData::iconwindow256_png,
                                           (size_t) BinaryData::iconwindow256_pngSize);
}
}
