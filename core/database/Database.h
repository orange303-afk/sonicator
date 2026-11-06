#pragma once
#include <juce_core/juce_core.h>
#include <mutex>
struct sqlite3;
namespace sonicator {
class SampleRepository;
class Database
{
public:
    explicit Database(const juce::File&);
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
private:
    friend class SampleRepository;
    sqlite3* handle = nullptr;
    mutable std::mutex mutex;
};
}
