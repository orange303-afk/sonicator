#include "Database.h"
#include "SqliteHelpers.h"
namespace sonicator {
Database::Database(const juce::File& f)
{
    if (! f.getParentDirectory().createDirectory())
        throw std::runtime_error("Cannot create database directory");
    if (sqlite3_open_v2(f.getFullPathName().toRawUTF8(), &handle,
                       SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK)
    {
        juce::String error = handle ? sqlite3_errmsg(handle) : "Cannot open database";
        sqlite3_close(handle); handle = nullptr;
        throw std::runtime_error(error.toStdString());
    }
    try
    {
        sqlite3_busy_timeout(handle, 5000);
        detail::exec(handle, "PRAGMA journal_mode=WAL; PRAGMA foreign_keys=ON;");
    }
    catch (...) { sqlite3_close(handle); handle = nullptr; throw; }
}
Database::~Database() { sqlite3_close(handle); }
}
