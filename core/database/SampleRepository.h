#pragma once
#include "Database.h"
#include "SampleRecord.h"
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
    int countHash(const juce::String&) const;
    void addRoot(const juce::String&);
    juce::StringArray roots() const;
private:
    Database& db;
};
}
