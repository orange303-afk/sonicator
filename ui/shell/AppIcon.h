#pragma once
#include <juce_graphics/juce_graphics.h>

namespace sonicator
{
// The mark shown in the title bar and the taskbar. Windows reads it from the window peer:
// Explorer's association for the executable is cached and can be stale, and an icon set on the
// peer is what the shell shows for a running application.
juce::Image appWindowIcon();
}
