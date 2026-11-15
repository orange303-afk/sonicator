#include "CatalogCompatibility.h"
#include <sqlite3.h>

namespace sonicator
{
juce::String catalogOlderThanBuildMessage()
{
    return "This library belongs to an older SONICATOR. Open the SONICATOR app once so it can "
           "update the library, then reopen this window.";
}

juce::String catalogNewerThanBuildMessage()
{
    return "This library was updated by a newer SONICATOR. Update SONICATOR and the plug-in, then "
           "reopen this window.";
}

CatalogCompatibility inspectCatalog(const juce::File& file)
{
    CatalogCompatibility result;
    if (file.getFullPathName().isEmpty() || ! file.existsAsFile())
    {
        result.state = CatalogCompatibility::State::missing;
        result.message = "SONICATOR reads the library the application owns.\n\n"
                         "Open the SONICATOR app once so it creates its catalog, then reopen this "
                         "window.";
        return result;
    }

    // Read-only: a plug-in must be able to look at the version of a catalog it must not change.
    sqlite3* handle = nullptr;
    if (sqlite3_open_v2(file.getFullPathName().toRawUTF8(), &handle,
                       SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK)
    {
        result.state = CatalogCompatibility::State::unreadable;
        result.message = juce::String("Cannot open the SONICATOR library: ")
            + (handle != nullptr ? sqlite3_errmsg(handle) : "unknown error");
        if (handle != nullptr) sqlite3_close(handle);
        return result;
    }
    sqlite3_busy_timeout(handle, 5000);

    sqlite3_stmt* statement = nullptr;
    constexpr auto sql =
        "SELECT (SELECT COUNT(*) FROM sqlite_master WHERE type='table'"
        " AND name IN ('samples','roots')), (SELECT user_version FROM pragma_user_version)";
    if (sqlite3_prepare_v2(handle, sql, -1, &statement, nullptr) != SQLITE_OK
        || sqlite3_step(statement) != SQLITE_ROW)
    {
        // A readable file that SQLite cannot parse is somebody else's file, not a broken catalog.
        const int error = handle != nullptr ? sqlite3_errcode(handle) : SQLITE_ERROR;
        const auto detail = handle != nullptr ? juce::String(sqlite3_errmsg(handle)) : juce::String();
        if (statement != nullptr) sqlite3_finalize(statement);
        result.state = error == SQLITE_NOTADB ? CatalogCompatibility::State::notCatalog
                                              : CatalogCompatibility::State::unreadable;
        result.message = result.state == CatalogCompatibility::State::notCatalog
            ? juce::String("That file is not a SONICATOR library.")
            : juce::String("Cannot read the SONICATOR library: ") + detail;
        sqlite3_close(handle);
        return result;
    }
    const int tables = sqlite3_column_int(statement, 0);
    result.version = sqlite3_column_int(statement, 1);
    sqlite3_finalize(statement);
    sqlite3_close(handle);

    if (tables != 2 || result.version < 1)
    {
        result.state = CatalogCompatibility::State::notCatalog;
        result.message = "That file is not a SONICATOR library.";
    }
    else if (result.version > catalogSchemaVersion)
    {
        result.state = CatalogCompatibility::State::newer;
        result.message = catalogNewerThanBuildMessage();
    }
    else if (result.version < catalogSchemaVersion)
    {
        result.state = CatalogCompatibility::State::older;
        result.message = catalogOlderThanBuildMessage();
    }
    else
    {
        result.state = CatalogCompatibility::State::ready;
    }
    return result;
}
} // namespace sonicator
