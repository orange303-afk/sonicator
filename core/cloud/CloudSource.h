#pragma once
#include "CloudModel.h"
#include "core/database/SampleRepository.h"
#include <functional>

namespace sonicator::cloud
{
// Reads every tagged file in one pass, plus its tags and the stored schema-2 vectors. This is the
// only catalog access the map makes: one batch, not a page of the table and not a query per file.
// `isCancelled` is checked between the queries and inside the per-file loop, so a superseded build
// gives the worker back within a file batch instead of reading a 50k-file catalog to the end.
CloudInput loadCloudInput(SampleRepository&, const std::function<bool()>& isCancelled = {});

// FNV-1a over exactly the fields the layout depends on: tag ids/colours/counts, assignments, the
// basic descriptor values, the stored v2 values and the algorithm version. A filter change never
// alters it; retagging, a real reanalysis or a new file does. The analysis timestamp stays out: it
// moved on every background reanalysis and rebuilt an identical map.
uint64_t fingerprintCloudInput(const CloudInput&);

std::array<float, 8> basicFeaturesOf(const SampleRecord&);
}
