#pragma once
#include "DiscBrowserService.h"
#include "core/database/SampleRepository.h"
#include <functional>
#include <vector>
namespace sonicator {
struct DiscIndexSummary
{
    int imagesTried = 0, imagesIndexed = 0, imagesSkipped = 0, imagesFailed = 0, entries = 0;
    juce::String lastError;
};
// ISO/IMG/BIN/CUE images that may contain a readable filesystem. MDX is deliberately excluded.
bool isDiscContentFormat(const juce::File&);
// A .bin that has a sibling .cue is represented by the cue, so its contents are indexed once.
bool isDiscContentCandidate(const juce::File&);
// Walks playable audio entries of each image and stores them as synthetic catalog rows. Never
// extracts files and never decodes PCM. An unchanged image (same size and mtime) is skipped.
DiscIndexSummary indexDiscContents(SampleRepository&, const std::vector<juce::File>& images,
                                   CancelCheck cancel,
                                   const std::function<void(int, int, const juce::String&)>& progress = {});
}
