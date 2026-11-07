#pragma once
#include "core/database/SampleRecord.h"
#include "core/scanner/FileScanner.h"
namespace sonicator {
// Builds a catalog row for a container that is not decoded as audio (REX2, SoundFont 2).
// The row has identity and the fields that can be read without DSP; version is
// metadataOnlyVersion so rescan does not expect measured Phase 1 features.
SampleRecord analyzeMetadataAsset(const juce::File&, CancelCheck = {});
} // namespace sonicator
