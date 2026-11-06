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
    int countHash(const juce::String&) const;
    void addRoot(const juce::String&);
    juce::StringArray roots() const;
    std::vector<Tag> tags() const;
    std::vector<Tag> tagsForSample(int64_t sampleId) const;
    std::map<int64_t, std::vector<Tag>> tagsForSamples(const std::vector<int64_t>& sampleIds) const;
    int64_t createTag(const juce::String& name, uint32_t colour);
    void setTagColour(int64_t tagId, uint32_t colour);
    bool renameTag(int64_t tagId, const juce::String& name);
    void deleteTag(int64_t tagId);
    void setSampleTag(int64_t sampleId, int64_t tagId, bool assigned);
    // Lower-cased name + newline + lower-cased path, used by whole-catalog text search.
    static juce::String searchKey(const juce::String& name, const juce::String& path);
private:
    Database& db;
};
} // namespace sonicator
