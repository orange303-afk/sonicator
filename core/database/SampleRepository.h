#pragma once
#include "Database.h"
#include "LibraryQuery.h"
#include "SampleRecord.h"
#include <map>
#include <optional>
#include <vector>
namespace sonicator {
struct PathEntry
{
    int64_t id = 0;
    juce::String path;
    bool missing = false;
};
struct RootRelocation
{
    juce::String oldRoot, newRoot;
};
struct RelocationResult
{
    int64_t movedFiles = 0, conflicts = 0;
};
class SampleRepository
{
public:
    explicit SampleRepository(Database& d) : db(d) {}
    void createTables();
    int64_t upsert(const SampleRecord&);
    std::optional<SampleRecord> findByPath(const juce::String&) const;
    std::vector<SampleRecord> page(int limit = 10000, int offset = 0) const;
    std::vector<PathEntry> pathsAfterId(int64_t lastId, int limit = 5000) const;
    void setMissing(const std::vector<int64_t>& ids);
    int count() const;
    int countInFolder(const juce::String& folder) const;
    std::vector<SampleRecord> pageInFolder(const juce::String& folder, int limit = 10000, int offset = 0) const;
    // Whole-catalog query. Text, folder and tags are combined with AND; the returned total is the
    // full match count, independent of the requested page.
    LibraryPage query(const LibraryFilter&, int limit = 10000, int offset = 0) const;
    // Every matching ID in the same order as query(). Used by the virtual all-on-one-page view.
    std::vector<int64_t> queryIds(const LibraryFilter&) const;
    std::vector<SampleRecord> recordsByIds(const std::vector<int64_t>&) const;
    int countHash(const juce::String&) const;
    void addRoot(const juce::String&);
    juce::StringArray roots() const;
    int countUnderRoot(const juce::String& root) const;
    // Rewrites every sample path below each old root to the matching new root, preserving the
    // relative structure. Runs in one transaction and keeps ids, tags, stars and analysis data.
    RelocationResult relocateRoots(const std::vector<RootRelocation>&);
    // Consistent SQLite backup through the backup API (WAL-safe). The destination must be new.
    void backupTo(const juce::File& destination);
    // True only for an existing SONICATOR catalog (samples and roots tables with a schema version).
    static bool isCatalog(const juce::File& file);
    std::vector<Tag> tags() const;
    std::vector<Tag> tagsForSample(int64_t sampleId) const;
    std::map<int64_t, std::vector<Tag>> tagsForSamples(const std::vector<int64_t>& sampleIds) const;
    int64_t createTag(const juce::String& name, uint32_t colour);
    void setTagColour(int64_t tagId, uint32_t colour);
    bool renameTag(int64_t tagId, const juce::String& name);
    void deleteTag(int64_t tagId);
    void setSampleTag(int64_t sampleId, int64_t tagId, bool assigned);
    void setSampleTag(const std::vector<int64_t>& sampleIds, int64_t tagId, bool assigned);
    void setStarred(const std::vector<int64_t>& sampleIds, bool starred);
    // Rebuilds the byte-identical duplicate groups (same full SHA-256 and size).
    void refreshDuplicates();
    std::map<int64_t,int> duplicateCounts(const std::vector<int64_t>& sampleIds) const;
    struct DuplicateInfo { int groupSize = 0; int64_t representativeId = 0; juce::String representativePath; };
    std::optional<DuplicateInfo> duplicateInfo(int64_t sampleId) const;
    std::vector<SampleRecord> duplicateMembers(int64_t sampleId) const;
    // Lower-cased name + newline + lower-cased path, used by whole-catalog text search.
    static juce::String searchKey(const juce::String& name, const juce::String& path);
private:
    Database& db;
};
} // namespace sonicator
