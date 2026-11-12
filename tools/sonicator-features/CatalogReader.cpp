#include "CatalogReader.h"
#include "core/dsp/AnalysisAudio.h"
#include <sqlite3.h>

namespace sonicator {
namespace {

struct DatabaseHandle
{
    sqlite3* handle = nullptr;
    ~DatabaseHandle()
    {
        if (handle != nullptr) sqlite3_close(handle);
    }
};

juce::String lastError(sqlite3* db)
{
    return db != nullptr ? juce::String(sqlite3_errmsg(db)) : juce::String("database is not open");
}

bool openReadOnly(const juce::File& file, DatabaseHandle& holder, juce::String& error)
{
    if (! file.existsAsFile())
    {
        error = "no such catalog: " + file.getFullPathName();
        return false;
    }
    const auto path = file.getFullPathName().toRawUTF8();
    if (sqlite3_open_v2(path, &holder.handle, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK)
    {
        error = "cannot open catalog: " + lastError(holder.handle);
        return false;
    }
    sqlite3_busy_timeout(holder.handle, 5000);
    return true;
}

// A failure here would silently mark every custom type as standard, so it is reported like any other
// read error instead of returning an empty list.
bool readCustomTypes(sqlite3* db, juce::StringArray& names, juce::String& error)
{
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT name FROM custom_types", -1, &statement, nullptr) != SQLITE_OK)
    {
        error = "cannot read custom types: " + lastError(db);
        return false;
    }
    while (true)
    {
        const auto step = sqlite3_step(statement);
        if (step == SQLITE_ROW)
        {
            names.addIfNotAlreadyThere(juce::String::fromUTF8(
                reinterpret_cast<const char*>(sqlite3_column_text(statement, 0))).trim().toLowerCase());
        }
        else if (step == SQLITE_DONE) break;
        else
        {
            error = "cannot read custom types: " + lastError(db);
            sqlite3_finalize(statement);
            return false;
        }
    }
    sqlite3_finalize(statement);
    return true;
}

juce::StringArray tagList(const juce::String& text)
{
    juce::StringArray tags;
    for (const auto& tag : juce::StringArray::fromTokens(text, ",", ""))
        if (tag.trim().isNotEmpty()) tags.add(tag.trim());
    return tags;
}

juce::String jsonString(const juce::String& text)
{
    return juce::JSON::toString(juce::var(text), false);
}

} // namespace

std::vector<GoldRow> readGoldRows(const juce::File& database, juce::String& error)
{
    std::vector<GoldRow> rows;
    DatabaseHandle holder;
    if (! openReadOnly(database, holder, error)) return rows;

    juce::StringArray customTypes;
    if (! readCustomTypes(holder.handle, customTypes, error)) return rows;
    constexpr auto query =
        "SELECT f.file_hash, f.target_type, f.tags, f.origin, s.path"
        " FROM ml_feedback_samples f JOIN samples s ON s.id = f.sample_id"
        " WHERE f.origin IN ('user','approved')"
        " ORDER BY f.file_hash, f.sample_id";
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(holder.handle, query, -1, &statement, nullptr) != SQLITE_OK)
    {
        error = "cannot read feedback rows: " + lastError(holder.handle);
        return rows;
    }

