#pragma once
#include <cstdint>
namespace sonicator {
// Why an analysed asset can or cannot be opened. `unknown` is not a verdict: legacy rows,
// missing files, cancellations and transient I/O failures stay visible, so nothing is guessed
// from error text or from a single failed attempt.
enum class AssetCompatibility : std::uint8_t
{
    unknown = 0,
    supported = 1,
    unsupported = 2, // recognised format/codec/layout this build does not decode
    invalid = 3      // the file itself is malformed or truncated
};
inline bool isIncompatible(AssetCompatibility compatibility)
{
    return compatibility == AssetCompatibility::unsupported
        || compatibility == AssetCompatibility::invalid;
}
inline AssetCompatibility assetCompatibilityFromInt(int value)
{
    switch (value)
    {
        case 1: return AssetCompatibility::supported;
        case 2: return AssetCompatibility::unsupported;
        case 3: return AssetCompatibility::invalid;
        default: return AssetCompatibility::unknown;
    }
}
} // namespace sonicator
