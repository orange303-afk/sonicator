#pragma once
#include "CloudModel.h"
#include "core/database/SampleRepository.h"

namespace sonicator::cloud
{
// Reads every tagged file in one pass, plus its tags and the stored schema-2 vectors. This is the
// only catalog access the map makes: one batch, not a page of the table and not a query per file.
CloudInput loadCloudInput(SampleRepository&);

// FNV-1a over exactly the fields the layout depends on: tag ids/colours/counts, assignments,
// descriptor and v2 availability, the v2 values themselves, analysis timestamps and the algorithm
// version. A filter change never alters it; retagging, reanalysis or a new file does.
uint64_t fingerprintCloudInput(const CloudInput&);

std::array<float, 8> basicFeaturesOf(const SampleRecord&);
}
