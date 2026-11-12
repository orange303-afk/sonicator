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

juce::StringArray readCustomTypes(sqlite3* db)
{
    juce::StringArray names;
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT name FROM custom_types", -1, &statement, nullptr) != SQLITE_OK)
        return names;
    while (sqlite3_step(statement) == SQLITE_ROW)
        names.addIfNotAlreadyThere(juce::String::fromUTF8(reinterpret_cast<const char*>(sqlite3_column_text(statement, 0))).trim().toLowerCase());
    sqlite3_finalize(statement);
    return names;
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

    const auto customTypes = readCustomTypes(holder.handle);
    constexpr auto query =
        "SELECT f.file_hash, f.target_type, f.tags, f.origin, s.path"
        " FROM ml_feedback_samples f JOIN samples s ON s.id = f.sample_id"
        " WHERE f.origin IN ('user','approved')"
        " ORDER BY f.file_hash";
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(holder.handle, query, -1, &statement, nullptr) != SQLITE_OK)
    {
        error = "cannot read feedback rows: " + lastError(holder.handle);
        return rows;
    }

    while (sqlite3_step(statement) == SQLITE_ROW)
    {
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
    juce::String error;
    const auto rows = readGoldRows(database, error);
    if (error.isNotEmpty())
    {
        result.error = error;
        return result;
    }

    out.getParentDirectory().createDirectory();
    out.deleteFile();
    juce::FileOutputStream stream(out);
    if (! stream.openedOk())
    {
        result.error = "cannot write " + out.getFullPathName();
        return result;
    }
    std::unique_ptr<juce::FileOutputStream> pathStream;
    if (paths != juce::File())
    {
        paths.deleteFile();
        paths.getParentDirectory().createDirectory();
        pathStream = std::make_unique<juce::FileOutputStream>(paths);
        if (! pathStream->openedOk())
        {
            result.error = "cannot write " + paths.getFullPathName();
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
        stream.writeText(goldJsonLine(row, vector) + "\n", false, false, "\n");
        ++result.written;
        if (pathStream != nullptr)
        {
            juce::String line;
            line << "{\"recordId\":" << jsonString(row.recordId)
                 << ",\"path\":" << jsonString(row.file.getFullPathName()) << "}";
            pathStream->writeText(line + "\n", false, false, "\n");
        }
    }
    stream.flush();
    if (pathStream != nullptr) pathStream->flush();
    return result;
}

} // namespace sonicator
