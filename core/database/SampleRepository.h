#pragma once
#include "Database.h"
#include "SampleRecord.h"
#include <optional>
#include <vector>
namespace sonicator {
class SampleRepository
{
public:
    explicit SampleRepository(Database& d) : db(d) {}
    void createTables();
    int64_t upsert(const SampleRecord&);
    std::optional<SampleRecord> findByPath(const juce::String&) const;
    std::vector<SampleRecord> page(int limit = 10000, int offset = 0) const;
    int count() const;
    int countHash(const juce::String&) const;
    void addRoot(const juce::String&);
    juce::StringArray roots() const;
private:
    Database& db;
};
}
