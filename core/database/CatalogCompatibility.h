#pragma once
#include "CatalogSchema.h"
#include <juce_core/juce_core.h>

namespace sonicator
{
// What this build can do with a catalog owned by a possibly different SONICATOR build.
//
// The application and the DAW plug-in share one SQLite file, and the two are not guaranteed to be
// installed together: a user may run an older application with a newer plug-in, or the other way
// round. Only the application may migrate the schema, because only the application ships every
// writer that knows the new layout. The plug-in reads the version, refuses to change the file and
// says what to update, so opening a plug-in never makes the library unreadable to the application.
struct CatalogCompatibility
{
    enum class State
    {
        ready,        // same schema version as this build
        missing,      // no catalog file at the path
        notCatalog,   // a file, but not a SONICATOR catalog
        older,        // written by an older SONICATOR (the application updates it)
        newer,        // written by a newer SONICATOR (this build must be updated)
        unreadable    // the file cannot be opened (permissions, corruption)
    };

    State state = State::missing;
    int version = 0;
    int expected = catalogSchemaVersion;
    // Ready to show to a user: empty when the catalog can be opened.
    juce::String message;

    bool readable() const { return state == State::ready; }
};

// Reads the schema version through a read-only connection, so the check never creates, migrates or
// locks the catalog for writing.
CatalogCompatibility inspectCatalog(const juce::File& file);

// The one wording for each refusal, shared by the pre-open check and the repository guard so a
// user sees the same sentence wherever the catalog is rejected.
juce::String catalogOlderThanBuildMessage();
juce::String catalogNewerThanBuildMessage();
} // namespace sonicator
