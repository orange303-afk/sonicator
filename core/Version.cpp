#include "core/Version.h"

#ifndef SONICATOR_VERSION
// A build outside the CMake project (an ad-hoc smoke target) still reports a valid version.
#define SONICATOR_VERSION "0.0.0"
#endif

namespace sonicator
{
const char* version() { return SONICATOR_VERSION; }
} // namespace sonicator
