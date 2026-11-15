#pragma once

namespace sonicator
{
// The catalog schema this build reads, writes and migrates. One number, one place: the repository,
// the compatibility check and the plug-in all compare against it.
inline constexpr int catalogSchemaVersion = 19;
} // namespace sonicator
