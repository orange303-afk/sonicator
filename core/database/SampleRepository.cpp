#include "SampleRepository.h"
#include "SearchSyntax.h"
#include "core/smart/KeyNames.h"
#include "PathRelocation.h"
#include "SqliteHelpers.h"
#include <algorithm>
#include <variant>
namespace sonicator {
namespace {
constexpr auto columns = "id,path,name,hash,format,error,size,modified,analyzed,duration,rate,bits,channels,version,peak,rms,centroid,rolloff,flatness,missing,starred";
constexpr int schemaVersion = 6;
SampleRecord read(const detail::Statement& s)
{
    SampleRecord r;
    r.id = s.integer(0); r.filePath = s.text(1); r.fileName = s.text(2);
    r.fileHash = s.text(3); r.format = s.text(4); r.error = s.text(5);
    r.fileSize = s.integer(6); r.dateModified = s.integer(7); r.dateAnalyzed = s.integer(8);
    r.duration = s.real(9); r.sampleRate = s.real(10); r.bitsPerSample = (int) s.integer(11);
    r.numChannels = (int) s.integer(12); r.version = (int) s.integer(13);
    r.peakLevel = s.real(14); r.rmsLevel = s.real(15); r.spectralCentroid = s.real(16);
    r.spectralRolloff = s.real(17); r.spectralFlatness = s.real(18); r.missing = s.integer(19) != 0;
    r.starred = s.integer(20) != 0;
    return r;
}
struct StandardTag { const char* name; uint32_t colour; };
constexpr StandardTag standardTags[] = {
    { "Kick", 0xffe05a5a }, { "Snare", 0xffe08a45 }, { "Clap", 0xffe0b84a },
    { "Hat", 0xffd7dd6a }, { "Percussion", 0xff8fd06a }, { "Bass", 0xff4fb8c8 },
    { "Sub", 0xff4f8fd0 }, { "Lead", 0xff7a6aea }, { "Pad", 0xffa96ae0 },
    { "Keys", 0xffd06ab8 }, { "Guitar", 0xffd06a7a }, { "Vocal", 0xff6ad0a0 },
    { "FX", 0xff8a8f9c }, { "Loop", 0xff9ad06a }, { "One-shot", 0xffc8a06a }
};
std::pair<juce::String,juce::String> folderRange(const juce::String& folder)
{
    auto prefix = juce::File(folder).getFullPathName();
    if (! prefix.endsWithChar(juce::File::getSeparatorChar())) prefix += juce::File::getSeparatorString();
    return {prefix, prefix.dropLastCharacters(1) + juce::String::charToString(juce::File::getSeparatorChar()+1)};
}
juce::String escapeLike(const juce::String& value)
{
    juce::String out;
    for (auto c : value)
    {
        if (c == '\\' || c == '%' || c == '_') out += juce::String::charToString('\\');
        out += juce::String::charToString(c);
    }
    return out;
}
// C-locale towlower folds ASCII only, so Cyrillic and Greek names would not match a lower-case
// query. Cover the case-bearing ranges that sample libraries actually use; scripts without case
// (CJK, Arabic, Hebrew) pass through unchanged.
juce::juce_wchar foldCase(juce::juce_wchar c)
{
    if (c >= 'A' && c <= 'Z') return c + 32;
    if (c >= 0x00C0 && c <= 0x00DE && c != 0x00D7) return c + 32;
    if (c == 0x0178) return 0x00FF;
    if (c >= 0x0391 && c <= 0x03A9 && c != 0x03A2) return c + 32;
    switch (c)
    {
        case 0x0386: return 0x03AC; case 0x0388: return 0x03AD; case 0x0389: return 0x03AE;
        case 0x038A: return 0x03AF; case 0x038C: return 0x03CC; case 0x038E: return 0x03CD;
        case 0x038F: return 0x03CE; default: break;
    }
    if (c >= 0x0410 && c <= 0x042F) return c + 32;
    if (c >= 0x0400 && c <= 0x040F) return c + 80;
    if (c >= 0x0460 && c <= 0x0481 && (c & 1) == 0) return c + 1;
    if (c >= 0x048A && c <= 0x04BF && (c & 1) == 0) return c + 1;
    if (c >= 0x04C1 && c <= 0x04CE && (c & 1) == 1) return c + 1;
    if (c >= 0x04D0 && c <= 0x052F && (c & 1) == 0) return c + 1;
    if (c >= 0x03DA && c <= 0x03EF && (c & 1) == 0) return c + 1;
    if (c >= 0x0100 && c <= 0x0177)
    {
        if (c == 0x0130 || c == 0x0131) return 'i';
        if (c == 0x0138 || c == 0x0149) return c;
        const bool upper = (c <= 0x0137) ? ((c & 1) == 0) : ((c <= 0x0148) ? ((c & 1) == 1) : ((c & 1) == 0));
        return upper ? c + 1 : c;
    }
    return c;
}
juce::String foldForSearch(const juce::String& text)
{
    juce::String out;
    out.preallocateBytes(text.getNumBytesAsUTF8());
    for (auto c : text) out += juce::String::charToString(foldCase(c));
    return out;
}
juce::String orderBy(LibrarySort sort, bool ascending)
{
    juce::String column;
    switch (sort)
    {
        case LibrarySort::Format:   column = "samples.format COLLATE NOCASE"; break;
        case LibrarySort::Duration: column = "samples.duration"; break;
        case LibrarySort::Rate:     column = "samples.sampleRate"; break;
        case LibrarySort::Channels: column = "samples.numChannels"; break;
        case LibrarySort::Status:   column = "samples.error COLLATE NOCASE"; break;
        case LibrarySort::Name:
        default:                    column = "samples.name COLLATE NOCASE"; break;
    }
    return column + (ascending ? " ASC" : " DESC") + ", samples.id ASC";
}
struct FilterSql
{
    juce::String where = " WHERE 1";
    std::vector<std::variant<juce::String, int64_t>> params;
};
FilterSql buildFilterSql(const LibraryFilter& filter)
{
    FilterSql sql;
    auto addText = [&](const juce::String& text, juce::String value)
    {
        sql.where += text; sql.params.emplace_back(std::move(value));
    };
    auto addInt = [&](const juce::String& text, int64_t value)
    {
        sql.where += text; sql.params.emplace_back(value);
    };
    for (const auto& term : parseSearchTerms(filter.text))
    {
        auto like = [&](const juce::String& value) { return "%" + escapeLike(foldForSearch(value)) + "%"; };
        switch (term.kind)
        {
            case SearchKind::Text:
                addText(" AND samples.search_key LIKE ? ESCAPE '\\'", like(term.text));
                break;
            case SearchKind::Glob:
                addText(" AND samples.name LIKE ? ESCAPE '\\'", globToLike(term.text));
                break;
            case SearchKind::Format:
            case SearchKind::GlobOr:
            {
                juce::String condition;
                for (const auto& value : term.values)
                {
                    if (condition.isNotEmpty()) condition += " OR ";
                    if (term.kind == SearchKind::Format)
                        condition += "(samples.format = ? COLLATE NOCASE OR samples.name LIKE ? ESCAPE '\\')";
                    else
                        condition += "samples.name LIKE ? ESCAPE '\\'";
                }
                if (condition.isEmpty()) break;
                sql.where += " AND (" + condition + ")";
                for (const auto& value : term.values)
                {
                    if (term.kind == SearchKind::Format) sql.params.emplace_back(value);
                    sql.params.emplace_back(term.kind == SearchKind::Format
                                            ? "%." + value : globToLike(value));
                }
                break;
            }
            case SearchKind::TypeOnly:
                addText(" AND EXISTS(SELECT 1 FROM smart_results sr WHERE sr.sample_id=samples.id"
                        " AND sr.type=? COLLATE NOCASE)", term.text);
                break;
            case SearchKind::TypeOrText:
                sql.where += " AND (EXISTS(SELECT 1 FROM smart_results sr WHERE sr.sample_id=samples.id"
                             " AND sr.type=? COLLATE NOCASE) OR samples.search_key LIKE ? ESCAPE '\\')";
                sql.params.emplace_back(term.text);
                sql.params.emplace_back(like(term.text));
                break;
            case SearchKind::KeyOnly:
            case SearchKind::KeyOrText:
            {
                juce::String condition = "EXISTS(SELECT 1 FROM smart_results sr WHERE sr.sample_id=samples.id"
                    " AND sr.key_tonic=? AND sr.key_state IN ('resolved','conflicting','overridden')";
                if (term.mode.isNotEmpty()) condition += " AND sr.key_mode=?";
                condition += ")";
                if (term.kind == SearchKind::KeyOrText)
                    condition += " OR samples.search_key LIKE ? ESCAPE '\\'";
                sql.where += " AND (" + condition + ")";
                sql.params.emplace_back((int64_t) term.tonic);
                if (term.mode.isNotEmpty()) sql.params.emplace_back(term.mode);
                if (term.kind == SearchKind::KeyOrText) sql.params.emplace_back(like(term.text));
                break;
            }
            case SearchKind::NoteOnly:
                sql.where += " AND EXISTS(SELECT 1 FROM smart_results sr WHERE sr.sample_id=samples.id"
                             " AND sr.root_note=? AND sr.root_state IN ('resolved','conflicting','overridden'))";
                sql.params.emplace_back((int64_t) term.midi);
                break;
        }
    }
    if (filter.folder.isNotEmpty())
    {
        auto [lower, upper] = folderRange(filter.folder);
        addText(" AND samples.path>=?", lower);
        addText(" AND samples.path<?", upper);
    }
    if (! filter.includeTags.empty())
    {
        if (! filter.matchAnyTag)
        {
            for (auto id : filter.includeTags)
                addInt(" AND EXISTS(SELECT 1 FROM sample_tags st WHERE st.sample_id=samples.id AND st.tag_id=?)", id);
        }
        else
        {
            juce::String placeholders;
            for (size_t i = 0; i < filter.includeTags.size(); ++i)
            {
                if (i != 0) placeholders += ",";
                placeholders += "?";
                sql.params.emplace_back(filter.includeTags[i]);
            }
            sql.where += " AND EXISTS(SELECT 1 FROM sample_tags st WHERE st.sample_id=samples.id AND st.tag_id IN ("
                + placeholders + "))";
        }
    }
    for (auto id : filter.excludeTags)
        addInt(" AND NOT EXISTS(SELECT 1 FROM sample_tags st WHERE st.sample_id=samples.id AND st.tag_id=?)", id);
    if (filter.starredOnly) sql.where += " AND samples.starred=1";
    // Metadata-only containers have no decoded peak, so "Hide empty" must not hide them.
    if (filter.hideEmpty) sql.where += " AND (samples.peak>=0.001 OR samples.version=-1)";
    if (filter.hideDuplicates)
        sql.where += " AND NOT EXISTS(SELECT 1 FROM duplicates d WHERE d.sample_id=samples.id AND d.is_representative=0)";
    if (filter.duplicateHash.isNotEmpty())
        addText(" AND samples.hash=?", filter.duplicateHash);
    return sql;
}
int bindFilter(detail::Statement& statement, const FilterSql& sql)
{
    int index = 1;
    for (const auto& param : sql.params)
    {
        if (const auto* text = std::get_if<juce::String>(&param)) statement.text(index, *text);
        else statement.integer(index, std::get<int64_t>(param));
        ++index;
    }
    return index;
}
Tag readTag(const detail::Statement& s, int offset, int countColumn = -1)
{
    Tag tag;
    tag.id = s.integer(offset); tag.name = s.text(offset + 1);
    tag.colour = (uint32_t) s.integer(offset + 2); tag.builtin = s.integer(offset + 3) != 0;
    if (countColumn >= 0) tag.sampleCount = (int) s.integer(countColumn);
    return tag;
}
void backupDatabase(sqlite3* source, const juce::File& destination)
{
    sqlite3* out = nullptr;
    if (sqlite3_open_v2(destination.getFullPathName().toRawUTF8(), &out,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK)
    {
        const juce::String error = out != nullptr ? juce::String(sqlite3_errmsg(out)) : juce::String("cannot open destination");
        if (out != nullptr) sqlite3_close(out);
        throw std::runtime_error("Cannot create database backup: " + error.toStdString());
    }
    sqlite3_busy_timeout(out, 5000);
    auto* backup = sqlite3_backup_init(out, "main", source, "main");
    if (backup == nullptr)
    {
        const juce::String error(sqlite3_errmsg(out));
        sqlite3_close(out);
        destination.deleteFile();
        throw std::runtime_error("Cannot start database backup: " + error.toStdString());
    }
    int rc = SQLITE_OK;
    for (;;)
    {
        rc = sqlite3_backup_step(backup, 256);
        if (rc == SQLITE_BUSY || rc == SQLITE_LOCKED) { sqlite3_sleep(50); continue; }
        if (rc == SQLITE_OK) continue;
        break;
    }
    const int finishResult = sqlite3_backup_finish(backup);
    const juce::String error(sqlite3_errmsg(out));
    sqlite3_close(out);
    if (rc != SQLITE_DONE || finishResult != SQLITE_OK)
    {
        // Never leave a zero-byte file that looks like a usable backup.
        destination.deleteFile();
        throw std::runtime_error("Database backup failed: " + error.toStdString());
    }
}
}
juce::String SampleRepository::searchKey(const juce::String& name, const juce::String& path)
{
    return foldForSearch(name) + "\n" + foldForSearch(path);
}
void SampleRepository::createTables()
{
    std::lock_guard lock(db.mutex);
    detail::Statement version(db.handle, "PRAGMA user_version");
    const int current = version.step() ? (int) version.integer(0) : 0;
    if (current > schemaVersion) throw std::runtime_error("Database schema is newer than this application");
    detail::Statement tables(db.handle, "SELECT COUNT(*) FROM sqlite_master WHERE name IN ('samples','roots')");
    const int existingTables = tables.step() ? (int) tables.integer(0) : 0;
    const bool upgrading = current < schemaVersion && (current >= 1 || existingTables > 0);
    if (upgrading)
    {
        // WAL may still hold committed pages, so copy through SQLite rather than the file system.
        const auto target = db.file().getSiblingFile(db.file().getFileName() + ".backup-"
            + juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S")
            + "-" + juce::String::toHexString(juce::Random::getSystemRandom().nextInt())).getNonexistentSibling();
        backupDatabase(db.handle, target);
    }
    try
    {
        detail::exec(db.handle,
            "BEGIN IMMEDIATE;"
            "CREATE TABLE IF NOT EXISTS samples (id INTEGER PRIMARY KEY, path TEXT NOT NULL UNIQUE,"
            "name TEXT NOT NULL, hash TEXT NOT NULL, format TEXT NOT NULL, error TEXT NOT NULL,"
            "size INTEGER, modified INTEGER, analyzed INTEGER, duration REAL, rate REAL, bits INTEGER,"
            "channels INTEGER, version INTEGER, peak REAL, rms REAL, centroid REAL, rolloff REAL, flatness REAL, missing INTEGER,"
            "search_key TEXT NOT NULL DEFAULT '', starred INTEGER NOT NULL DEFAULT 0);"
            "CREATE INDEX IF NOT EXISTS samples_hash ON samples(hash);"
            "CREATE INDEX IF NOT EXISTS samples_name_id ON samples(name COLLATE NOCASE, id);"
            "CREATE TABLE IF NOT EXISTS roots(path TEXT PRIMARY KEY);"
            "CREATE TABLE IF NOT EXISTS tags (id INTEGER PRIMARY KEY, name TEXT NOT NULL COLLATE NOCASE UNIQUE,"
            "colour INTEGER NOT NULL, builtin INTEGER NOT NULL DEFAULT 0);"
            "CREATE TABLE IF NOT EXISTS sample_tags (sample_id INTEGER NOT NULL REFERENCES samples(id) ON DELETE CASCADE,"
            "tag_id INTEGER NOT NULL REFERENCES tags(id) ON DELETE CASCADE, PRIMARY KEY(sample_id, tag_id)) WITHOUT ROWID;"
            "CREATE INDEX IF NOT EXISTS sample_tags_tag ON sample_tags(tag_id, sample_id);"
            "CREATE TABLE IF NOT EXISTS duplicates (sample_id INTEGER PRIMARY KEY REFERENCES samples(id) ON DELETE CASCADE,"
            "representative_id INTEGER NOT NULL, group_hash TEXT NOT NULL, group_size INTEGER NOT NULL,"
            "is_representative INTEGER NOT NULL DEFAULT 0);"
            "CREATE INDEX IF NOT EXISTS duplicates_hash ON duplicates(group_hash);"
            "CREATE INDEX IF NOT EXISTS duplicates_rep ON duplicates(representative_id);"
            "CREATE TABLE IF NOT EXISTS smart_candidates ("
            "sample_id INTEGER NOT NULL REFERENCES samples(id) ON DELETE CASCADE,"
            "field TEXT NOT NULL, source TEXT NOT NULL, status TEXT NOT NULL,"
            "detail TEXT NOT NULL DEFAULT '', text_value TEXT NOT NULL DEFAULT '',"
            "numeric_value REAL NOT NULL DEFAULT 0,"
            "PRIMARY KEY(sample_id, field, source, detail)) WITHOUT ROWID;"
            "CREATE INDEX IF NOT EXISTS smart_candidates_field ON smart_candidates(field, numeric_value);"
            "CREATE TABLE IF NOT EXISTS smart_results ("
            "sample_id INTEGER PRIMARY KEY REFERENCES samples(id) ON DELETE CASCADE,"
            "type TEXT NOT NULL DEFAULT '', structure TEXT NOT NULL DEFAULT '',"
            "bpm REAL NOT NULL DEFAULT 0, has_bpm INTEGER NOT NULL DEFAULT 0,"
            "key_tonic INTEGER NOT NULL DEFAULT -1, key_mode TEXT NOT NULL DEFAULT '',"
            "root_note INTEGER NOT NULL DEFAULT -1,"
            "type_state TEXT NOT NULL DEFAULT 'unknown', structure_state TEXT NOT NULL DEFAULT 'unknown',"
            "bpm_state TEXT NOT NULL DEFAULT 'unknown', key_state TEXT NOT NULL DEFAULT 'unknown',"
            "root_state TEXT NOT NULL DEFAULT 'unknown', explanation TEXT NOT NULL DEFAULT '',"
            "resolver_version INTEGER NOT NULL DEFAULT 1, updated_at INTEGER NOT NULL DEFAULT 0) WITHOUT ROWID;"
            "CREATE TABLE IF NOT EXISTS smart_overrides ("
            "sample_id INTEGER NOT NULL REFERENCES samples(id) ON DELETE CASCADE,"
            "field TEXT NOT NULL, cleared INTEGER NOT NULL DEFAULT 0,"
            "text_value TEXT NOT NULL DEFAULT '', numeric_value REAL NOT NULL DEFAULT 0,"
            "updated_at INTEGER NOT NULL DEFAULT 0, PRIMARY KEY(sample_id, field)) WITHOUT ROWID;"
            "CREATE TABLE IF NOT EXISTS smart_inputs ("
            "sample_id INTEGER NOT NULL REFERENCES samples(id) ON DELETE CASCADE,"
            "kind TEXT NOT NULL, path TEXT NOT NULL, size INTEGER NOT NULL DEFAULT 0,"
            "modified INTEGER NOT NULL DEFAULT 0, PRIMARY KEY(sample_id, kind, path)) WITHOUT ROWID;");
        // Databases created before schema 3 predate the folded search column.
        bool hasSearchKey = false, hasStarred = false;
        {
            detail::Statement info(db.handle, "PRAGMA table_info(samples)");
            while (info.step()) if (info.text(1) == "search_key") hasSearchKey = true;
        }
        if (! hasSearchKey) detail::exec(db.handle, "ALTER TABLE samples ADD COLUMN search_key TEXT NOT NULL DEFAULT '';");
        {
            detail::Statement info(db.handle, "PRAGMA table_info(samples)");
            while (info.step()) if (info.text(1) == "starred") hasStarred = true;
        }
        if (! hasStarred) detail::exec(db.handle, "ALTER TABLE samples ADD COLUMN starred INTEGER NOT NULL DEFAULT 0;");
        // Rows written by the build that bumped the version to 2 measured the same feature set, so
        // they are relabelled rather than reanalyzed. Only during this one-time upgrade.
        if (current >= 1 && current < 2) detail::exec(db.handle, "UPDATE samples SET version=1 WHERE version=2;");
        if (current < 3)
        {
            // SQL lower() only folds ASCII, so the key is built with the same fold used by queries.
            detail::Statement rows(db.handle, "SELECT id,name,path FROM samples WHERE id>? ORDER BY id LIMIT 2000");
            detail::Statement update(db.handle, "UPDATE samples SET search_key=? WHERE id=?");
            int64_t lastId = 0;
            for (;;)
            {
                rows.reset(); rows.integer(1, lastId);
                std::vector<std::pair<int64_t, juce::String>> batch;
                while (rows.step())
                    batch.emplace_back(rows.integer(0), searchKey(rows.text(1), rows.text(2)));
                if (batch.empty()) break;
                for (const auto& [id, key] : batch)
                {
                    update.reset(); update.text(1, key); update.integer(2, id); update.step();
                }
                lastId = batch.back().first;
            }
            detail::Statement tagCount(db.handle, "SELECT COUNT(*) FROM tags");
            if (tagCount.step() && tagCount.integer(0) == 0)
            {
                detail::Statement insert(db.handle, "INSERT OR IGNORE INTO tags(name,colour,builtin) VALUES(?,?,1)");
                for (const auto& tag : standardTags)
                {
                    insert.reset(); insert.text(1, tag.name); insert.integer(2, (int64_t) tag.colour); insert.step();
                }
            }
        }
        detail::exec(db.handle, "PRAGMA user_version=6;");
        detail::exec(db.handle, "COMMIT;");
    }
    catch (...)
    {
        try { detail::exec(db.handle, "ROLLBACK;"); } catch (...) {}
        throw;
    }
}
int64_t SampleRepository::upsert(const SampleRecord& r)
{
    std::lock_guard lock(db.mutex);
    detail::Statement s(db.handle,
        "INSERT INTO samples(path,name,hash,format,error,size,modified,analyzed,duration,rate,bits,channels,version,peak,rms,centroid,rolloff,flatness,missing,search_key,starred)"
        " VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) ON CONFLICT(path) DO UPDATE SET "
        "name=excluded.name,hash=excluded.hash,format=excluded.format,error=excluded.error,size=excluded.size,"
        "modified=excluded.modified,analyzed=excluded.analyzed,duration=excluded.duration,rate=excluded.rate,"
        "bits=excluded.bits,channels=excluded.channels,version=excluded.version,peak=excluded.peak,rms=excluded.rms,"
        "centroid=excluded.centroid,rolloff=excluded.rolloff,flatness=excluded.flatness,missing=excluded.missing,"
        "search_key=excluded.search_key RETURNING id");
    s.text(1,r.filePath); s.text(2,r.fileName); s.text(3,r.fileHash); s.text(4,r.format); s.text(5,r.error);
    s.integer(6,r.fileSize); s.integer(7,r.dateModified); s.integer(8,r.dateAnalyzed);
    s.real(9,r.duration); s.real(10,r.sampleRate); s.integer(11,r.bitsPerSample); s.integer(12,r.numChannels);
    s.integer(13,r.version); s.real(14,r.peakLevel); s.real(15,r.rmsLevel); s.real(16,r.spectralCentroid);
    s.real(17,r.spectralRolloff); s.real(18,r.spectralFlatness); s.integer(19,r.missing ? 1 : 0);
    s.text(20,searchKey(r.fileName,r.filePath)); s.integer(21,r.starred ? 1 : 0);
    if (! s.step()) throw std::runtime_error("Insert did not return a sample ID");
    const auto id = s.integer(0);
    // Reanalysed audio invalidates the derived smart result; candidates and overrides are kept
    // and the next smart run rebuilds the result from them.
    detail::Statement invalidate(db.handle, "DELETE FROM smart_results WHERE sample_id=?");
    invalidate.integer(1, id); invalidate.step();
    return id;
}
std::optional<SampleRecord> SampleRepository::findByPath(const juce::String& path) const
{
    std::lock_guard lock(db.mutex);
    auto sql = "SELECT " + juce::String(columns) + " FROM samples WHERE path=?";
    detail::Statement s(db.handle, sql.toRawUTF8()); s.text(1,path);
    return s.step() ? std::optional<SampleRecord>(read(s)) : std::nullopt;
}
std::vector<SampleRecord> SampleRepository::page(int limit, int offset) const
{
    std::lock_guard lock(db.mutex);
    auto sql = "SELECT " + juce::String(columns) + " FROM samples ORDER BY name COLLATE NOCASE,id LIMIT ? OFFSET ?";
    detail::Statement s(db.handle,sql.toRawUTF8());
    s.integer(1,juce::jlimit(0,50000,limit)); s.integer(2,juce::jmax(0,offset));
    std::vector<SampleRecord> out;
    while (s.step()) out.push_back(read(s));
    return out;
}
std::vector<PathEntry> SampleRepository::pathsAfterId(int64_t lastId, int limit) const
{
    std::lock_guard lock(db.mutex);
    detail::Statement s(db.handle,"SELECT id,path,missing FROM samples WHERE id>? ORDER BY id LIMIT ?");
    s.integer(1,juce::jmax<int64_t>(0,lastId)); s.integer(2,juce::jlimit(0,50000,limit));
    std::vector<PathEntry> out;
    while (s.step())
        out.push_back({s.integer(0), s.text(1), s.integer(2) != 0});
    return out;
}
void SampleRepository::setMissing(const std::vector<int64_t>& ids)
{
    if (ids.empty()) return;
    std::lock_guard lock(db.mutex);
    detail::exec(db.handle,"BEGIN IMMEDIATE;");
    try
    {
        detail::Statement s(db.handle,"UPDATE samples SET missing=1 WHERE id=?");
        for (auto id : ids) { s.integer(1,id); s.step(); s.reset(); }
        detail::exec(db.handle,"COMMIT;");
    }
    catch (...)
    {
        try { detail::exec(db.handle,"ROLLBACK;"); } catch (...) {}
        throw;
    }
}
int SampleRepository::count() const
{
    std::lock_guard lock(db.mutex);
    detail::Statement s(db.handle,"SELECT COUNT(*) FROM samples");
    return s.step() ? (int) s.integer(0) : 0;
}
int SampleRepository::countInFolder(const juce::String& folder) const
{
    if (folder.isEmpty()) return count();
    std::lock_guard lock(db.mutex);
    auto [lower,upper] = folderRange(folder);
    detail::Statement s(db.handle,"SELECT COUNT(*) FROM samples WHERE path>=? AND path<?");
    s.text(1,lower); s.text(2,upper);
    return s.step() ? (int) s.integer(0) : 0;
}
std::vector<SampleRecord> SampleRepository::pageInFolder(const juce::String& folder,int limit,int offset) const
{
    if (folder.isEmpty()) return page(limit,offset);
    std::lock_guard lock(db.mutex);
    auto [lower,upper] = folderRange(folder);
    auto sql = "SELECT " + juce::String(columns) + " FROM samples WHERE path>=? AND path<? ORDER BY name COLLATE NOCASE,id LIMIT ? OFFSET ?";
    detail::Statement s(db.handle,sql.toRawUTF8());
    s.text(1,lower); s.text(2,upper); s.integer(3,juce::jlimit(0,50000,limit)); s.integer(4,juce::jmax(0,offset));
    std::vector<SampleRecord> records;
    while (s.step()) records.push_back(read(s));
    return records;
}
LibraryPage SampleRepository::query(const LibraryFilter& filter, int limit, int offset) const
{
    std::lock_guard lock(db.mutex);
    const auto parts = buildFilterSql(filter);
    LibraryPage result;
    {
        auto sql = "SELECT COUNT(*) FROM samples" + parts.where;
        detail::Statement s(db.handle, sql.toRawUTF8()); bindFilter(s, parts);
        if (s.step()) result.total = (int) s.integer(0);
    }
    {
        auto sql = "SELECT " + juce::String(columns) + " FROM samples" + parts.where
            + " ORDER BY " + orderBy(filter.sort, filter.ascending) + " LIMIT ? OFFSET ?";
        detail::Statement s(db.handle, sql.toRawUTF8());
        const int next = bindFilter(s, parts);
        s.integer(next, juce::jlimit(0, 50000, limit));
        s.integer(next + 1, juce::jmax(0, offset));
        while (s.step()) result.samples.push_back(read(s));
    }
    return result;
}
std::vector<int64_t> SampleRepository::queryIds(const LibraryFilter& filter) const
{
    std::lock_guard lock(db.mutex);
    const auto parts = buildFilterSql(filter);
    auto sql = "SELECT samples.id FROM samples" + parts.where
        + " ORDER BY " + orderBy(filter.sort, filter.ascending);
    detail::Statement s(db.handle, sql.toRawUTF8()); bindFilter(s, parts);
    std::vector<int64_t> ids;
    while (s.step()) ids.push_back(s.integer(0));
    return ids;
}
std::vector<SampleRecord> SampleRepository::recordsByIds(const std::vector<int64_t>& ids) const
{
    std::lock_guard lock(db.mutex);
    std::map<int64_t, SampleRecord> byId;
    constexpr size_t chunkSize = 400;
    for (size_t start = 0; start < ids.size(); start += chunkSize)
    {
        const size_t end = std::min(ids.size(), start + chunkSize);
        juce::String placeholders;
        for (size_t i = start; i < end; ++i) placeholders += (i == start ? "?" : ",?");
        auto sql = "SELECT " + juce::String(columns) + " FROM samples WHERE id IN (" + placeholders + ")";
        detail::Statement s(db.handle, sql.toRawUTF8());
        int index = 1;
        for (size_t i = start; i < end; ++i) s.integer(index++, ids[i]);
        while (s.step())
        {
            auto record = read(s);
            byId[record.id] = std::move(record);
        }
    }
    std::vector<SampleRecord> out;
    out.reserve(ids.size());
    for (auto id : ids)
    {
        const auto found = byId.find(id);
        if (found != byId.end()) out.push_back(found->second);
    }
    return out;
}
int SampleRepository::countHash(const juce::String& hash) const
{
    std::lock_guard lock(db.mutex);
    detail::Statement s(db.handle,"SELECT COUNT(*) FROM samples WHERE hash=? AND hash<>''"); s.text(1,hash);
    return s.step() ? (int) s.integer(0) : 0;
}
void SampleRepository::addRoot(const juce::String& path)
{
    std::lock_guard lock(db.mutex);
    detail::Statement s(db.handle,"INSERT OR IGNORE INTO roots(path) VALUES(?)"); s.text(1,path); s.step();
}
juce::StringArray SampleRepository::roots() const
{
    std::lock_guard lock(db.mutex);
    detail::Statement s(db.handle,"SELECT path FROM roots ORDER BY path"); juce::StringArray out;
    while (s.step()) out.add(s.text(0));
    return out;
}
int SampleRepository::countUnderRoot(const juce::String& root) const
{
    std::lock_guard lock(db.mutex);
    const auto trimmed = pathrelocation::trimTrailingSeparators(root);
    const auto slash = escapeLike(pathrelocation::withSeparator(trimmed, '/')) + "%";
    const auto backslash = escapeLike(pathrelocation::withSeparator(trimmed, '\\')) + "%";
    detail::Statement s(db.handle,
        "SELECT COUNT(*) FROM samples WHERE path LIKE ? ESCAPE '\\' OR path LIKE ? ESCAPE '\\'");
    s.text(1, slash); s.text(2, backslash);
    return s.step() ? (int) s.integer(0) : 0;
}
RelocationResult SampleRepository::relocateRoots(const std::vector<RootRelocation>& relocations)
{
    std::lock_guard lock(db.mutex);
    for (const auto& relocation : relocations)
    {
        if (relocation.oldRoot.isEmpty()) throw std::runtime_error("Old folder is empty");
        if (relocation.newRoot.isEmpty() || ! juce::File(relocation.newRoot).isDirectory())
            throw std::runtime_error("New folder does not exist: " + relocation.newRoot.toStdString());
    }
    RelocationResult result;
    if (relocations.empty()) return result;

    detail::exec(db.handle, "BEGIN IMMEDIATE");
    try
    {
        struct Row { int64_t id = 0; juce::String name, path; };
        detail::Statement rows(db.handle, "SELECT id,name,path FROM samples WHERE id>? ORDER BY id LIMIT 2000");
        detail::Statement update(db.handle, "UPDATE OR IGNORE samples SET path=?, search_key=? WHERE id=?");
        detail::Statement target(db.handle, "SELECT id FROM samples WHERE path=?");
        detail::Statement mergeTags(db.handle,
            "INSERT OR IGNORE INTO sample_tags(sample_id,tag_id) SELECT ?,tag_id FROM sample_tags WHERE sample_id=?");
        detail::Statement mergeStar(db.handle,
            "UPDATE samples SET starred = starred | COALESCE((SELECT starred FROM samples WHERE id=?),0) WHERE id=?");
        detail::Statement deleteOld(db.handle, "DELETE FROM samples WHERE id=?");
        int64_t lastId = 0;
        for (;;)
        {
            rows.reset(); rows.integer(1, lastId);
            std::vector<Row> batch;
            while (rows.step()) batch.push_back({ rows.integer(0), rows.text(1), rows.text(2) });
            if (batch.empty()) break;
            for (const auto& row : batch)
            {
                const RootRelocation* best = nullptr;
                for (const auto& relocation : relocations)
                    if (pathrelocation::isUnder(row.path, relocation.oldRoot)
                        && (best == nullptr || relocation.oldRoot.length() > best->oldRoot.length()))
                        best = &relocation;
                if (best == nullptr) continue;
                const auto newPath = pathrelocation::remap(row.path, best->oldRoot, best->newRoot);
                if (newPath == row.path) continue;
                update.reset(); update.text(1, newPath);
                update.text(2, searchKey(row.name, newPath)); update.integer(3, row.id); update.step();
                if (sqlite3_changes(db.handle) == 0)
                {
                    // The target path already exists: keep that row and move the user's tags and
                    // star onto it, then remove the duplicate. Analysis already belongs to the row.
                    target.reset(); target.text(1, newPath);
                    if (! target.step()) throw std::runtime_error("Cannot relocate a catalog path");
                    const auto existingId = target.integer(0);
                    mergeTags.reset(); mergeTags.integer(1, existingId); mergeTags.integer(2, row.id); mergeTags.step();
                    mergeStar.reset(); mergeStar.integer(1, row.id); mergeStar.integer(2, existingId); mergeStar.step();
                    deleteOld.reset(); deleteOld.integer(1, row.id); deleteOld.step();
                    ++result.conflicts;
                }
                else
                {
                    ++result.movedFiles;
                }
            }
            lastId = batch.back().id;
        }
        detail::Statement deleteRoot(db.handle, "DELETE FROM roots WHERE path=?");
        detail::Statement insertRoot(db.handle, "INSERT OR IGNORE INTO roots(path) VALUES(?)");
        for (const auto& relocation : relocations)
        {
            deleteRoot.reset(); deleteRoot.text(1, relocation.oldRoot); deleteRoot.step();
            insertRoot.reset(); insertRoot.text(1, relocation.newRoot); insertRoot.step();
        }
        detail::exec(db.handle, "COMMIT");
        return result;
    }
    catch (...)
    {
        try { detail::exec(db.handle, "ROLLBACK"); } catch (...) {}
        throw;
    }
}
bool SampleRepository::isCatalog(const juce::File& file)
{
    if (! file.existsAsFile()) return false;
    sqlite3* handle = nullptr;
    if (sqlite3_open_v2(file.getFullPathName().toRawUTF8(), &handle, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK)
    {
        if (handle != nullptr) sqlite3_close(handle);
        return false;
    }
    bool catalog = false;
    sqlite3_stmt* statement = nullptr;
    constexpr auto sql = "SELECT (SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name IN ('samples','roots'))"
                         ", (SELECT user_version FROM pragma_user_version)";
    if (sqlite3_prepare_v2(handle, sql, -1, &statement, nullptr) == SQLITE_OK && sqlite3_step(statement) == SQLITE_ROW)
        catalog = sqlite3_column_int(statement, 0) == 2 && sqlite3_column_int(statement, 1) >= 1;
    if (statement != nullptr) sqlite3_finalize(statement);
    sqlite3_close(handle);
    return catalog;
}
void SampleRepository::backupTo(const juce::File& destination)
{
    std::lock_guard lock(db.mutex);
    if (destination == db.file()) throw std::runtime_error("Backup destination must differ from the live database");
    if (destination.existsAsFile()) throw std::runtime_error("Backup destination already exists");
    if (! destination.getParentDirectory().createDirectory()) throw std::runtime_error("Cannot create backup directory");
    backupDatabase(db.handle, destination);
}
std::vector<Tag> SampleRepository::tags() const
{
    std::lock_guard lock(db.mutex);
    detail::Statement s(db.handle,
        "SELECT t.id,t.name,t.colour,t.builtin,COUNT(st.sample_id) FROM tags t"
        " LEFT JOIN sample_tags st ON st.tag_id=t.id GROUP BY t.id ORDER BY t.name COLLATE NOCASE");
    std::vector<Tag> out;
    while (s.step()) out.push_back(readTag(s, 0, 4));
    return out;
}
std::vector<Tag> SampleRepository::tagsForSample(int64_t sampleId) const
{
    std::lock_guard lock(db.mutex);
    detail::Statement s(db.handle,
        "SELECT t.id,t.name,t.colour,t.builtin FROM tags t JOIN sample_tags st ON st.tag_id=t.id"
        " WHERE st.sample_id=? ORDER BY t.name COLLATE NOCASE");
    s.integer(1, sampleId);
    std::vector<Tag> out;
    while (s.step()) out.push_back(readTag(s, 0));
    return out;
}
std::map<int64_t, std::vector<Tag>> SampleRepository::tagsForSamples(const std::vector<int64_t>& sampleIds) const
{
    std::lock_guard lock(db.mutex);
    std::map<int64_t, std::vector<Tag>> out;
    constexpr size_t chunkSize = 400;
    for (size_t start = 0; start < sampleIds.size(); start += chunkSize)
    {
        const size_t end = std::min(sampleIds.size(), start + chunkSize);
        juce::String placeholders;
        for (size_t i = start; i < end; ++i) placeholders += (i == start ? "?" : ",?");
        auto sql = "SELECT st.sample_id,t.id,t.name,t.colour,t.builtin FROM sample_tags st"
            " JOIN tags t ON t.id=st.tag_id WHERE st.sample_id IN (" + placeholders + ")"
            " ORDER BY t.name COLLATE NOCASE";
        detail::Statement s(db.handle, sql.toRawUTF8());
        int index = 1;
        for (size_t i = start; i < end; ++i) s.integer(index++, sampleIds[i]);
        while (s.step())
            out[s.integer(0)].push_back(readTag(s, 1));
    }
    return out;
}
int64_t SampleRepository::createTag(const juce::String& name, uint32_t colour)
{
    const auto trimmed = name.trim();
    if (trimmed.isEmpty()) throw std::runtime_error("Tag name cannot be empty");
    std::lock_guard lock(db.mutex);
    {
        detail::Statement insert(db.handle, "INSERT OR IGNORE INTO tags(name,colour,builtin) VALUES(?,?,0)");
        insert.text(1, trimmed); insert.integer(2, (int64_t) colour); insert.step();
    }
    detail::Statement select(db.handle, "SELECT id FROM tags WHERE name=?");
    select.text(1, trimmed);
    if (! select.step()) throw std::runtime_error("Cannot create tag");
    return select.integer(0);
}
void SampleRepository::setTagColour(int64_t tagId, uint32_t colour)
{
    std::lock_guard lock(db.mutex);
    detail::Statement s(db.handle, "UPDATE tags SET colour=? WHERE id=?");
    s.integer(1, (int64_t) colour); s.integer(2, tagId); s.step();
}
bool SampleRepository::renameTag(int64_t tagId, const juce::String& name)
{
    const auto trimmed = name.trim();
    if (trimmed.isEmpty()) return false;
    std::lock_guard lock(db.mutex);
    detail::Statement s(db.handle, "UPDATE OR IGNORE tags SET name=? WHERE id=?");
    s.text(1, trimmed); s.integer(2, tagId); s.step();
    return sqlite3_changes(db.handle) > 0;
}
void SampleRepository::deleteTag(int64_t tagId)
{
    std::lock_guard lock(db.mutex);
    detail::Statement s(db.handle, "DELETE FROM tags WHERE id=?");
    s.integer(1, tagId); s.step();
}
void SampleRepository::setSampleTag(int64_t sampleId, int64_t tagId, bool assigned)
{
    std::lock_guard lock(db.mutex);
    if (assigned)
    {
        detail::Statement s(db.handle, "INSERT OR IGNORE INTO sample_tags(sample_id,tag_id) VALUES(?,?)");
        s.integer(1, sampleId); s.integer(2, tagId); s.step();
    }
    else
    {
        detail::Statement s(db.handle, "DELETE FROM sample_tags WHERE sample_id=? AND tag_id=?");
        s.integer(1, sampleId); s.integer(2, tagId); s.step();
    }
}
void SampleRepository::setSampleTag(const std::vector<int64_t>& sampleIds, int64_t tagId, bool assigned)
{
    if (sampleIds.empty()) return;
    std::lock_guard lock(db.mutex);
    detail::exec(db.handle, "BEGIN IMMEDIATE;");
    try
    {
        detail::Statement s(db.handle, assigned
            ? "INSERT OR IGNORE INTO sample_tags(sample_id,tag_id) VALUES(?,?)"
            : "DELETE FROM sample_tags WHERE sample_id=? AND tag_id=?");
        for (auto id : sampleIds)
        {
            s.integer(1, id); s.integer(2, tagId); s.step(); s.reset();
        }
        detail::exec(db.handle, "COMMIT;");
    }
    catch (...)
    {
        try { detail::exec(db.handle, "ROLLBACK;"); } catch (...) {}
        throw;
    }
}
void SampleRepository::setStarred(const std::vector<int64_t>& sampleIds, bool starred)
{
    if (sampleIds.empty()) return;
    std::lock_guard lock(db.mutex);
    detail::exec(db.handle, "BEGIN IMMEDIATE;");
    try
    {
        detail::Statement s(db.handle, "UPDATE samples SET starred=? WHERE id=?");
        for (auto id : sampleIds)
        {
            s.integer(1, starred ? 1 : 0); s.integer(2, id); s.step(); s.reset();
        }
        detail::exec(db.handle, "COMMIT;");
    }
    catch (...)
    {
        try { detail::exec(db.handle, "ROLLBACK;"); } catch (...) {}
        throw;
    }
}
void SampleRepository::refreshDuplicates()
{
    std::lock_guard lock(db.mutex);
    detail::exec(db.handle, "BEGIN IMMEDIATE;");
    try
    {
        detail::exec(db.handle, "DELETE FROM duplicates;");
        detail::Statement groups(db.handle,
            "SELECT hash FROM samples WHERE hash<>'' AND size>0 GROUP BY hash HAVING COUNT(*)>1");
        std::vector<juce::String> hashes;
        while (groups.step()) hashes.push_back(groups.text(0));
        struct Member { int64_t id = 0; bool starred = false, missing = false; bool failed = false; int pathLength = 0; };
        detail::Statement select(db.handle,
            "SELECT id,path,starred,missing,error FROM samples WHERE hash=?");
        detail::Statement insert(db.handle,
            "INSERT OR REPLACE INTO duplicates(sample_id,representative_id,group_hash,group_size,is_representative)"
            " VALUES(?,?,?,?,?)");
        for (const auto& hash : hashes)
        {
            select.reset(); select.text(1, hash);
            std::vector<Member> members;
            while (select.step())
                members.push_back({ select.integer(0), select.integer(2) != 0, select.integer(3) != 0,
                                    ! select.text(4).isEmpty(), select.text(1).length() });
            if (members.size() < 2) continue;
            const auto better = [](const Member& a, const Member& b)
            {
                if (a.starred != b.starred) return a.starred;
                if (a.missing != b.missing) return ! a.missing;
                if (a.failed != b.failed) return ! a.failed;
                if (a.pathLength != b.pathLength) return a.pathLength < b.pathLength;
                return a.id < b.id;
            };
            const auto representative = std::min_element(members.begin(), members.end(), better);
            const auto groupSize = (int) members.size();
            for (const auto& member : members)
            {
                insert.reset(); insert.integer(1, member.id); insert.integer(2, representative->id);
                insert.text(3, hash); insert.integer(4, groupSize);
                insert.integer(5, member.id == representative->id ? 1 : 0); insert.step();
            }
        }
        detail::exec(db.handle, "COMMIT;");
    }
    catch (...)
    {
        try { detail::exec(db.handle, "ROLLBACK;"); } catch (...) {}
        throw;
    }
}
std::map<int64_t,int> SampleRepository::duplicateCounts(const std::vector<int64_t>& sampleIds) const
{
    std::lock_guard lock(db.mutex);
    std::map<int64_t,int> out;
    constexpr size_t chunkSize = 400;
    for (size_t start = 0; start < sampleIds.size(); start += chunkSize)
    {
        const size_t end = std::min(sampleIds.size(), start + chunkSize);
        juce::String placeholders;
        for (size_t i = start; i < end; ++i) placeholders += (i == start ? "?" : ",?");
        auto sql = "SELECT sample_id,group_size FROM duplicates WHERE sample_id IN (" + placeholders + ")";
        detail::Statement s(db.handle, sql.toRawUTF8());
        int index = 1;
        for (size_t i = start; i < end; ++i) s.integer(index++, sampleIds[i]);
        while (s.step()) out[s.integer(0)] = (int) s.integer(1);
    }
    return out;
}
std::optional<SampleRepository::DuplicateInfo> SampleRepository::duplicateInfo(int64_t sampleId) const
{
    std::lock_guard lock(db.mutex);
    detail::Statement s(db.handle,
        "SELECT d.group_size,d.representative_id,s.path FROM duplicates d"
        " JOIN samples s ON s.id=d.representative_id WHERE d.sample_id=?");
    s.integer(1, sampleId);
    if (! s.step()) return std::nullopt;
    return DuplicateInfo { (int) s.integer(0), s.integer(1), s.text(2) };
}
std::vector<SampleRecord> SampleRepository::duplicateMembers(int64_t sampleId) const
{
    std::lock_guard lock(db.mutex);
    detail::Statement group(db.handle, "SELECT group_hash FROM duplicates WHERE sample_id=?");
    group.integer(1, sampleId);
    if (! group.step()) return {};
    const auto hash = group.text(0);
    auto sql = "SELECT " + juce::String(columns) + " FROM samples WHERE hash=?"
        " ORDER BY missing ASC,starred DESC,length(path) ASC,id ASC";
    detail::Statement s(db.handle, sql.toRawUTF8()); s.text(1, hash);
    std::vector<SampleRecord> out;
    while (s.step()) out.push_back(read(s));
    return out;
}
void SampleRepository::replaceSmartCandidates(int64_t sampleId, const std::vector<SmartCandidate>& candidates)
{
    std::lock_guard lock(db.mutex);
    detail::exec(db.handle, "BEGIN IMMEDIATE;");
    try
    {
        detail::Statement remove(db.handle, "DELETE FROM smart_candidates WHERE sample_id=?");
        remove.integer(1, sampleId); remove.step();
        detail::Statement insert(db.handle,
            "INSERT OR REPLACE INTO smart_candidates"
            "(sample_id,field,source,status,detail,text_value,numeric_value) VALUES(?,?,?,?,?,?,?)");
        for (const auto& c : candidates)
        {
            insert.reset();
            insert.integer(1, sampleId);
            insert.text(2, smartFieldKey(c.field));
            insert.text(3, c.source);
            insert.text(4, candidateStatusKey(c.status));
            insert.text(5, c.detail);
            insert.text(6, c.textValue);
            insert.real(7, c.numericValue);
            insert.step();
        }
        detail::exec(db.handle, "COMMIT;");
    }
    catch (...)
    {
        try { detail::exec(db.handle, "ROLLBACK;"); } catch (...) {}
        throw;
    }
}
std::vector<SmartCandidate> SampleRepository::smartCandidates(int64_t sampleId) const
{
    std::lock_guard lock(db.mutex);
    detail::Statement s(db.handle,
        "SELECT field,source,status,detail,text_value,numeric_value FROM smart_candidates"
        " WHERE sample_id=? ORDER BY field,source,detail");
    s.integer(1, sampleId);
    std::vector<SmartCandidate> out;
    while (s.step())
    {
        SmartCandidate c;
        c.field = smartFieldFromKey(s.text(0)).value_or(SmartField::Type);
        c.source = s.text(1);
        c.status = candidateStatusFromKey(s.text(2)).value_or(CandidateStatus::Invalid);
        c.detail = s.text(3);
        c.textValue = s.text(4);
        c.numericValue = s.real(5);
        out.push_back(c);
    }
    return out;
}
void SampleRepository::setSmartResult(int64_t sampleId, const SmartResult& r)
{
    std::lock_guard lock(db.mutex);
    detail::Statement s(db.handle,
        "INSERT OR REPLACE INTO smart_results"
        "(sample_id,type,structure,bpm,has_bpm,key_tonic,key_mode,root_note,type_state,"
        "structure_state,bpm_state,key_state,root_state,explanation,resolver_version,updated_at)"
        " VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    s.integer(1, sampleId);
    s.text(2, r.type); s.text(3, r.structure);
    s.real(4, r.bpm); s.integer(5, r.hasBpm ? 1 : 0);
    s.integer(6, r.keyTonic); s.text(7, r.keyMode); s.integer(8, r.rootNote);
    s.text(9, smartStateKey(r.typeState)); s.text(10, smartStateKey(r.structureState));
    s.text(11, smartStateKey(r.bpmState)); s.text(12, smartStateKey(r.keyState));
    s.text(13, smartStateKey(r.rootState)); s.text(14, r.explanation);
    s.integer(15, 1);
    s.integer(16, juce::Time::currentTimeMillis());
    s.step();
}
namespace {
SmartState stateFromKey(const juce::String& key)
{
    for (auto state : { SmartState::Unknown, SmartState::Resolved, SmartState::Ambiguous,
                        SmartState::Conflicting, SmartState::Unsupported, SmartState::Default,
                        SmartState::Overridden })
        if (smartStateKey(state) == key) return state;
    return SmartState::Unknown;
}
}
std::optional<SmartResult> SampleRepository::smartResult(int64_t sampleId) const
{
    std::lock_guard lock(db.mutex);
    detail::Statement s(db.handle,
        "SELECT type,structure,bpm,has_bpm,key_tonic,key_mode,root_note,type_state,"
        "structure_state,bpm_state,key_state,root_state,explanation FROM smart_results WHERE sample_id=?");
    s.integer(1, sampleId);
    if (! s.step()) return std::nullopt;
    SmartResult r;
    r.type = s.text(0); r.structure = s.text(1);
    r.bpm = s.real(2); r.hasBpm = s.integer(3) != 0;
    r.keyTonic = (int) s.integer(4); r.keyMode = s.text(5); r.rootNote = (int) s.integer(6);
    r.typeState = stateFromKey(s.text(7)); r.structureState = stateFromKey(s.text(8));
    r.bpmState = stateFromKey(s.text(9)); r.keyState = stateFromKey(s.text(10));
    r.rootState = stateFromKey(s.text(11)); r.explanation = s.text(12);
    return r;
}
void SampleRepository::setSmartOverride(int64_t sampleId, SmartField field, const SmartOverride& value)
{
    std::lock_guard lock(db.mutex);
    detail::Statement s(db.handle,
        "INSERT OR REPLACE INTO smart_overrides(sample_id,field,cleared,text_value,numeric_value,updated_at)"
        " VALUES(?,?,?,?,?,?)");
    s.integer(1, sampleId); s.text(2, smartFieldKey(field));
    s.integer(3, value.cleared ? 1 : 0); s.text(4, value.textValue);
    s.real(5, value.numericValue); s.integer(6, juce::Time::currentTimeMillis());
    s.step();
}
void SampleRepository::clearSmartOverride(int64_t sampleId, SmartField field)
{
    std::lock_guard lock(db.mutex);
    detail::Statement s(db.handle, "DELETE FROM smart_overrides WHERE sample_id=? AND field=?");
    s.integer(1, sampleId); s.text(2, smartFieldKey(field)); s.step();
}
SmartOverrideMap SampleRepository::smartOverrides(int64_t sampleId) const
{
    std::lock_guard lock(db.mutex);
    detail::Statement s(db.handle,
        "SELECT field,cleared,text_value,numeric_value FROM smart_overrides WHERE sample_id=?");
    s.integer(1, sampleId);
    SmartOverrideMap out;
    while (s.step())
    {
        auto field = smartFieldFromKey(s.text(0));
        if (! field) continue;
        SmartOverride value;
        value.cleared = s.integer(1) != 0;
        value.textValue = s.text(2);
        value.numericValue = s.real(3);
        out[*field] = value;
    }
    return out;
}
void SampleRepository::setSmartInputs(int64_t sampleId, const std::vector<SmartInput>& inputs)
{
    std::lock_guard lock(db.mutex);
    detail::exec(db.handle, "BEGIN IMMEDIATE;");
    try
    {
        detail::Statement remove(db.handle, "DELETE FROM smart_inputs WHERE sample_id=?");
        remove.integer(1, sampleId); remove.step();
        detail::Statement insert(db.handle,
            "INSERT OR REPLACE INTO smart_inputs(sample_id,kind,path,size,modified) VALUES(?,?,?,?,?)");
        for (const auto& input : inputs)
        {
            insert.reset(); insert.integer(1, sampleId); insert.text(2, input.kind);
            insert.text(3, input.path); insert.integer(4, input.size); insert.integer(5, input.modified);
            insert.step();
        }
        detail::exec(db.handle, "COMMIT;");
    }
    catch (...)
    {
        try { detail::exec(db.handle, "ROLLBACK;"); } catch (...) {}
        throw;
    }
}
std::vector<SmartInput> SampleRepository::smartInputs(int64_t sampleId) const
{
    std::lock_guard lock(db.mutex);
    detail::Statement s(db.handle,
        "SELECT kind,path,size,modified FROM smart_inputs WHERE sample_id=? ORDER BY kind,path");
    s.integer(1, sampleId);
    std::vector<SmartInput> out;
    while (s.step())
        out.push_back({ s.text(0), s.text(1), s.integer(2), s.integer(3) });
    return out;
}
std::optional<SampleRecord> SampleRepository::findById(int64_t id) const
{
    std::lock_guard lock(db.mutex);
    const auto sql = juce::String("SELECT ") + columns + " FROM samples WHERE id=?";
    detail::Statement s(db.handle, sql.toRawUTF8());
    s.integer(1, id);
    if (! s.step()) return std::nullopt;
    return read(s);
}
std::vector<int64_t> SampleRepository::samplesNeedingSmartAnalysis(int64_t afterId, int limit) const
{
    std::lock_guard lock(db.mutex);
    detail::Statement s(db.handle,
        "SELECT s.id FROM samples s LEFT JOIN smart_results r ON r.sample_id=s.id"
        " WHERE s.id>? AND r.sample_id IS NULL ORDER BY s.id LIMIT ?");
    s.integer(1, afterId); s.integer(2, limit);
    std::vector<int64_t> out;
    while (s.step()) out.push_back(s.integer(0));
    return out;
}
} // namespace sonicator
