#pragma once
#include <sqlite3.h>
#include <juce_core/juce_core.h>
#include <stdexcept>
namespace sonicator::detail {
inline void exec(sqlite3* db, const char* sql)
{
    if (sqlite3_exec(db, sql, nullptr, nullptr, nullptr) != SQLITE_OK)
        throw std::runtime_error(sqlite3_errmsg(db));
}
class Statement
{
public:
    Statement(sqlite3* d, const char* sql) : db(d)
    {
        if (sqlite3_prepare_v2(db, sql, -1, &s, nullptr) != SQLITE_OK)
            throw std::runtime_error(sqlite3_errmsg(db));
    }
    ~Statement() { sqlite3_finalize(s); }
    void text(int i, const juce::String& v) { check(sqlite3_bind_text(s, i, v.toRawUTF8(), -1, SQLITE_TRANSIENT)); }
    void integer(int i, int64_t v) { check(sqlite3_bind_int64(s, i, v)); }
    void real(int i, double v) { check(sqlite3_bind_double(s, i, v)); }
    bool step()
    {
        auto rc = sqlite3_step(s);
        if (rc != SQLITE_ROW && rc != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db));
        return rc == SQLITE_ROW;
    }
    juce::String text(int i) const { return juce::String::fromUTF8(reinterpret_cast<const char*>(sqlite3_column_text(s, i))); }
    int64_t integer(int i) const { return sqlite3_column_int64(s, i); }
    double real(int i) const { return sqlite3_column_double(s, i); }
private:
    void check(int rc) { if (rc != SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db)); }
    sqlite3* db;
    sqlite3_stmt* s = nullptr;
};
}