    while (true)
    {
        const auto step = sqlite3_step(statement);
        if (step == SQLITE_DONE) break;
        if (step != SQLITE_ROW)
        {
            // A busy or malformed catalog must not turn into a short gold file that looks complete.
            error = "cannot read feedback rows: " + lastError(holder.handle);
            sqlite3_finalize(statement);
            return {};
        }
        const auto text = [&](int column)
        {
            const auto* value = sqlite3_column_text(statement, column);
            return value != nullptr ? juce::String::fromUTF8(reinterpret_cast<const char*>(value)) : juce::String();
        };
        GoldRow row;
        const auto hash = text(0);
        row.type = text(1).trim().toLowerCase();
        row.tags = text(2);
        row.origin = text(3).toLowerCase();
        row.file = juce::File(text(4));
        row.typeIsCustom = row.type.isNotEmpty() && customTypes.contains(row.type);
        // A stable identity that carries no path: the first 16 hex characters of sha256(file hash).
        row.recordId = juce::SHA256(hash.toRawUTF8(), static_cast<int>(hash.getNumBytesAsUTF8()))
                           .toHexString()
                           .substring(0, 16);
        rows.push_back(std::move(row));
    }
    sqlite3_finalize(statement);
    return rows;
}

juce::String goldJsonLine(const GoldRow& row, const FeatureVectorV2& vector)
{
    juce::String line;
    line << "{\"recordId\":" << jsonString(row.recordId)
         << ",\"type\":" << jsonString(row.type)
         << ",\"typeIsCustom\":" << (row.typeIsCustom ? "true" : "false")
         << ",\"tags\":[";
    const auto tags = tagList(row.tags);
    for (int i = 0; i < tags.size(); ++i)
    {
        if (i > 0) line << ",";
        line << jsonString(tags[i]);
    }
    line << "],\"origin\":" << jsonString(row.origin)
         << ",\"featureSchema\":" << FeatureExtractorV2::version()
         << ",\"dimensions\":" << FeatureExtractorV2::dimensions()
         << ",\"inBandEnergyShare\":" << juce::String(vector.inBandShare, 3)
         << ",\"features\":[";
    for (size_t i = 0; i < vector.values.size(); ++i)
    {
        if (i > 0) line << ",";
        line << juce::String(vector.values[i], 3);
    }
    line << "]}";
    return line;
}

CatalogResult writeCatalogGold(const juce::File& database, const juce::File& out, const juce::File& paths)
{
    CatalogResult result;
    const auto databasePath = database.getFullPathName();
    const auto outPath = out.getFullPathName();
    const auto pathsPath = paths == juce::File() ? juce::String() : paths.getFullPathName();
    if (outPath == databasePath || pathsPath == databasePath || (pathsPath.isNotEmpty() && pathsPath == outPath))
    {
        result.error = "the output must not be the catalog or the paths file";
        return result;
    }

    juce::String error;
    const auto rows = readGoldRows(database, error);
    if (error.isNotEmpty())
    {
        result.error = error;
        return result;
    }
    if (rows.empty())
    {
        result.error = "the catalog has no confirmed rows";
        return result;
    }

    // Write a sibling temporary file and replace the old gold only on success: a disconnected drive or
    // a full disk must never destroy the previous good sample.
    out.getParentDirectory().createDirectory();
    const auto temporary = out.getSiblingFile("." + out.getFileName() + ".tmp");
    temporary.deleteFile();
    juce::File pathsTemporary;
    if (pathsPath.isNotEmpty())
    {
        paths.getParentDirectory().createDirectory();
        pathsTemporary = paths.getSiblingFile("." + paths.getFileName() + ".tmp");
        pathsTemporary.deleteFile();
    }

    {
        juce::FileOutputStream stream(temporary);
        if (! stream.openedOk())
        {
            result.error = "cannot write " + temporary.getFullPathName();
            return result;
        }
        std::unique_ptr<juce::FileOutputStream> pathStream;
        if (pathsPath.isNotEmpty())
        {
            pathStream = std::make_unique<juce::FileOutputStream>(pathsTemporary);
            if (! pathStream->openedOk())
            {
                result.error = "cannot write " + pathsTemporary.getFullPathName();
                temporary.deleteFile();
                return result;
            }
        }

        for (const auto& row : rows)
        {
            if (! row.file.existsAsFile())
            {
                ++result.missingFile;
                continue;
            }
            AnalysisAudioOptions options;
            options.maxSeconds = 10.0;
            const auto audio = readAnalysisAudio(row.file, options, {});
            if (! audio.ok())
            {
                ++result.unreadable;
                continue;
            }
            const auto vector = FeatureExtractorV2::extract(audio.buffer, audio.sampleRate);
            if (! vector.ok())
            {
                ++result.unreadable;
                continue;
            }
            if (! stream.writeText(goldJsonLine(row, vector) + "\n", false, false, "\n"))
            {
                result.error = "cannot write " + temporary.getFullPathName();
                break;
            }
            ++result.written;
            if (pathStream != nullptr)
            {
                juce::String line;
                line << "{\"recordId\":" << jsonString(row.recordId)
                     << ",\"path\":" << jsonString(row.file.getFullPathName()) << "}";
                if (! pathStream->writeText(line + "\n", false, false, "\n"))
                {
                    result.error = "cannot write " + pathsTemporary.getFullPathName();
                    break;
                }
            }
        }
        stream.flush();
        if (pathStream != nullptr) pathStream->flush();
        if (result.error.isEmpty() && stream.getStatus().failed())
            result.error = "cannot write " + temporary.getFullPathName();
        if (result.error.isEmpty() && pathStream != nullptr && pathStream->getStatus().failed())
            result.error = "cannot write " + pathsTemporary.getFullPathName();
    }

    if (result.error.isEmpty() && result.written == 0)
        result.error = "no rows written: every confirmed file is missing or unreadable";
    if (result.error.isNotEmpty())
    {
        temporary.deleteFile();
        if (pathsTemporary != juce::File()) pathsTemporary.deleteFile();
        return result;
    }
    if (! temporary.replaceFileIn(out))
    {
        result.error = "cannot replace " + outPath;
        temporary.deleteFile();
        if (pathsTemporary != juce::File()) pathsTemporary.deleteFile();
        return result;
    }
    if (pathsTemporary != juce::File() && ! pathsTemporary.replaceFileIn(paths))
    {
        result.error = "cannot replace " + pathsPath;
        pathsTemporary.deleteFile();
    }
    return result;
}

} // namespace sonicator
