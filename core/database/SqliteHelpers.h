#pragma once
#include <sqlite3.h>
#include <juce_core/juce_core.h>
#include <chrono>
#include <stdexcept>
namespace sonicator::detail {
// A statement that takes longer than this is written to the log with its SQL, so a frozen window
// leaves a record of what the database was doing. SONICATOR_SQL_TRACE=1 also logs the start of
// every statement, which names the one that never finished.
constexpr double slowStatementMilliseconds = 2000.0;

inline juce::String shortSql(const char* sql)
{
    if (sql == nullptr) return {};
    return juce::String(sql).replaceCharacters("\n\r\t", "   ").substring(0, 220);
}

inline bool sqlTraceEnabled()
{
    static const bool enabled =
        juce::SystemStats::getEnvironmentVariable("SONICATOR_SQL_TRACE", {}).isNotEmpty();
    return enabled;
}

inline void logStatementStart(const char* sql)
{
    if (sqlTraceEnabled()) juce::Logger::writeToLog("sql> " + shortSql(sql));
}

inline void logSlowStatement(const char* sql, double milliseconds)
{
    if (milliseconds < slowStatementMilliseconds) return;
    juce::Logger::writeToLog("sql: " + juce::String(milliseconds / 1000.0, 2) + " s "
                             + shortSql(sql));
}

inline void exec(sqlite3* db, const char* sql)
{
    logStatementStart(sql);
    const auto started = std::chrono::steady_clock::now();
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, nullptr);
    logSlowStatement(sql, std::chrono::duration<double, std::milli>(
                               std::chrono::steady_clock::now() - started).count());
    if (rc != SQLITE_OK)
        throw std::runtime_error(sqlite3_errmsg(db));
}
class Statement
{
public:
    Statement(sqlite3* d, const char* sql) : db(d), sqlText(sql),
        started(std::chrono::steady_clock::now())
    {
        logStatementStart(sql);
        if (sqlite3_prepare_v2(db, sql, -1, &s, nullptr) != SQLITE_OK)
            throw std::runtime_error(sqlite3_errmsg(db));
    }
    ~Statement()
    {
        logSlowStatement(sqlText, std::chrono::duration<double, std::milli>(
                                      std::chrono::steady_clock::now() - started).count());
        sqlite3_finalize(s);
    }
    void text(int i, const juce::String& v) { check(sqlite3_bind_text(s, i, v.toRawUTF8(), -1, SQLITE_TRANSIENT)); }
    void integer(int i, int64_t v) { check(sqlite3_bind_int64(s, i, v)); }
    void real(int i, double v) { check(sqlite3_bind_double(s, i, v)); }
    void blob(int i, const void* data, int size) { check(sqlite3_bind_blob(s, i, data, size, SQLITE_TRANSIENT)); }
    void reset() { sqlite3_reset(s); sqlite3_clear_bindings(s); }
    bool step()
    {
        auto rc = sqlite3_step(s);
        if (rc != SQLITE_ROW && rc != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db));
        return rc == SQLITE_ROW;
    }
    juce::String text(int i) const { return juce::String::fromUTF8(reinterpret_cast<const char*>(sqlite3_column_text(s, i))); }
    int64_t integer(int i) const { return sqlite3_column_int64(s, i); }
    double real(int i) const { return sqlite3_column_double(s, i); }
    const void* blob(int i) const { return sqlite3_column_blob(s, i); }
    int bytes(int i) const { return sqlite3_column_bytes(s, i); }
private:
    void check(int rc) { if (rc != SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db)); }
    sqlite3* db;
    const char* sqlText = nullptr;
    std::chrono::steady_clock::time_point started;
    sqlite3_stmt* s = nullptr;
};
}
