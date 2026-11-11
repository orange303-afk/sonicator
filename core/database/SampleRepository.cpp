#include "SampleRepository.h"
#include "SearchSyntax.h"
#include "core/audio/StereoPair.h"
#include "core/smart/KeyNames.h"
#include "core/smart/SmartResolver.h"
#include "PathRelocation.h"
#include "SqliteHelpers.h"
#include <algorithm>
#include <set>
#include <variant>
namespace sonicator {
namespace {
constexpr auto columns = "id,path,name,hash,format,error,size,modified,analyzed,duration,rate,bits,channels,version,peak,rms,centroid,rolloff,flatness,missing,starred,compatibility,container_path,disc_entry,disc_parent,disc_partner,disc_path";
constexpr int schemaVersion = 13;
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
    r.compatibility = assetCompatibilityFromInt((int) s.integer(21));
    r.containerPath = s.text(22); r.discEntry = s.text(23); r.discParent = s.text(24);
    r.discPartner = s.text(25); r.discPath = s.text(26);
    return r;
}
struct StandardTag { const char* name; uint32_t colour; };
constexpr StandardTag standardTags[] = {
    { "Kick", 0xffe05a5a }, { "Snare", 0xffe08a45 }, { "Clap", 0xffe0b84a },
    { "Hat", 0xffd7dd6a }, { "Percussion", 0xff8fd06a }, { "Bass", 0xff4fb8c8 },
    { "Sub", 0xff4f8fd0 }, { "Lead", 0xff7a6aea }, { "Pad", 0xffa96ae0 },
    { "Keys", 0xffd06ab8 }, { "Guitar", 0xffd06a7a }, { "Vocal", 0xff6ad0a0 },
    { "FX", 0xff8a8f9c }, { "Loop", 0xff9ad06a }, { "One-shot", 0xffc8a06a },
    { "Crash", 0xffe07a4a }, { "Ride", 0xffd9b45a }, { "Tom", 0xff9ac06a },
    { "Rim", 0xffc0a06a }, { "Fill", 0xffb0b0b0 }, { "Riser", 0xff6ac0d0 },
    { "Downfilter", 0xff7a8ad0 }, { "Upfilter", 0xffb07ad0 }, { "Snap", 0xffe09a6a },
    { "Shaker", 0xffc8d06a }, { "Tamb", 0xffe0c05a }
};
// Added by schema 9. An upgrade inserts only these, so a user-deleted older tag is not recreated.
constexpr StandardTag schema9Tags[] = {
    { "Crash", 0xffe07a4a }, { "Ride", 0xffd9b45a }, { "Tom", 0xff9ac06a },
    { "Rim", 0xffc0a06a }, { "Fill", 0xffb0b0b0 }, { "Riser", 0xff6ac0d0 },
    { "Downfilter", 0xff7a8ad0 }, { "Upfilter", 0xffb07ad0 }, { "Snap", 0xffe09a6a },
    { "Shaker", 0xffc8d06a }, { "Tamb", 0xffe0c05a }
};
std::pair<juce::String,juce::String> folderRange(const juce::String& folder)
{
    auto prefix = juce::File(folder).getFullPathName();
    if (! prefix.endsWithChar(juce::File::getSeparatorChar())) prefix += juce::File::getSeparatorString();
    return {prefix, prefix.dropLastCharacters(1) + juce::String::charToString(juce::File::getSeparatorChar()+1)};
}
// Mono files whose names differ only by an L/R marker in the same folder share this key.
std::pair<juce::String,int> makePairKey(const SampleRecord& record)
{
    if (record.containerPath.isNotEmpty()) return { {}, 0 }; // disc listings already merge pairs
    if (record.numChannels != 1 || record.sampleRate <= 0) return { {}, 0 };
    const auto name = parseStereoName(record.fileName);
    if (name.side == 0 || name.base.isEmpty()) return { {}, 0 };
    const auto folder = juce::File(record.filePath).getParentDirectory().getFullPathName();
    return { folder + "\n" + name.base.toLowerCase(), name.side };
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
// Effective values used by whole-catalog sorting. The correlated subqueries are covered by the
// primary keys (sample_id) and by (sample_id, field), so they stay index lookups per row.
juce::String effectiveTypeSql()
{
    return "(CASE WHEN EXISTS(SELECT 1 FROM smart_overrides so WHERE so.sample_id=samples.id"
           " AND so.field='type') THEN (SELECT CASE WHEN so.cleared=0 THEN NULLIF(so.text_value,'') END"
           " FROM smart_overrides so WHERE so.sample_id=samples.id AND so.field='type')"
           " ELSE (SELECT NULLIF(sr.type,'') FROM smart_results sr WHERE sr.sample_id=samples.id)"
           " END)";
}
juce::String effectiveBpmSql()
{
    return "(CASE WHEN EXISTS(SELECT 1 FROM smart_overrides so WHERE so.sample_id=samples.id"
           " AND so.field='bpm') THEN (SELECT CASE WHEN so.cleared=0"
           " AND so.numeric_value>=20 AND so.numeric_value<=400 THEN so.numeric_value END"
           " FROM smart_overrides so WHERE so.sample_id=samples.id AND so.field='bpm')"
           " ELSE (SELECT CASE WHEN sr.has_bpm=1 THEN sr.bpm END FROM smart_results sr"
           " WHERE sr.sample_id=samples.id) END)";
}
juce::String effectiveKeyTonicSql()
{
    return "(CASE WHEN EXISTS(SELECT 1 FROM smart_overrides so WHERE so.sample_id=samples.id"
           " AND so.field='key') THEN (SELECT CASE WHEN so.cleared=0 AND so.numeric_value>=0"
           " THEN so.numeric_value END FROM smart_overrides so"
           " WHERE so.sample_id=samples.id AND so.field='key')"
           " ELSE (SELECT CASE WHEN sr.key_tonic>=0 THEN sr.key_tonic END FROM smart_results sr"
           " WHERE sr.sample_id=samples.id) END)";
}
juce::String effectiveKeyModeSql()
{
    return "(CASE WHEN EXISTS(SELECT 1 FROM smart_overrides so WHERE so.sample_id=samples.id"
           " AND so.field='key') THEN (SELECT NULLIF(so.aux_text,'') FROM smart_overrides so"
           " WHERE so.sample_id=samples.id AND so.field='key')"
           " ELSE (SELECT NULLIF(sr.key_mode,'') FROM smart_results sr"
           " WHERE sr.sample_id=samples.id) END)";
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
        case LibrarySort::Type:
        {
            const auto expr = effectiveTypeSql();
            return "(" + expr + " IS NULL) ASC, " + expr + " COLLATE NOCASE "
                 + (ascending ? "ASC" : "DESC") + ", samples.id ASC";
        }
        case LibrarySort::Bpm:
        {
            const auto expr = effectiveBpmSql();
            return "(" + expr + " IS NULL) ASC, " + expr + " "
                 + (ascending ? "ASC" : "DESC") + ", samples.id ASC";
        }
        case LibrarySort::Key:
        {
            const auto tonic = effectiveKeyTonicSql();
            const auto mode = effectiveKeyModeSql();
            return "(" + tonic + " IS NULL) ASC, " + tonic + " "
                 + (ascending ? "ASC" : "DESC") + ", " + mode + " COLLATE NOCASE "
                 + (ascending ? "ASC" : "DESC") + ", samples.id ASC";
        }
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
// Visibility is cached in samples.ignored (refreshed by ignorePath/unignorePath), so the query
// stays parameter-free and independent of how many paths are ignored.
void appendIgnore(FilterSql& target, const std::vector<IgnoredPath>& ignored)
{
    if (! ignored.empty()) target.where += " AND samples.ignored=0";
}
// The R half of a valid stereo pair shares the L row in the library.
void appendStereoVisibility(FilterSql& target)
{
    target.where += " AND NOT EXISTS(SELECT 1 FROM stereo_pairs sp"
                    " WHERE sp.sample_id=samples.id AND sp.is_representative=0)";
}
FilterSql buildFilterSql(const LibraryFilter& filter, const std::vector<IgnoredPath>& ignored)
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
        auto escapeGlob = [](const juce::String& value)
        {
            juce::String out;
            for (auto c : value)
            {
                if (c == '*' || c == '?' || c == '[')
                    out += "[" + juce::String::charToString(c) + "]";
                else
                    out += juce::String::charToString(c);
            }
            return out;
        };

        switch (term.kind)
        {
            case SearchKind::Text:
                if (term.negated)
                    addText(" AND samples.search_key NOT LIKE ? ESCAPE '\\'", like(term.text));
                else
                    addText(" AND samples.search_key LIKE ? ESCAPE '\\'", like(term.text));
                break;
            case SearchKind::ExactPhrase:
                if (term.negated)
                    addText(" AND samples.search_key NOT LIKE ? ESCAPE '\\'", like(term.text));
                else
                    addText(" AND samples.search_key LIKE ? ESCAPE '\\'", like(term.text));
                break;
            case SearchKind::ExactWord:
            {
                const auto escaped = escapeGlob(term.text.toLowerCase());
                juce::String cond =
                    "(lower(samples.name) GLOB ('*[^a-zA-Z0-9]' || ? || '[^a-zA-Z0-9]*')"
                    " OR lower(samples.name) GLOB (? || '[^a-zA-Z0-9]*')"
                    " OR lower(samples.name) GLOB ('*[^a-zA-Z0-9]' || ?)"
                    " OR lower(samples.name) GLOB ?)";
                if (term.negated)
                    sql.where += " AND NOT " + cond;
                else
                    sql.where += " AND " + cond;
                sql.params.emplace_back(escaped);
                sql.params.emplace_back(escaped);
                sql.params.emplace_back(escaped);
                sql.params.emplace_back(escaped);
                break;
            }
            case SearchKind::Glob:
                if (term.negated)
                    addText(" AND samples.name NOT LIKE ? ESCAPE '\\'", globToLike(term.text));
                else
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
                if (term.negated)
                    sql.where += " AND NOT (" + condition + ")";
                else
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
            {
                // A manual type is the effective type, so it must win over the automatic one.
                juce::String cond =
                    "(EXISTS(SELECT 1 FROM smart_overrides so WHERE so.sample_id=samples.id"
                    " AND so.field='type' AND so.cleared=0 AND so.text_value=? COLLATE NOCASE)"
                    " OR (NOT EXISTS(SELECT 1 FROM smart_overrides so WHERE so.sample_id=samples.id"
                    " AND so.field='type') AND EXISTS(SELECT 1 FROM smart_results sr"
                    " WHERE sr.sample_id=samples.id AND sr.type=? COLLATE NOCASE)))";
                const auto tVal = term.typeValue.isNotEmpty() ? term.typeValue : term.text;
                if (term.negated)
                    sql.where += " AND NOT " + cond;
                else
                    sql.where += " AND " + cond;
                sql.params.emplace_back(tVal);
                sql.params.emplace_back(tVal);
                break;
            }
            case SearchKind::TypeOrText:
            {
                juce::String cond =
                    "((EXISTS(SELECT 1 FROM smart_overrides so WHERE so.sample_id=samples.id"
                    " AND so.field='type' AND so.cleared=0 AND so.text_value=? COLLATE NOCASE)"
                    " OR (NOT EXISTS(SELECT 1 FROM smart_overrides so WHERE so.sample_id=samples.id"
                    " AND so.field='type') AND EXISTS(SELECT 1 FROM smart_results sr"
                    " WHERE sr.sample_id=samples.id AND sr.type=? COLLATE NOCASE)))"
                    " OR samples.search_key LIKE ? ESCAPE '\\')";
                const auto tVal = term.typeValue.isNotEmpty() ? term.typeValue : term.text;
                if (term.negated)
                    sql.where += " AND NOT (" + cond + ")";
                else
                    sql.where += " AND " + cond;
                sql.params.emplace_back(tVal);
                sql.params.emplace_back(tVal);
                sql.params.emplace_back(like(term.text));
                break;
            }
            case SearchKind::KeyOnly:
            case SearchKind::KeyOrText:
            {
                juce::String condition =
                    "EXISTS(SELECT 1 FROM smart_overrides so WHERE so.sample_id=samples.id"
                    " AND so.field='key' AND so.cleared=0 AND so.numeric_value=?";
                if (term.mode.isNotEmpty()) condition += " AND so.aux_text=?";
                condition += ") OR (NOT EXISTS(SELECT 1 FROM smart_overrides so WHERE so.sample_id=samples.id"
                             " AND so.field='key') AND EXISTS(SELECT 1 FROM smart_results sr"
                             " WHERE sr.sample_id=samples.id AND sr.key_tonic=?"
                             " AND sr.key_state IN ('resolved','ambiguous','conflicting','default','overridden')";
                if (term.mode.isNotEmpty()) condition += " AND sr.key_mode=?";
                condition += "))";
                if (term.kind == SearchKind::KeyOrText)
                    condition += " OR samples.search_key LIKE ? ESCAPE '\\'";
                sql.where += " AND (" + condition + ")";
                sql.params.emplace_back((int64_t) term.tonic);
                if (term.mode.isNotEmpty()) sql.params.emplace_back(term.mode);
                sql.params.emplace_back((int64_t) term.tonic);
                if (term.mode.isNotEmpty()) sql.params.emplace_back(term.mode);
                if (term.kind == SearchKind::KeyOrText) sql.params.emplace_back(like(term.text));
                break;
            }
            case SearchKind::NoteOnly:
                sql.where += " AND (EXISTS(SELECT 1 FROM smart_overrides so WHERE so.sample_id=samples.id"
                             " AND so.field='root_note' AND so.cleared=0 AND so.numeric_value=?)"
                             " OR (NOT EXISTS(SELECT 1 FROM smart_overrides so WHERE so.sample_id=samples.id"
                             " AND so.field='root_note') AND EXISTS(SELECT 1 FROM smart_results sr"
                             " WHERE sr.sample_id=samples.id AND sr.root_note=?"
                             " AND sr.root_state IN ('resolved','conflicting','overridden'))))";
                sql.params.emplace_back((int64_t) term.midi);
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
    // Metadata-only containers have no decoded peak, so "Hide empty" must not hide them. An
    // unsupported or invalid asset is hidden by the same toggle, but unknown (legacy rows,
    // transient I/O, missing files) is never treated as a verdict.
    if (filter.hideEmpty)
        sql.where += " AND (samples.peak>=0.001 OR samples.version=-1)"
                     " AND samples.compatibility NOT IN (2,3)";
    if (filter.hideDuplicates)
        sql.where += " AND NOT EXISTS(SELECT 1 FROM duplicates d WHERE d.sample_id=samples.id AND d.is_representative=0)";
    if (filter.duplicateHash.isNotEmpty())
        addText(" AND samples.hash=?", filter.duplicateHash);
    if (! filter.showIgnored) appendIgnore(sql, ignored);
    appendStereoVisibility(sql);
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
void backupDatabase(sqlite3* source, const juce::File& destination){
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
// Adds a column only when it is missing, so a fresh schema and an upgraded one end up equal.
void ensureColumn(sqlite3* handle, const char* table, const char* column, const char* definition)
{
    bool found = false;
    {
        detail::Statement info(handle, ("PRAGMA table_info(" + juce::String(table) + ")").toRawUTF8());
        while (info.step()) if (info.text(1) == column) { found = true; break; }
    }
    if (! found)
        detail::exec(handle, ("ALTER TABLE " + juce::String(table) + " ADD COLUMN "
                              + juce::String(column) + " " + juce::String(definition) + ";").toRawUTF8());
}
}
juce::String SampleRepository::searchKey(const juce::String& name, const juce::String& path)
{
    return foldForSearch(name) + "\n" + foldForSearch(path);
}
bool isPathIgnored(const juce::String& path, const std::vector<IgnoredPath>& entries)
{
    const juce::File file(path);
    for (const auto& entry : entries)
    {
        if (file == juce::File(entry.path)) return true;
        if (entry.folder && file.isAChildOf(juce::File(entry.path))) return true;
    }
    return false;
}
void SampleRepository::removeByPaths(const juce::StringArray& paths)
{
    if (paths.isEmpty()) return;
    std::lock_guard lock(db.mutex);
    detail::exec(db.handle, "BEGIN IMMEDIATE;");
    try
    {
        detail::Statement deleteSample(db.handle, "DELETE FROM samples WHERE path=?");
        detail::Statement deleteContents(db.handle, "DELETE FROM samples WHERE container_path=?");
        detail::Statement deleteState(db.handle, "DELETE FROM disc_index WHERE image_path=?");
        detail::Statement deleteIgnore(db.handle,
            "DELETE FROM ignored_paths WHERE path=? AND is_folder=0");
        for (const auto& path : paths)
        {
            if (path.isEmpty()) continue;
            deleteSample.reset(); deleteSample.text(1, path); deleteSample.step();
            deleteContents.reset(); deleteContents.text(1, path); deleteContents.step();
            deleteState.reset(); deleteState.text(1, path); deleteState.step();
            deleteIgnore.reset(); deleteIgnore.text(1, path); deleteIgnore.step();
        }
        detail::exec(db.handle, "COMMIT;");
    }
    catch (...)
    {
        try { detail::exec(db.handle, "ROLLBACK;"); } catch (...) {}
        throw;
    }
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
            "search_key TEXT NOT NULL DEFAULT '', starred INTEGER NOT NULL DEFAULT 0,"
            "compatibility INTEGER NOT NULL DEFAULT 0, ignored INTEGER NOT NULL DEFAULT 0,"
            "pair_key TEXT NOT NULL DEFAULT '', pair_side INTEGER NOT NULL DEFAULT 0,"
            "container_path TEXT NOT NULL DEFAULT '', disc_entry TEXT NOT NULL DEFAULT '',"
            "disc_parent TEXT NOT NULL DEFAULT '', disc_partner TEXT NOT NULL DEFAULT '',"
            "disc_path TEXT NOT NULL DEFAULT '');"
            "CREATE INDEX IF NOT EXISTS samples_hash ON samples(hash);"
            "CREATE INDEX IF NOT EXISTS samples_name_id ON samples(name COLLATE NOCASE, id);"
            "CREATE TABLE IF NOT EXISTS roots(path TEXT PRIMARY KEY);"
            "CREATE TABLE IF NOT EXISTS tags (id INTEGER PRIMARY KEY, name TEXT NOT NULL COLLATE NOCASE UNIQUE,"
            "colour INTEGER NOT NULL, builtin INTEGER NOT NULL DEFAULT 0);"
            "CREATE TABLE IF NOT EXISTS sample_tags (sample_id INTEGER NOT NULL REFERENCES samples(id) ON DELETE CASCADE,"
            "tag_id INTEGER NOT NULL REFERENCES tags(id) ON DELETE CASCADE, PRIMARY KEY(sample_id, tag_id)) WITHOUT ROWID;"
            "CREATE INDEX IF NOT EXISTS sample_tags_tag ON sample_tags(tag_id, sample_id);"
            "CREATE TABLE IF NOT EXISTS recent_tags ("
            "tag_id INTEGER PRIMARY KEY REFERENCES tags(id) ON DELETE CASCADE,"
            "used_at INTEGER NOT NULL, seq INTEGER NOT NULL) WITHOUT ROWID;"
            "CREATE TABLE IF NOT EXISTS duplicates (sample_id INTEGER PRIMARY KEY REFERENCES samples(id) ON DELETE CASCADE,"
            "representative_id INTEGER NOT NULL, group_hash TEXT NOT NULL, group_size INTEGER NOT NULL,"
            "is_representative INTEGER NOT NULL DEFAULT 0);"
            "CREATE INDEX IF NOT EXISTS duplicates_hash ON duplicates(group_hash);"
            "CREATE INDEX IF NOT EXISTS duplicates_rep ON duplicates(representative_id);"
            "CREATE TABLE IF NOT EXISTS smart_candidates ("
            "sample_id INTEGER NOT NULL REFERENCES samples(id) ON DELETE CASCADE,"
            "field TEXT NOT NULL, source TEXT NOT NULL, status TEXT NOT NULL,"
            "detail TEXT NOT NULL DEFAULT '', text_value TEXT NOT NULL DEFAULT '',"
            "numeric_value REAL NOT NULL DEFAULT 0, aux_value REAL NOT NULL DEFAULT 0,"
            "extractor_id TEXT NOT NULL DEFAULT '', extractor_version INTEGER NOT NULL DEFAULT 0,"
            "raw_value TEXT NOT NULL DEFAULT '', input_kind TEXT NOT NULL DEFAULT '',"
            "input_path TEXT NOT NULL DEFAULT '', fingerprint TEXT NOT NULL DEFAULT '',"
            "limitation TEXT NOT NULL DEFAULT '', variable_tempo INTEGER NOT NULL DEFAULT 0,"
            "extracted_at INTEGER NOT NULL DEFAULT 0,"
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
            "root_state TEXT NOT NULL DEFAULT 'unknown',"
            "pitch_midi INTEGER NOT NULL DEFAULT -1, pitch_cents REAL NOT NULL DEFAULT 0,"
            "pitch_state TEXT NOT NULL DEFAULT 'unknown',"
            "type_source TEXT NOT NULL DEFAULT '', structure_source TEXT NOT NULL DEFAULT '',"
            "bpm_source TEXT NOT NULL DEFAULT '', key_source TEXT NOT NULL DEFAULT '',"
            "pitch_source TEXT NOT NULL DEFAULT '', root_source TEXT NOT NULL DEFAULT '',"
            "type_override TEXT NOT NULL DEFAULT 'none', structure_override TEXT NOT NULL DEFAULT 'none',"
            "bpm_override TEXT NOT NULL DEFAULT 'none', key_override TEXT NOT NULL DEFAULT 'none',"
            "pitch_override TEXT NOT NULL DEFAULT 'none', root_override TEXT NOT NULL DEFAULT 'none',"
            "explanation TEXT NOT NULL DEFAULT '',"
            "revision INTEGER NOT NULL DEFAULT 0, generation INTEGER NOT NULL DEFAULT 0,"
            "pipeline_state TEXT NOT NULL DEFAULT 'not_analyzed',"
            "extractor_version INTEGER NOT NULL DEFAULT 0, resolver_version INTEGER NOT NULL DEFAULT 0,"
            "dsp_version INTEGER NOT NULL DEFAULT 0, analyzed_at INTEGER NOT NULL DEFAULT 0,"
            "error_code TEXT NOT NULL DEFAULT '', error_detail TEXT NOT NULL DEFAULT '',"
            "updated_at INTEGER NOT NULL DEFAULT 0) WITHOUT ROWID;"
            "CREATE TABLE IF NOT EXISTS smart_overrides ("
            "sample_id INTEGER NOT NULL REFERENCES samples(id) ON DELETE CASCADE,"
            "field TEXT NOT NULL, cleared INTEGER NOT NULL DEFAULT 0,"
            "not_applicable INTEGER NOT NULL DEFAULT 0,"
            "text_value TEXT NOT NULL DEFAULT '', numeric_value REAL NOT NULL DEFAULT 0,"
            "aux_value REAL NOT NULL DEFAULT 0, aux_text TEXT NOT NULL DEFAULT '',"
            "updated_at INTEGER NOT NULL DEFAULT 0, PRIMARY KEY(sample_id, field)) WITHOUT ROWID;"
            "CREATE TABLE IF NOT EXISTS smart_inputs ("
            "sample_id INTEGER NOT NULL REFERENCES samples(id) ON DELETE CASCADE,"
            "kind TEXT NOT NULL, path TEXT NOT NULL, size INTEGER NOT NULL DEFAULT 0,"
            "modified INTEGER NOT NULL DEFAULT 0, PRIMARY KEY(sample_id, kind, path)) WITHOUT ROWID;"
            "CREATE TABLE IF NOT EXISTS smart_extractions ("
            "sample_id INTEGER NOT NULL REFERENCES samples(id) ON DELETE CASCADE,"
            "source TEXT NOT NULL, extractor_version INTEGER NOT NULL DEFAULT 0,"
            "status TEXT NOT NULL DEFAULT '', error TEXT NOT NULL DEFAULT '',"
            "updated_at INTEGER NOT NULL DEFAULT 0, PRIMARY KEY(sample_id, source)) WITHOUT ROWID;"
            "CREATE TABLE IF NOT EXISTS smart_failures ("
            "sample_id INTEGER PRIMARY KEY REFERENCES samples(id) ON DELETE CASCADE,"
            "code TEXT NOT NULL DEFAULT '', message TEXT NOT NULL DEFAULT '',"
            "attempts INTEGER NOT NULL DEFAULT 0, updated_at INTEGER NOT NULL DEFAULT 0) WITHOUT ROWID;"
            "CREATE TABLE IF NOT EXISTS smart_meta ("
            "key TEXT PRIMARY KEY, value INTEGER NOT NULL DEFAULT 0) WITHOUT ROWID;"
            "CREATE TABLE IF NOT EXISTS ignored_paths ("
            "path TEXT PRIMARY KEY, is_folder INTEGER NOT NULL DEFAULT 0,"
            "created_at INTEGER NOT NULL DEFAULT 0);"
            "CREATE TABLE IF NOT EXISTS disc_index ("
            "image_path TEXT PRIMARY KEY, image_size INTEGER NOT NULL DEFAULT 0,"
            "image_modified INTEGER NOT NULL DEFAULT 0, entry_count INTEGER NOT NULL DEFAULT 0,"
            "indexed_at INTEGER NOT NULL DEFAULT 0);"
            "CREATE TABLE IF NOT EXISTS stereo_pairs ("
            "sample_id INTEGER PRIMARY KEY REFERENCES samples(id) ON DELETE CASCADE,"
            "representative_id INTEGER NOT NULL, partner_id INTEGER NOT NULL,"
            "is_representative INTEGER NOT NULL DEFAULT 0);"
            "CREATE INDEX IF NOT EXISTS stereo_pairs_rep ON stereo_pairs(representative_id);"
            "CREATE TABLE IF NOT EXISTS custom_types ("
            "name TEXT PRIMARY KEY NOT NULL COLLATE NOCASE, created_at INTEGER NOT NULL);"
            "CREATE TABLE IF NOT EXISTS ml_feedback_samples ("
            "sample_id INTEGER PRIMARY KEY REFERENCES samples(id) ON DELETE CASCADE, file_hash TEXT NOT NULL,"
            "target_type TEXT NOT NULL COLLATE NOCASE, tags TEXT NOT NULL DEFAULT '',"
            "features BLOB NOT NULL, updated_at INTEGER NOT NULL,"
            "contribution_id TEXT NOT NULL DEFAULT '');"
            "CREATE TABLE IF NOT EXISTS contribution_state ("
            "key TEXT PRIMARY KEY, value INTEGER NOT NULL DEFAULT 0) WITHOUT ROWID;");
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
        // Schema 10: why an asset cannot be opened. Existing rows stay unknown (0) and are never
        // reanalysed or hidden by a guess.
        ensureColumn(db.handle, "samples", "compatibility", "INTEGER NOT NULL DEFAULT 0");
        // Cached visibility derived from ignored_paths. Keeping it on the row makes every query
        // independent of how many paths are ignored; the table stays the source of truth.
        ensureColumn(db.handle, "samples", "ignored", "INTEGER NOT NULL DEFAULT 0");
        ensureColumn(db.handle, "samples", "pair_key", "TEXT NOT NULL DEFAULT ''");
        ensureColumn(db.handle, "samples", "pair_side", "INTEGER NOT NULL DEFAULT 0");
        // Schema 12: samples that live inside a disc image. Ordinary rows keep empty values.
        ensureColumn(db.handle, "samples", "container_path", "TEXT NOT NULL DEFAULT ''");
        ensureColumn(db.handle, "samples", "disc_entry", "TEXT NOT NULL DEFAULT ''");
        ensureColumn(db.handle, "samples", "disc_parent", "TEXT NOT NULL DEFAULT ''");
        ensureColumn(db.handle, "samples", "disc_partner", "TEXT NOT NULL DEFAULT ''");
        ensureColumn(db.handle, "samples", "disc_path", "TEXT NOT NULL DEFAULT ''");
        detail::exec(db.handle,
            "CREATE TABLE IF NOT EXISTS disc_index ("
            "image_path TEXT PRIMARY KEY, image_size INTEGER NOT NULL DEFAULT 0,"
            "image_modified INTEGER NOT NULL DEFAULT 0, entry_count INTEGER NOT NULL DEFAULT 0,"
            "indexed_at INTEGER NOT NULL DEFAULT 0);");
        // Schema 7 extends smart storage in place. Existing rows keep their identity, tags, stars,
        // measured features, duplicate groups and overrides; only new columns/tables are added.
        ensureColumn(db.handle, "smart_candidates", "aux_value", "REAL NOT NULL DEFAULT 0");
        ensureColumn(db.handle, "smart_candidates", "extractor_id", "TEXT NOT NULL DEFAULT ''");
        ensureColumn(db.handle, "smart_candidates", "extractor_version", "INTEGER NOT NULL DEFAULT 0");
        ensureColumn(db.handle, "smart_candidates", "raw_value", "TEXT NOT NULL DEFAULT ''");
        ensureColumn(db.handle, "smart_candidates", "input_kind", "TEXT NOT NULL DEFAULT ''");
        ensureColumn(db.handle, "smart_candidates", "input_path", "TEXT NOT NULL DEFAULT ''");
        ensureColumn(db.handle, "smart_candidates", "fingerprint", "TEXT NOT NULL DEFAULT ''");
        ensureColumn(db.handle, "smart_candidates", "limitation", "TEXT NOT NULL DEFAULT ''");
        ensureColumn(db.handle, "smart_candidates", "variable_tempo", "INTEGER NOT NULL DEFAULT 0");
        ensureColumn(db.handle, "smart_candidates", "extracted_at", "INTEGER NOT NULL DEFAULT 0");
        ensureColumn(db.handle, "smart_results", "pitch_midi", "INTEGER NOT NULL DEFAULT -1");
        ensureColumn(db.handle, "smart_results", "pitch_cents", "REAL NOT NULL DEFAULT 0");
        ensureColumn(db.handle, "smart_results", "pitch_state", "TEXT NOT NULL DEFAULT 'unknown'");
        ensureColumn(db.handle, "smart_results", "type_source", "TEXT NOT NULL DEFAULT ''");
        ensureColumn(db.handle, "smart_results", "structure_source", "TEXT NOT NULL DEFAULT ''");
        ensureColumn(db.handle, "smart_results", "bpm_source", "TEXT NOT NULL DEFAULT ''");
        ensureColumn(db.handle, "smart_results", "key_source", "TEXT NOT NULL DEFAULT ''");
        ensureColumn(db.handle, "smart_results", "pitch_source", "TEXT NOT NULL DEFAULT ''");
        ensureColumn(db.handle, "smart_results", "root_source", "TEXT NOT NULL DEFAULT ''");
        ensureColumn(db.handle, "smart_results", "type_override", "TEXT NOT NULL DEFAULT 'none'");
        ensureColumn(db.handle, "smart_results", "structure_override", "TEXT NOT NULL DEFAULT 'none'");
        ensureColumn(db.handle, "smart_results", "bpm_override", "TEXT NOT NULL DEFAULT 'none'");
        ensureColumn(db.handle, "smart_results", "key_override", "TEXT NOT NULL DEFAULT 'none'");
        ensureColumn(db.handle, "smart_results", "pitch_override", "TEXT NOT NULL DEFAULT 'none'");
        ensureColumn(db.handle, "smart_results", "root_override", "TEXT NOT NULL DEFAULT 'none'");
        ensureColumn(db.handle, "smart_results", "revision", "INTEGER NOT NULL DEFAULT 0");
        ensureColumn(db.handle, "smart_results", "generation", "INTEGER NOT NULL DEFAULT 0");
        ensureColumn(db.handle, "smart_results", "pipeline_state", "TEXT NOT NULL DEFAULT 'not_analyzed'");
        ensureColumn(db.handle, "smart_results", "extractor_version", "INTEGER NOT NULL DEFAULT 0");
        ensureColumn(db.handle, "smart_results", "dsp_version", "INTEGER NOT NULL DEFAULT 0");
        ensureColumn(db.handle, "smart_results", "analyzed_at", "INTEGER NOT NULL DEFAULT 0");
        ensureColumn(db.handle, "smart_results", "error_code", "TEXT NOT NULL DEFAULT ''");
        ensureColumn(db.handle, "smart_results", "error_detail", "TEXT NOT NULL DEFAULT ''");
        ensureColumn(db.handle, "smart_overrides", "not_applicable", "INTEGER NOT NULL DEFAULT 0");
        ensureColumn(db.handle, "smart_overrides", "aux_value", "REAL NOT NULL DEFAULT 0");
        ensureColumn(db.handle, "smart_overrides", "aux_text", "TEXT NOT NULL DEFAULT ''");
        // Schema 13: a stable id per contributed record, so a later correction replaces the
        // earlier label during export instead of becoming a second sample.
        ensureColumn(db.handle, "ml_feedback_samples", "contribution_id", "TEXT NOT NULL DEFAULT ''");
        detail::exec(db.handle, "CREATE INDEX IF NOT EXISTS smart_results_pipeline"
                                " ON smart_results(pipeline_state, sample_id);");
        detail::exec(db.handle, "CREATE INDEX IF NOT EXISTS samples_pair_key ON samples(pair_key) WHERE pair_key != '';");
        detail::exec(db.handle, "CREATE INDEX IF NOT EXISTS samples_ignored ON samples(ignored);");
        detail::exec(db.handle, "CREATE INDEX IF NOT EXISTS samples_starred ON samples(starred) WHERE starred = 1;");
        // Rows written by schema 6 predate pipeline_state and versions. Their values are still
        // useful provenance, so they are marked stale (visible, not current) instead of
        // "not analyzed", which would hide data the user already has.
        if (current >= 1 && current < 7)
            detail::exec(db.handle, "UPDATE smart_results SET pipeline_state='stale'"
                                    " WHERE pipeline_state='not_analyzed';");
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
        if (current < 9)
        {
            detail::Statement insert(db.handle,
                "INSERT OR IGNORE INTO tags(name,colour,builtin) VALUES(?,?,1)");
            for (const auto& tag : schema9Tags)
            {
                insert.reset(); insert.text(1, tag.name);
                insert.integer(2, (int64_t) tag.colour); insert.step();
            }
        }
        {
            // Rows written before schema 13 have no id; give each one now, once.
            detail::Statement missing(db.handle,
                "SELECT sample_id FROM ml_feedback_samples WHERE contribution_id='' LIMIT 500");
            detail::Statement assign(db.handle,
                "UPDATE ml_feedback_samples SET contribution_id=? WHERE sample_id=?");
            for (;;)
            {
                std::vector<int64_t> ids;
                missing.reset();
                while (missing.step()) ids.push_back(missing.integer(0));
                if (ids.empty()) break;
                for (const auto id : ids)
                {
                    assign.reset();
                    assign.text(1, juce::Uuid().toDashedString().toLowerCase());
                    assign.integer(2, id);
                    assign.step();
                }
            }
        }
        detail::exec(db.handle, "PRAGMA user_version=13;");
        detail::exec(db.handle, "COMMIT;");
    }
    catch (...)
    {
        try { detail::exec(db.handle, "ROLLBACK;"); } catch (...) {}
        throw;
    }
}
int64_t SampleRepository::upsertLocked(const SampleRecord& r,
                                       const std::vector<IgnoredPath>* cachedIgnores)
{
    const auto entries = cachedIgnores != nullptr ? *cachedIgnores : ignoredPathsLocked();
    const bool ignored = isPathIgnored(r.filePath, entries)
        || (r.containerPath.isNotEmpty() && isPathIgnored(r.containerPath, entries));
    const auto [pairKey, pairSide] = makePairKey(r);
    detail::Statement s(db.handle,
        "INSERT INTO samples(path,name,hash,format,error,size,modified,analyzed,duration,rate,bits,channels,version,peak,rms,centroid,rolloff,flatness,missing,search_key,starred,compatibility,ignored,pair_key,pair_side,container_path,disc_entry,disc_parent,disc_partner,disc_path)"
        " VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) ON CONFLICT(path) DO UPDATE SET "
        "name=excluded.name,hash=excluded.hash,format=excluded.format,error=excluded.error,size=excluded.size,"
        "modified=excluded.modified,analyzed=excluded.analyzed,duration=excluded.duration,rate=excluded.rate,"
        "bits=excluded.bits,channels=excluded.channels,version=excluded.version,peak=excluded.peak,rms=excluded.rms,"
        "centroid=excluded.centroid,rolloff=excluded.rolloff,flatness=excluded.flatness,missing=excluded.missing,"
        "search_key=excluded.search_key,compatibility=excluded.compatibility,ignored=excluded.ignored,"
        "pair_key=excluded.pair_key,pair_side=excluded.pair_side,container_path=excluded.container_path,"
        "disc_entry=excluded.disc_entry,disc_parent=excluded.disc_parent,"
        "disc_partner=excluded.disc_partner,disc_path=excluded.disc_path RETURNING id");
    s.text(1,r.filePath); s.text(2,r.fileName); s.text(3,r.fileHash); s.text(4,r.format); s.text(5,r.error);
    s.integer(6,r.fileSize); s.integer(7,r.dateModified); s.integer(8,r.dateAnalyzed);
    s.real(9,r.duration); s.real(10,r.sampleRate); s.integer(11,r.bitsPerSample); s.integer(12,r.numChannels);
    s.integer(13,r.version); s.real(14,r.peakLevel); s.real(15,r.rmsLevel); s.real(16,r.spectralCentroid);
    s.real(17,r.spectralRolloff); s.real(18,r.spectralFlatness); s.integer(19,r.missing ? 1 : 0);
    s.text(20,searchKey(r.fileName,r.filePath)); s.integer(21,r.starred ? 1 : 0);
    s.integer(22,(int) r.compatibility); s.integer(23, ignored ? 1 : 0);
    s.text(24, pairKey); s.integer(25, pairSide);
    s.text(26, r.containerPath); s.text(27, r.discEntry); s.text(28, r.discParent);
    s.text(29, r.discPartner); s.text(30, r.discPath);
    if (! s.step()) throw std::runtime_error("Insert did not return a sample ID");
    const auto id = s.integer(0);
    // Reanalysed audio makes the derived smart result stale; candidates, overrides and the last
    // completed values are kept and the next smart run rebuilds them. The row is not deleted, so
    // the UI can still show the previous value as stale instead of pretending it was never known.
    if (r.containerPath.isEmpty())
    {
        detail::Statement invalidate(db.handle,
            "UPDATE smart_results SET pipeline_state='stale', updated_at=? WHERE sample_id=?");
        invalidate.integer(1, juce::Time::currentTimeMillis());
        invalidate.integer(2, id); invalidate.step();
    }
    return id;
}
int64_t SampleRepository::upsert(const SampleRecord& r)
{
    std::lock_guard lock(db.mutex);
    return upsertLocked(r);
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
    detail::Statement s(db.handle,
        "SELECT id,path,missing,container_path FROM samples WHERE id>? ORDER BY id LIMIT ?");
    s.integer(1,juce::jmax<int64_t>(0,lastId)); s.integer(2,juce::jlimit(0,50000,limit));
    std::vector<PathEntry> out;
    while (s.step())
        out.push_back({s.integer(0), s.text(1), s.integer(2) != 0, s.text(3)});
    return out;
}
std::vector<IgnoredPath> SampleRepository::ignoredPathsLocked() const
{
    detail::Statement s(db.handle,
        "SELECT path,is_folder,created_at FROM ignored_paths ORDER BY path");
    std::vector<IgnoredPath> out;
    while (s.step())
        out.push_back({ s.text(0), s.integer(1) != 0, s.integer(2) });
    return out;
}
std::vector<IgnoredPath> SampleRepository::ignoredPaths() const
{
    std::lock_guard lock(db.mutex);
    return ignoredPathsLocked();
}
bool SampleRepository::isIgnored(const juce::String& path) const
{
    std::lock_guard lock(db.mutex);
    return isPathIgnored(path, ignoredPathsLocked());
}
bool SampleRepository::hasExactIgnore(const juce::String& path) const
{
    std::lock_guard lock(db.mutex);
    detail::Statement s(db.handle, "SELECT 1 FROM ignored_paths WHERE path=?");
    s.text(1, path);
    return s.step();
}
void SampleRepository::ignorePath(const juce::String& path, bool folder)
{
    if (path.isEmpty()) return;
    std::lock_guard lock(db.mutex);
    detail::exec(db.handle, "BEGIN IMMEDIATE;");
    try
    {
        detail::Statement s(db.handle,
            "INSERT OR REPLACE INTO ignored_paths(path,is_folder,created_at) VALUES(?,?,?)");
        s.text(1, path);
        s.integer(2, folder ? 1 : 0);
        s.integer(3, juce::Time::currentTimeMillis());
        s.step();
        if (folder)
        {
            auto [lower, upper] = folderRange(path);
            detail::Statement u(db.handle, "UPDATE samples SET ignored=1 WHERE path>=? AND path<?");
            u.text(1, lower); u.text(2, upper); u.step();
        }
        else
        {
            // Ignoring a disc image hides the samples it contains as well.
            detail::Statement u(db.handle, "UPDATE samples SET ignored=1 WHERE path=? OR container_path=?");
            u.text(1, path); u.text(2, path); u.step();
        }
        detail::exec(db.handle, "COMMIT;");
    }
    catch (...)
    {
        try { detail::exec(db.handle, "ROLLBACK;"); } catch (...) {}
        throw;
    }
}
void SampleRepository::unignorePathLocked(const juce::String& path)
{
    bool folder = false;
    {
        detail::Statement s(db.handle, "SELECT is_folder FROM ignored_paths WHERE path=?");
        s.text(1, path);
        if (! s.step()) return;
        folder = s.integer(0) != 0;
    }
    {
        detail::Statement d(db.handle, "DELETE FROM ignored_paths WHERE path=?");
        d.text(1, path); d.step();
    }
    // A row may still be covered by another entry (an exact file inside an ignored folder, or
    // a nested folder), so recompute the cached flag from the remaining table.
    const auto entries = ignoredPathsLocked();
    std::unique_ptr<detail::Statement> select;
    if (folder)
    {
        auto [lower, upper] = folderRange(path);
        select = std::make_unique<detail::Statement>(db.handle,
            "SELECT id,path,container_path FROM samples WHERE path>=? AND path<?");
        select->text(1, lower); select->text(2, upper);
    }
    else
    {
        select = std::make_unique<detail::Statement>(db.handle,
            "SELECT id,path,container_path FROM samples WHERE path=? OR container_path=?");
        select->text(1, path); select->text(2, path);
    }
    detail::Statement update(db.handle, "UPDATE samples SET ignored=? WHERE id=?");
    while (select->step())
    {
        update.reset();
        const auto container = select->text(2);
        const bool ignored = isPathIgnored(select->text(1), entries)
            || (container.isNotEmpty() && isPathIgnored(container, entries));
        update.integer(1, ignored ? 1 : 0);
        update.integer(2, select->integer(0));
        update.step();
    }
}
void SampleRepository::unignorePath(const juce::String& path)
{
    std::lock_guard lock(db.mutex);
    detail::exec(db.handle, "BEGIN IMMEDIATE;");
    try
    {
        unignorePathLocked(path);
        detail::exec(db.handle, "COMMIT;");
    }
    catch (...)
    {
        try { detail::exec(db.handle, "ROLLBACK;"); } catch (...) {}
        throw;
    }
}
int SampleRepository::ignorePaths(const std::vector<juce::String>& paths, bool folder)
{
    if (paths.empty()) return 0;
    std::lock_guard lock(db.mutex);
    detail::exec(db.handle, "BEGIN IMMEDIATE;");
    int changed = 0;
    try
    {
        detail::Statement s(db.handle,
            "INSERT OR REPLACE INTO ignored_paths(path,is_folder,created_at) VALUES(?,?,?)");
        const auto now = juce::Time::currentTimeMillis();
        std::unique_ptr<detail::Statement> uFolder, uFile;
        if (folder)
            uFolder = std::make_unique<detail::Statement>(db.handle,
                "UPDATE samples SET ignored=1 WHERE path>=? AND path<?");
        else
            uFile = std::make_unique<detail::Statement>(db.handle,
                "UPDATE samples SET ignored=1 WHERE path=? OR container_path=?");

        for (const auto& path : paths)
        {
            if (path.isEmpty()) continue;
            s.reset();
            s.text(1, path);
            s.integer(2, folder ? 1 : 0);
            s.integer(3, now);
            s.step();
            if (folder)
            {
                auto [lower, upper] = folderRange(path);
                uFolder->reset();
                uFolder->text(1, lower); uFolder->text(2, upper); uFolder->step();
            }
            else
            {
                uFile->reset();
                uFile->text(1, path); uFile->text(2, path); uFile->step();
            }
            ++changed;
        }
        detail::exec(db.handle, "COMMIT;");
    }
    catch (...)
    {
        try { detail::exec(db.handle, "ROLLBACK;"); } catch (...) {}
        throw;
    }
    return changed;
}
int SampleRepository::unignorePaths(const std::vector<juce::String>& paths)
{
    if (paths.empty()) return 0;
    std::lock_guard lock(db.mutex);
    detail::exec(db.handle, "BEGIN IMMEDIATE;");
    int changed = 0;
    try
    {
        for (const auto& path : paths)
        {
            if (path.isEmpty()) continue;
            unignorePathLocked(path);
            ++changed;
        }
        detail::exec(db.handle, "COMMIT;");
    }
    catch (...)
    {
        try { detail::exec(db.handle, "ROLLBACK;"); } catch (...) {}
        throw;
    }
    return changed;
}
int SampleRepository::unignoreCoveringFolders(const juce::String& path)
{
    if (path.isEmpty()) return 0;
    const juce::File folder(path);
    std::lock_guard lock(db.mutex);
    juce::StringArray targets;
    for (const auto& entry : ignoredPathsLocked())
        if (entry.folder && (folder == juce::File(entry.path) || folder.isAChildOf(juce::File(entry.path))))
            targets.addIfNotAlreadyThere(entry.path);
    if (targets.isEmpty()) return 0;
    detail::exec(db.handle, "BEGIN IMMEDIATE;");
    try
    {
        for (const auto& target : targets) unignorePathLocked(target);
        detail::exec(db.handle, "COMMIT;");
    }
    catch (...)
    {
        try { detail::exec(db.handle, "ROLLBACK;"); } catch (...) {}
        throw;
    }
    return targets.size();
}
std::vector<juce::String> SampleRepository::errorPathsWithFormats(const juce::StringArray& formats) const
{
    std::vector<juce::String> out;
    if (formats.isEmpty()) return out;
    juce::String placeholders;
    for (int i = 0; i < formats.size(); ++i) placeholders << (i == 0 ? "?" : ",?");
    const auto sql = "SELECT path FROM samples WHERE error<>'' AND format IN (" + placeholders + ")";
    std::lock_guard lock(db.mutex);
    detail::Statement select(db.handle, sql.toRawUTF8());
    for (int i = 0; i < formats.size(); ++i) select.text(i + 1, formats[i]);
    while (select.step()) out.push_back(select.text(0));
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
int SampleRepository::count(bool includeIgnored) const
{
    std::lock_guard lock(db.mutex);
    FilterSql parts;
    if (! includeIgnored) appendIgnore(parts, ignoredPathsLocked());
    appendStereoVisibility(parts);
    detail::Statement s(db.handle, ("SELECT COUNT(*) FROM samples" + parts.where).toRawUTF8());
    bindFilter(s, parts);
    return s.step() ? (int) s.integer(0) : 0;
}
int SampleRepository::countInFolder(const juce::String& folder, bool includeDiscContents) const
{
    if (folder.isEmpty()) return count();
    std::lock_guard lock(db.mutex);
    auto [lower,upper] = folderRange(folder);
    FilterSql parts;
    parts.where += " AND samples.path>=? AND samples.path<?";
    if (! includeDiscContents) parts.where += " AND samples.container_path=''";
    parts.params.emplace_back(lower); parts.params.emplace_back(upper);
    appendIgnore(parts, ignoredPathsLocked());
    appendStereoVisibility(parts);
    detail::Statement s(db.handle, ("SELECT COUNT(*) FROM samples" + parts.where).toRawUTF8());
    bindFilter(s, parts);
    return s.step() ? (int) s.integer(0) : 0;
}
std::vector<SampleRecord> SampleRepository::pageInFolder(const juce::String& folder,int limit,int offset) const
{
    if (folder.isEmpty()) return page(limit,offset);
    std::lock_guard lock(db.mutex);
    auto [lower,upper] = folderRange(folder);
    FilterSql parts;
    parts.where += " AND samples.path>=? AND samples.path<?";
    parts.params.emplace_back(lower); parts.params.emplace_back(upper);
    appendIgnore(parts, ignoredPathsLocked());
    appendStereoVisibility(parts);
    auto sql = "SELECT " + juce::String(columns) + " FROM samples" + parts.where
        + " ORDER BY name COLLATE NOCASE,id LIMIT ? OFFSET ?";
    detail::Statement s(db.handle,sql.toRawUTF8());
    const int next = bindFilter(s, parts);
    s.integer(next, juce::jlimit(0,50000,limit)); s.integer(next + 1, juce::jmax(0,offset));
    std::vector<SampleRecord> records;
    while (s.step()) records.push_back(read(s));
    return records;
}
LibraryPage SampleRepository::query(const LibraryFilter& filter, int limit, int offset) const
{
    std::lock_guard lock(db.mutex);
    const auto parts = buildFilterSql(filter, ignoredPathsLocked());
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
    const auto parts = buildFilterSql(filter, ignoredPathsLocked());
    auto sql = "SELECT samples.id FROM samples" + parts.where
        + " ORDER BY " + orderBy(filter.sort, filter.ascending);
    detail::Statement s(db.handle, sql.toRawUTF8()); bindFilter(s, parts);
    std::vector<int64_t> ids;
    while (s.step()) ids.push_back(s.integer(0));
    return ids;
}
std::vector<SampleRecord> SampleRepository::recordsByIds(const std::vector<int64_t>& ids) const
{    std::lock_guard lock(db.mutex);
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
std::vector<int64_t> SampleRepository::visibleIds(const std::vector<int64_t>& ids) const
{
    if (ids.empty()) return {};
    std::lock_guard lock(db.mutex);
    std::set<int64_t> visible;
    constexpr size_t chunkSize = 400;
    for (size_t start = 0; start < ids.size(); start += chunkSize)
    {
        const size_t end = std::min(ids.size(), start + chunkSize);
        juce::String placeholders;
        for (size_t i = start; i < end; ++i) placeholders += (i == start ? "?" : ",?");
        detail::Statement s(db.handle,
            ("SELECT id FROM samples WHERE id IN (" + placeholders + ") AND ignored=0"
             " AND id NOT IN (SELECT sample_id FROM stereo_pairs WHERE is_representative=0)").toRawUTF8());
        int index = 1;
        for (size_t i = start; i < end; ++i) s.integer(index++, ids[i]);
        while (s.step()) visible.insert(s.integer(0));
    }
    std::vector<int64_t> out;
    for (auto id : ids)
        if (visible.count(id) != 0) out.push_back(id);
    return out;
}
std::vector<int64_t> SampleRepository::nonDiscIds(const std::vector<int64_t>& ids) const
{
    if (ids.empty()) return {};
    std::lock_guard lock(db.mutex);
    std::set<int64_t> kept;
    constexpr size_t chunkSize = 400;
    for (size_t start = 0; start < ids.size(); start += chunkSize)
    {
        const size_t end = std::min(ids.size(), start + chunkSize);
        juce::String placeholders;
        for (size_t i = start; i < end; ++i) placeholders += (i == start ? "?" : ",?");
        detail::Statement s(db.handle,
            ("SELECT id FROM samples WHERE id IN (" + placeholders + ") AND container_path=''").toRawUTF8());
        int index = 1;
        for (size_t i = start; i < end; ++i) s.integer(index++, ids[i]);
        while (s.step()) kept.insert(s.integer(0));
    }
    std::vector<int64_t> out;
    for (auto id : ids) if (kept.count(id) != 0) out.push_back(id);
    return out;
}
std::map<int64_t, juce::String> SampleRepository::stereoPairPartners(const std::vector<int64_t>& ids) const
{
    std::map<int64_t, juce::String> out;
    if (ids.empty()) return out;
    std::lock_guard lock(db.mutex);
    constexpr size_t chunkSize = 400;
    for (size_t start = 0; start < ids.size(); start += chunkSize)
    {
        const size_t end = std::min(ids.size(), start + chunkSize);
        juce::String placeholders;
        for (size_t i = start; i < end; ++i) placeholders += (i == start ? "?" : ",?");
        detail::Statement s(db.handle,
            ("SELECT sp.representative_id,s.path FROM stereo_pairs sp"
             " JOIN samples s ON s.id=sp.partner_id"
             " WHERE sp.is_representative=1 AND sp.representative_id IN (" + placeholders + ")").toRawUTF8());
        int index = 1;
        for (size_t i = start; i < end; ++i) s.integer(index++, ids[i]);
        while (s.step()) out[s.integer(0)] = s.text(1);
    }
    return out;
}
int SampleRepository::countHash(const juce::String& hash) const
{
    std::lock_guard lock(db.mutex);
    detail::Statement s(db.handle, "SELECT COUNT(*) FROM samples WHERE hash=? AND hash<>'' AND ignored=0");
    s.text(1,hash);
    return s.step() ? (int) s.integer(0) : 0;
}
void SampleRepository::addRoot(const juce::String& path)
{
    std::lock_guard lock(db.mutex);
    detail::Statement s(db.handle,"INSERT OR IGNORE INTO roots(path) VALUES(?)"); s.text(1,path); s.step();
}
FolderRemoval SampleRepository::removeFolder(const juce::String& folder)
{
    FolderRemoval outcome;
    if (folder.trim().isEmpty()) return outcome;
    const auto canonical = pathrelocation::trimTrailingSeparators(juce::File(folder).getFullPathName());
    if (canonical.isEmpty()) return outcome;
    std::lock_guard lock(db.mutex);
    detail::exec(db.handle, "BEGIN IMMEDIATE;");
    try
    {
        juce::String registeredRoot;
        {
            detail::Statement roots(db.handle, "SELECT path FROM roots");
            while (roots.step())
                if (pathrelocation::trimTrailingSeparators(roots.text(0)) == canonical)
                { registeredRoot = roots.text(0); break; }
        }
        outcome.wasRoot = registeredRoot.isNotEmpty();
        const auto [lower, upper] = folderRange(canonical);
        {
            detail::Statement remove(db.handle, "DELETE FROM samples WHERE path>=? AND path<?");
            remove.text(1, lower); remove.text(2, upper); remove.step();
            outcome.rowsRemoved = sqlite3_changes(db.handle);
        }
        {
            // Ignore entries inside the removed subtree, plus the folder itself, no longer apply.
            detail::Statement remove(db.handle,
                "DELETE FROM ignored_paths WHERE path=? OR (path>=? AND path<?)");
            remove.text(1, canonical); remove.text(2, lower); remove.text(3, upper); remove.step();
        }
        {
            // A nested root cannot survive its parent leaving the library.
            detail::Statement remove(db.handle,
                "DELETE FROM roots WHERE path=? OR path=? OR (path>=? AND path<?)");
            remove.text(1, canonical);
            remove.text(2, registeredRoot.isNotEmpty() ? registeredRoot : canonical);
            remove.text(3, lower); remove.text(4, upper); remove.step();
        }
        {
            // Removing a folder also forgets the disc state of its images; adding the folder
            // again must re-index their contents even when the image bytes did not change.
            detail::Statement remove(db.handle,
                "DELETE FROM disc_index WHERE image_path>=? AND image_path<?");
            remove.text(1, lower); remove.text(2, upper); remove.step();
        }
        {
            // A removed folder stays out of every later scan, including a parent root or another
            // root that contains it. Explicitly adding it again clears this entry.
            detail::Statement ignore(db.handle,
                "INSERT OR REPLACE INTO ignored_paths(path,is_folder,created_at) VALUES(?,1,?)");
            ignore.text(1, canonical); ignore.integer(2, juce::Time::currentTimeMillis()); ignore.step();
        }
        detail::exec(db.handle, "COMMIT;");
    }
    catch (...)
    {
        try { detail::exec(db.handle, "ROLLBACK;"); } catch (...) {}
        throw;
    }
    return outcome;
}
int SampleRepository::removeSamplesWithFormats(const juce::StringArray& formats)
{
    juce::StringArray cleaned;
    for (const auto& format : formats)
        if (format.trim().isNotEmpty()) cleaned.addIfNotAlreadyThere(format.trim().toUpperCase());
    if (cleaned.isEmpty()) return 0;
    std::lock_guard lock(db.mutex);
    juce::String placeholders;
    for (int i = 0; i < cleaned.size(); ++i) placeholders << (i == 0 ? "?" : ",?");
    detail::exec(db.handle, "BEGIN IMMEDIATE;");
    try
    {
        const auto sql = "DELETE FROM samples WHERE UPPER(format) IN (" + placeholders + ")";
        detail::Statement remove(db.handle, sql.toRawUTF8());
        for (int i = 0; i < cleaned.size(); ++i) remove.text(i + 1, cleaned[i]);
        remove.step();
        const int removed = sqlite3_changes(db.handle);
        detail::exec(db.handle, "COMMIT;");
        return removed;
    }
    catch (...)
    {
        try { detail::exec(db.handle, "ROLLBACK;"); } catch (...) {}
        throw;
    }
}
std::vector<juce::String> SampleRepository::discPathsLocked(const juce::String& imagePath) const
{
    std::vector<juce::String> out;
    detail::Statement s(db.handle, "SELECT path FROM samples WHERE container_path=? ORDER BY path");
    s.text(1, imagePath);
    while (s.step()) out.push_back(s.text(0));
    return out;
}
std::optional<DiscIndexState> SampleRepository::discIndexState(const juce::String& imagePath) const
{
    std::lock_guard lock(db.mutex);
    detail::Statement s(db.handle,
        "SELECT image_size,image_modified,entry_count FROM disc_index WHERE image_path=?");
    s.text(1, imagePath);
    if (! s.step()) return std::nullopt;
    return DiscIndexState{ s.integer(0), s.integer(1), s.integer(2) };
}
void SampleRepository::replaceDiscContents(const juce::String& imagePath, int64_t imageSize,
                                           int64_t imageModified, const std::vector<SampleRecord>& rows,
                                           bool removeMissing)
{
    if (imagePath.isEmpty()) return;
    std::lock_guard lock(db.mutex);
    detail::exec(db.handle, "BEGIN IMMEDIATE;");
    try
    {
        const auto existing = discPathsLocked(imagePath);
        std::set<juce::String> wanted;
        for (const auto& row : rows) wanted.insert(row.filePath);
        detail::Statement remove(db.handle, "DELETE FROM samples WHERE path=?");
        if (removeMissing)
            for (const auto& path : existing)
                if (wanted.find(path) == wanted.end())
                { remove.reset(); remove.text(1, path); remove.step(); }
        const auto ignoredEntries = ignoredPathsLocked();
        for (const auto& row : rows) upsertLocked(row, &ignoredEntries);
        detail::Statement state(db.handle,
            "INSERT INTO disc_index(image_path,image_size,image_modified,entry_count,indexed_at)"
            " VALUES(?,?,?,?,?) ON CONFLICT(image_path) DO UPDATE SET"
            " image_size=excluded.image_size,image_modified=excluded.image_modified,"
            " entry_count=excluded.entry_count,indexed_at=excluded.indexed_at");
        state.text(1, imagePath); state.integer(2, imageSize); state.integer(3, imageModified);
        state.integer(4, (int64_t) rows.size()); state.integer(5, juce::Time::currentTimeMillis());
        state.step();
        detail::exec(db.handle, "COMMIT;");
    }
    catch (...)
    {
        try { detail::exec(db.handle, "ROLLBACK;"); } catch (...) {}
        throw;
    }
}
void SampleRepository::deleteDiscContents(const juce::String& imagePath)
{
    if (imagePath.isEmpty()) return;
    std::lock_guard lock(db.mutex);
    detail::exec(db.handle, "BEGIN IMMEDIATE;");
    try
    {
        detail::Statement remove(db.handle, "DELETE FROM samples WHERE container_path=?");
        remove.text(1, imagePath); remove.step();
        detail::Statement state(db.handle, "DELETE FROM disc_index WHERE image_path=?");
        state.text(1, imagePath); state.step();
        detail::exec(db.handle, "COMMIT;");
    }
    catch (...)
    {
        try { detail::exec(db.handle, "ROLLBACK;"); } catch (...) {}
        throw;
    }
}
void SampleRepository::invalidateDiscState(const juce::String& imagePath)
{
    if (imagePath.isEmpty()) return;
    std::lock_guard lock(db.mutex);
    detail::Statement remove(db.handle, "DELETE FROM disc_index WHERE image_path=?");
    remove.text(1, imagePath); remove.step();
}
int SampleRepository::pruneDiscContents()
{
    std::lock_guard lock(db.mutex);
    detail::exec(db.handle, "BEGIN IMMEDIATE;");
    try
    {
        // Contents of a disc image mirror the image row's missing flag. Unplugging or deleting an
        // image never drops tags/stars; an explicit Remove of the image or its folder does.
        detail::exec(db.handle,
            "DELETE FROM samples WHERE container_path<>'' AND NOT EXISTS("
            "SELECT 1 FROM samples image WHERE image.path=samples.container_path)");
        const int removed = sqlite3_changes(db.handle);
        detail::exec(db.handle,
            "UPDATE samples SET missing=(SELECT image.missing FROM samples image"
            " WHERE image.path=samples.container_path) WHERE container_path<>'' AND EXISTS("
            "SELECT 1 FROM samples image WHERE image.path=samples.container_path)"
            " AND missing<>(SELECT image.missing FROM samples image WHERE image.path=samples.container_path)");
        detail::exec(db.handle,
            "DELETE FROM disc_index WHERE NOT EXISTS("
            "SELECT 1 FROM samples image WHERE image.path=disc_index.image_path)");
        detail::exec(db.handle, "COMMIT;");
        return removed;
    }
    catch (...)
    {
        try { detail::exec(db.handle, "ROLLBACK;"); } catch (...) {}
        throw;
    }
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
        struct Row { int64_t id = 0; juce::String name, path, containerPath; };
        detail::Statement rows(db.handle,
            "SELECT id,name,path,container_path FROM samples WHERE id>? ORDER BY id LIMIT 2000");
        detail::Statement update(db.handle,
            "UPDATE OR IGNORE samples SET path=?, search_key=?, container_path=? WHERE id=?");
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
            while (rows.step())
                batch.push_back({ rows.integer(0), rows.text(1), rows.text(2), rows.text(3) });
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
                const auto newContainer = ! row.containerPath.isEmpty()
                        && pathrelocation::isUnder(row.containerPath, best->oldRoot)
                    ? pathrelocation::remap(row.containerPath, best->oldRoot, best->newRoot)
                    : row.containerPath;
                update.reset(); update.text(1, newPath);
                update.text(2, searchKey(row.name, newPath)); update.text(3, newContainer);
                update.integer(4, row.id); update.step();
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
        {
            // Image state keys are absolute paths too; move them with their root so an unchanged
            // image is not walked again after relocation.
            std::vector<std::pair<juce::String, juce::String>> states;
            {
                detail::Statement read(db.handle, "SELECT image_path FROM disc_index");
                while (read.step())
                {
                    const auto oldImage = read.text(0);
                    const RootRelocation* best = nullptr;
                    for (const auto& relocation : relocations)
                        if (pathrelocation::isUnder(oldImage, relocation.oldRoot)
                            && (best == nullptr || relocation.oldRoot.length() > best->oldRoot.length()))
                            best = &relocation;
                    if (best == nullptr) continue;
                    const auto newImage = pathrelocation::remap(oldImage, best->oldRoot, best->newRoot);
                    if (newImage != oldImage) states.emplace_back(newImage, oldImage);
                }
            }
            detail::Statement move(db.handle,
                "INSERT OR REPLACE INTO disc_index(image_path,image_size,image_modified,entry_count,indexed_at)"
                " SELECT ?,image_size,image_modified,entry_count,indexed_at FROM disc_index WHERE image_path=?");
            detail::Statement remove(db.handle, "DELETE FROM disc_index WHERE image_path=?");
            for (const auto& [newImage, oldImage] : states)
            {
                move.reset(); move.text(1, newImage); move.text(2, oldImage); move.step();
                remove.reset(); remove.text(1, oldImage); remove.step();
            }
        }
        // Ignore entries are absolute paths too. Move them with their root so a hidden folder
        // stays hidden after the catalog is relocated.
        std::vector<IgnoredPath> ignoredRows;
        {
            detail::Statement read(db.handle, "SELECT path,is_folder,created_at FROM ignored_paths");
            while (read.step())
                ignoredRows.push_back({ read.text(0), read.integer(1) != 0, read.integer(2) });
        }
        detail::Statement deleteIgnore(db.handle, "DELETE FROM ignored_paths WHERE path=?");
        detail::Statement insertIgnore(db.handle,
            "INSERT OR IGNORE INTO ignored_paths(path,is_folder,created_at) VALUES(?,?,?)");
        for (const auto& entry : ignoredRows)
        {
            const RootRelocation* best = nullptr;
            for (const auto& relocation : relocations)
                if (pathrelocation::isUnder(entry.path, relocation.oldRoot)
                    && (best == nullptr || relocation.oldRoot.length() > best->oldRoot.length()))
                    best = &relocation;
            if (best == nullptr) continue;
            const auto newPath = pathrelocation::remap(entry.path, best->oldRoot, best->newRoot);
            if (newPath == entry.path) continue;
            deleteIgnore.reset(); deleteIgnore.text(1, entry.path); deleteIgnore.step();
            insertIgnore.reset(); insertIgnore.text(1, newPath);
            insertIgnore.integer(2, entry.folder ? 1 : 0);
            insertIgnore.integer(3, entry.createdAt);
            insertIgnore.step();
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
int SampleRepository::assignTag(const std::vector<int64_t>& sampleIds, int64_t tagId)
{
    return assignTagBatch(sampleIds, tagId, {}).added;
}
SampleRepository::TagAssignmentOutcome SampleRepository::assignTagBatch(
    const std::vector<int64_t>& sampleIds, int64_t tagId, const std::function<bool()>& isCancelled)
{
    TagAssignmentOutcome outcome;
    if (sampleIds.empty() || tagId <= 0) return outcome;
    std::lock_guard lock(db.mutex);
    detail::exec(db.handle, "BEGIN IMMEDIATE;");
    try
    {
        detail::Statement s(db.handle,
            "INSERT OR IGNORE INTO sample_tags(sample_id,tag_id)"
            " SELECT ?,? WHERE EXISTS(SELECT 1 FROM samples WHERE id=?)");
        for (auto id : sampleIds)
        {
            if (isCancelled && isCancelled())
            {
                detail::exec(db.handle, "ROLLBACK;");
                outcome.added = 0;
                outcome.cancelled = true;
                return outcome;
            }
            if (id <= 0) continue;
            s.integer(1, id); s.integer(2, tagId); s.integer(3, id); s.step();
            if (sqlite3_changes(db.handle) > 0) ++outcome.added;
            s.reset();
        }
        detail::exec(db.handle, "COMMIT;");
    }
    catch (...)
    {
        try { detail::exec(db.handle, "ROLLBACK;"); } catch (...) {}
        throw;
    }
    return outcome;
}
SampleRepository::TagAssignmentOutcome SampleRepository::assignTagPairs(
    const std::vector<TagPair>& pairs, const std::function<bool()>& isCancelled)
{
    TagAssignmentOutcome outcome;
    if (pairs.empty()) return outcome;
    std::lock_guard lock(db.mutex);
    detail::exec(db.handle, "BEGIN IMMEDIATE;");
    try
    {
        detail::Statement s(db.handle,
            "INSERT OR IGNORE INTO sample_tags(sample_id,tag_id)"
            " SELECT ?,? WHERE EXISTS(SELECT 1 FROM samples WHERE id=?)"
            " AND EXISTS(SELECT 1 FROM tags WHERE id=?)");
        for (const auto& pair : pairs)
        {
            if (isCancelled && isCancelled())
            {
                detail::exec(db.handle, "ROLLBACK;");
                outcome.added = 0;
                outcome.cancelled = true;
                return outcome;
            }
            if (pair.sampleId <= 0 || pair.tagId <= 0) continue;
            s.integer(1, pair.sampleId); s.integer(2, pair.tagId);
            s.integer(3, pair.sampleId); s.integer(4, pair.tagId);
            s.step();
            if (sqlite3_changes(db.handle) > 0) ++outcome.added;
            s.reset();
        }
        detail::exec(db.handle, "COMMIT;");
    }
    catch (...)
    {
        try { detail::exec(db.handle, "ROLLBACK;"); } catch (...) {}
        throw;
    }
    return outcome;
}
SampleRepository::TagAssignmentOutcome SampleRepository::clearTagsBatch(
    const std::vector<int64_t>& sampleIds, const std::function<bool()>& isCancelled)
{
    TagAssignmentOutcome outcome;
    if (sampleIds.empty()) return outcome;
    std::lock_guard lock(db.mutex);
    detail::exec(db.handle, "BEGIN IMMEDIATE;");
    try
    {
        detail::Statement s(db.handle, "DELETE FROM sample_tags WHERE sample_id=?");
        for (auto id : sampleIds)
        {
            if (isCancelled && isCancelled())
            {
                detail::exec(db.handle, "ROLLBACK;");
                outcome.added = 0;
                outcome.cancelled = true;
                return outcome;
            }
            if (id <= 0) continue;
            s.integer(1, id); s.step();
            const int removed = sqlite3_changes(db.handle);
            if (removed > 0) outcome.added += removed;
            s.reset();
        }
        detail::exec(db.handle, "COMMIT;");
    }
    catch (...)
    {
        try { detail::exec(db.handle, "ROLLBACK;"); } catch (...) {}
        throw;
    }
    return outcome;
}
int SampleRepository::countTaggedInFolder(const juce::String& folder) const
{
    if (folder.isEmpty()) return 0;
    std::lock_guard lock(db.mutex);
    auto [lower, upper] = folderRange(folder);
    FilterSql parts;
    parts.where += " AND samples.path>=? AND samples.path<?";
    parts.params.emplace_back(lower); parts.params.emplace_back(upper);
    appendIgnore(parts, ignoredPathsLocked());
    appendStereoVisibility(parts);
    detail::Statement s(db.handle,
        ("SELECT COUNT(DISTINCT samples.id) FROM samples JOIN sample_tags st ON st.sample_id=samples.id"
         + parts.where).toRawUTF8());
    bindFilter(s, parts);
    return s.step() ? (int) s.integer(0) : 0;
}
int SampleRepository::countTaggedSamples(const std::vector<int64_t>& sampleIds) const
{
    if (sampleIds.empty()) return 0;
    auto ids = sampleIds;
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    std::lock_guard lock(db.mutex);
    int count = 0;
    constexpr size_t chunkSize = 400;
    for (size_t start = 0; start < ids.size(); start += chunkSize)
    {
        const size_t end = std::min(ids.size(), start + chunkSize);
        juce::String placeholders;
        for (size_t i = start; i < end; ++i) placeholders += (i == start ? "?" : ",?");
        detail::Statement s(db.handle,
            ("SELECT COUNT(DISTINCT sample_id) FROM sample_tags WHERE sample_id IN ("
             + placeholders + ")").toRawUTF8());
        int index = 1;
        for (size_t i = start; i < end; ++i) s.integer(index++, ids[i]);
        if (s.step()) count += (int) s.integer(0);
    }
    return count;
}
void SampleRepository::recordRecentTag(int64_t tagId)
{
    if (tagId <= 0) return;
    std::lock_guard lock(db.mutex);
    detail::exec(db.handle, "BEGIN IMMEDIATE;");
    try
    {
        detail::Statement exists(db.handle, "SELECT 1 FROM tags WHERE id=?");
        exists.integer(1, tagId);
        if (! exists.step())
        {
            detail::exec(db.handle, "COMMIT;");
            return;
        }
        detail::Statement remove(db.handle, "DELETE FROM recent_tags WHERE tag_id=?");
        remove.integer(1, tagId); remove.step();
        detail::Statement insert(db.handle,
            "INSERT INTO recent_tags(tag_id,used_at,seq) VALUES(?,?,"
            "COALESCE((SELECT MAX(seq) FROM recent_tags),0)+1)");
        insert.integer(1, tagId); insert.integer(2, juce::Time::currentTimeMillis()); insert.step();
        detail::exec(db.handle,
            "DELETE FROM recent_tags WHERE tag_id NOT IN"
            "(SELECT tag_id FROM recent_tags ORDER BY seq DESC LIMIT 5);");
        detail::exec(db.handle, "COMMIT;");
    }
    catch (...)
    {
        try { detail::exec(db.handle, "ROLLBACK;"); } catch (...) {}
        throw;
    }
}
std::vector<Tag> SampleRepository::recentTags(int limit) const
{
    std::lock_guard lock(db.mutex);
    detail::Statement s(db.handle,
        "SELECT t.id,t.name,t.colour,t.builtin FROM recent_tags r"
        " JOIN tags t ON t.id=r.tag_id ORDER BY r.seq DESC LIMIT ?");
    s.integer(1, juce::jlimit(0, 5, limit));
    std::vector<Tag> out;
    while (s.step()) out.push_back(readTag(s, 0));
    return out;
}
std::vector<int64_t> SampleRepository::idsInFolder(const juce::String& folder, bool includeDiscContents) const
{
    std::vector<int64_t> out;
    if (folder.isEmpty()) return out;
    std::lock_guard lock(db.mutex);
    auto [lower, upper] = folderRange(folder);
    FilterSql parts;
    parts.where += " AND samples.path>=? AND samples.path<?";
    if (! includeDiscContents) parts.where += " AND samples.container_path=''";
    parts.params.emplace_back(lower); parts.params.emplace_back(upper);
    appendIgnore(parts, ignoredPathsLocked());
    appendStereoVisibility(parts);
    detail::Statement s(db.handle,
        ("SELECT samples.id FROM samples" + parts.where + " ORDER BY samples.id").toRawUTF8());
    bindFilter(s, parts);
    while (s.step()) out.push_back(s.integer(0));
    return out;
}
int SampleRepository::countWithTagInFolder(const juce::String& folder, int64_t tagId) const
{
    if (folder.isEmpty() || tagId <= 0) return 0;
    std::lock_guard lock(db.mutex);
    auto [lower, upper] = folderRange(folder);
    FilterSql parts;
    parts.where += " AND samples.path>=? AND samples.path<? AND st.tag_id=?";
    parts.params.emplace_back(lower); parts.params.emplace_back(upper);
    parts.params.emplace_back(tagId);
    appendIgnore(parts, ignoredPathsLocked());
    appendStereoVisibility(parts);
    detail::Statement s(db.handle,
        ("SELECT COUNT(*) FROM samples JOIN sample_tags st ON st.sample_id=samples.id"
         + parts.where).toRawUTF8());
    bindFilter(s, parts);
    return s.step() ? (int) s.integer(0) : 0;
}
void SampleRepository::updateDuplicateRepresentativesLocked(const std::vector<int64_t>& sampleIds)
{
    if (sampleIds.empty()) return;
    std::set<juce::String> affectedHashes;
    detail::Statement hashStmt(db.handle, "SELECT group_hash FROM duplicates WHERE sample_id=?");
    for (auto id : sampleIds)
    {
        hashStmt.integer(1, id);
        if (hashStmt.step())
        {
            auto h = hashStmt.text(0);
            if (h.isNotEmpty()) affectedHashes.insert(h);
        }
        hashStmt.reset();
    }
    if (affectedHashes.empty()) return;

    struct Member { int64_t id = 0; bool starred = false, missing = false; bool failed = false; int pathLength = 0; };
    detail::Statement select(db.handle,
        "SELECT id,path,starred,missing,error FROM samples WHERE hash=? AND ignored=0"
        " AND id NOT IN (SELECT sample_id FROM stereo_pairs WHERE is_representative=0)");
    detail::Statement insert(db.handle,
        "INSERT OR REPLACE INTO duplicates(sample_id,representative_id,group_hash,group_size,is_representative)"
        " VALUES(?,?,?,?,?)");
    detail::Statement del(db.handle, "DELETE FROM duplicates WHERE group_hash=?");

    for (const auto& hash : affectedHashes)
    {
        select.reset(); select.text(1, hash);
        std::vector<Member> members;
        while (select.step())
            members.push_back({ select.integer(0), select.integer(2) != 0, select.integer(3) != 0,
                                ! select.text(4).isEmpty(), select.text(1).length() });
        if (members.size() < 2)
        {
            del.reset(); del.text(1, hash); del.step();
            continue;
        }
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
}
void SampleRepository::updateDuplicateRepresentative(int64_t sampleId)
{
    updateDuplicateRepresentatives({ sampleId });
}
void SampleRepository::updateDuplicateRepresentatives(const std::vector<int64_t>& sampleIds)
{
    std::lock_guard lock(db.mutex);
    detail::exec(db.handle, "BEGIN IMMEDIATE;");
    try
    {
        updateDuplicateRepresentativesLocked(sampleIds);
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
        updateDuplicateRepresentativesLocked(sampleIds);
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
        {
            bool pairKeysReady = false;
            {
                detail::Statement marker(db.handle,
                    "SELECT value FROM smart_meta WHERE key='pair_keys_v1'");
                pairKeysReady = marker.step() && marker.integer(0) != 0;
            }
            if (! pairKeysReady)
            {
                // Existing catalogs predate pair_key/pair_side. Backfill only mono names that
                // actually carry an L/R marker, then remember the pass so later refreshes skip it.
                detail::Statement select(db.handle,
                    "SELECT id,name,path,channels,rate FROM samples"
                    " WHERE pair_key='' AND channels=1 AND rate>0 AND ("
                    " lower(name) LIKE '%-l.%' OR lower(name) LIKE '%-r.%'"
                    " OR lower(name) LIKE '%_l.%' OR lower(name) LIKE '%_r.%'"
                    " OR lower(name) LIKE '%.l.%' OR lower(name) LIKE '%.r.%'"
                    " OR lower(name) LIKE '% l.%' OR lower(name) LIKE '% r.%')");
                detail::Statement update(db.handle,
                    "UPDATE samples SET pair_key=?,pair_side=? WHERE id=?");
                while (select.step())
                {
                    SampleRecord probe;
                    probe.fileName = select.text(1);
                    probe.filePath = select.text(2);
                    probe.numChannels = (int) select.integer(3);
                    probe.sampleRate = select.real(4);
                    const auto [key, side] = makePairKey(probe);
                    if (key.isEmpty()) continue;
                    update.reset(); update.text(1, key); update.integer(2, side);
                    update.integer(3, select.integer(0)); update.step();
                }
                detail::exec(db.handle,
                    "INSERT OR REPLACE INTO smart_meta(key,value) VALUES('pair_keys_v1',1);");
            }
        }
        detail::exec(db.handle, "DELETE FROM stereo_pairs;");
        constexpr auto stereoPairSql =
            "INSERT OR IGNORE INTO stereo_pairs(sample_id,representative_id,partner_id,is_representative) "
            "SELECT l.id,l.id,r.id,1 FROM samples l JOIN samples r ON r.pair_key=l.pair_key "
            "WHERE l.pair_key<>'' AND l.pair_side=-1 AND r.pair_side=1"
            " AND l.channels=1 AND r.channels=1 AND l.rate=r.rate AND l.duration>0"
            " AND abs(l.duration-r.duration)<0.001 AND l.ignored=0 AND r.ignored=0"
            " AND l.missing=0 AND r.missing=0 "
            "UNION ALL "
            "SELECT r.id,l.id,r.id,0 FROM samples l JOIN samples r ON r.pair_key=l.pair_key "
            "WHERE l.pair_key<>'' AND l.pair_side=-1 AND r.pair_side=1"
            " AND l.channels=1 AND r.channels=1 AND l.rate=r.rate AND l.duration>0"
            " AND abs(l.duration-r.duration)<0.001 AND l.ignored=0 AND r.ignored=0"
            " AND l.missing=0 AND r.missing=0;";
        detail::exec(db.handle, stereoPairSql);
        detail::exec(db.handle, "DELETE FROM duplicates;");
        detail::Statement groups(db.handle,
            "SELECT hash FROM samples WHERE hash<>'' AND size>0 AND ignored=0"
            " AND id NOT IN (SELECT sample_id FROM stereo_pairs WHERE is_representative=0)"
            " GROUP BY hash HAVING COUNT(*)>1");
        std::vector<juce::String> hashes;
        while (groups.step()) hashes.push_back(groups.text(0));
        struct Member { int64_t id = 0; bool starred = false, missing = false; bool failed = false; int pathLength = 0; };
        detail::Statement select(db.handle,
            "SELECT id,path,starred,missing,error FROM samples WHERE hash=? AND ignored=0"
            " AND id NOT IN (SELECT sample_id FROM stereo_pairs WHERE is_representative=0)");
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
namespace {
SmartOverrideMap readOverrides(sqlite3* h, int64_t sampleId)
{
    detail::Statement s(h, "SELECT field,cleared,not_applicable,text_value,numeric_value,aux_value"
                           " FROM smart_overrides WHERE sample_id=?");
    s.integer(1, sampleId);
    SmartOverrideMap out;
    while (s.step())
    {
        auto field = smartFieldFromKey(s.text(0));
        if (! field) continue;
        SmartOverride value;
        value.cleared = s.integer(1) != 0;
        value.notApplicable = s.integer(2) != 0;
        value.textValue = s.text(3);
        value.numericValue = s.real(4);
        value.auxValue = s.real(5);
        out[*field] = value;
    }
    return out;
}
std::vector<SmartCandidate> readCandidates(sqlite3* h, int64_t sampleId)
{
    detail::Statement s(h,
        "SELECT field,source,status,detail,text_value,numeric_value,aux_value,"
        "extractor_id,extractor_version,raw_value,input_kind,input_path,fingerprint,limitation,"
        "variable_tempo,extracted_at"
        " FROM smart_candidates WHERE sample_id=? ORDER BY field,source,detail");
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
        c.auxValue = s.real(6);
        c.extractorId = s.text(7);
        c.extractorVersion = (int) s.integer(8);
        c.rawValue = s.text(9);
        c.inputKind = s.text(10);
        c.inputPath = s.text(11);
        c.fingerprint = s.text(12);
        c.limitation = s.text(13);
        c.variableTempo = s.integer(14) != 0;
        c.extractedAt = s.integer(15);
        out.push_back(c);
    }
    return out;
}
std::vector<SmartInput> readInputs(sqlite3* h, int64_t sampleId)
{
    detail::Statement s(h, "SELECT kind,path,size,modified FROM smart_inputs"
                           " WHERE sample_id=? ORDER BY kind,path");
    s.integer(1, sampleId);
    std::vector<SmartInput> out;
    while (s.step()) out.push_back({ s.text(0), s.text(1), s.integer(2), s.integer(3) });
    return out;
}
std::vector<SmartExtraction> readExtractions(sqlite3* h, int64_t sampleId)
{
    detail::Statement s(h, "SELECT source,extractor_version,status,error,updated_at"
                           " FROM smart_extractions WHERE sample_id=? ORDER BY source");
    s.integer(1, sampleId);
    std::vector<SmartExtraction> out;
    while (s.step()) out.push_back({ s.text(0), (int) s.integer(1), s.text(2), s.text(3), s.integer(4) });
    return out;
}
std::optional<SmartResult> readResult(sqlite3* h, int64_t sampleId)
{
    detail::Statement s(h,
        "SELECT type,structure,bpm,has_bpm,key_tonic,key_mode,root_note,"
        "type_state,structure_state,bpm_state,key_state,root_state,"
        "pitch_midi,pitch_cents,pitch_state,"
        "type_source,structure_source,bpm_source,key_source,pitch_source,root_source,"
        "type_override,structure_override,bpm_override,key_override,pitch_override,root_override,"
        "explanation,revision,generation,pipeline_state,extractor_version,resolver_version,dsp_version,"
        "analyzed_at,error_code,error_detail,updated_at"
        " FROM smart_results WHERE sample_id=?");
    s.integer(1, sampleId);
    if (! s.step()) return std::nullopt;
    SmartResult r;
    r.type = s.text(0); r.structure = s.text(1);
    r.bpm = s.real(2); r.hasBpm = s.integer(3) != 0;
    r.keyTonic = (int) s.integer(4); r.keyMode = s.text(5); r.rootNote = (int) s.integer(6);
    r.typeState = smartStateFromKey(s.text(7)).value_or(SmartState::Unknown);
    r.structureState = smartStateFromKey(s.text(8)).value_or(SmartState::Unknown);
    r.bpmState = smartStateFromKey(s.text(9)).value_or(SmartState::Unknown);
    r.keyState = smartStateFromKey(s.text(10)).value_or(SmartState::Unknown);
    r.rootState = smartStateFromKey(s.text(11)).value_or(SmartState::Unknown);
    r.pitchMidi = (int) s.integer(12); r.pitchCents = s.real(13);
    r.pitchState = smartStateFromKey(s.text(14)).value_or(SmartState::Unknown);
    r.typeSource = s.text(15); r.structureSource = s.text(16); r.bpmSource = s.text(17);
    r.keySource = s.text(18); r.pitchSource = s.text(19); r.rootSource = s.text(20);
    r.typeOverride = overrideStateFromKey(s.text(21)).value_or(OverrideState::None);
    r.structureOverride = overrideStateFromKey(s.text(22)).value_or(OverrideState::None);
    r.bpmOverride = overrideStateFromKey(s.text(23)).value_or(OverrideState::None);
    r.keyOverride = overrideStateFromKey(s.text(24)).value_or(OverrideState::None);
    r.pitchOverride = overrideStateFromKey(s.text(25)).value_or(OverrideState::None);
    r.rootOverride = overrideStateFromKey(s.text(26)).value_or(OverrideState::None);
    r.explanation = s.text(27);
    r.revision = s.integer(28); r.generation = s.integer(29);
    r.pipelineState = smartPipelineStateFromKey(s.text(30)).value_or(SmartPipelineState::NotAnalyzed);
    r.extractorVersion = (int) s.integer(31); r.resolverVersion = (int) s.integer(32);
    r.dspVersion = (int) s.integer(33);
    r.analyzedAt = s.integer(34); r.errorCode = s.text(35); r.errorDetail = s.text(36);
    r.hasPitch = r.pitchMidi >= 0 && r.pitchState != SmartState::Unknown
                 && r.pitchState != SmartState::NotApplicable;
    return r;
}
void writeCandidates(sqlite3* h, int64_t sampleId, const std::vector<SmartCandidate>& candidates)
{
    {
        detail::Statement remove(h, "DELETE FROM smart_candidates WHERE sample_id=?");
        remove.integer(1, sampleId); remove.step();
    }
    detail::Statement insert(h,
        "INSERT OR REPLACE INTO smart_candidates"
        "(sample_id,field,source,status,detail,text_value,numeric_value,aux_value,"
        "extractor_id,extractor_version,raw_value,input_kind,input_path,fingerprint,limitation,"
        "variable_tempo,extracted_at)"
        " VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
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
        insert.real(8, c.auxValue);
        insert.text(9, c.extractorId);
        insert.integer(10, c.extractorVersion);
        insert.text(11, c.rawValue);
        insert.text(12, c.inputKind);
        insert.text(13, c.inputPath);
        insert.text(14, c.fingerprint);
        insert.text(15, c.limitation);
        insert.integer(16, c.variableTempo ? 1 : 0);
        insert.integer(17, c.extractedAt);
        insert.step();
    }
}
void writeInputs(sqlite3* h, int64_t sampleId, const std::vector<SmartInput>& inputs)
{
    {
        detail::Statement remove(h, "DELETE FROM smart_inputs WHERE sample_id=?");
        remove.integer(1, sampleId); remove.step();
    }
    detail::Statement insert(h,
        "INSERT OR REPLACE INTO smart_inputs(sample_id,kind,path,size,modified) VALUES(?,?,?,?,?)");
    for (const auto& input : inputs)
    {
        insert.reset(); insert.integer(1, sampleId); insert.text(2, input.kind);
        insert.text(3, input.path); insert.integer(4, input.size); insert.integer(5, input.modified);
        insert.step();
    }
}
void writeExtractions(sqlite3* h, int64_t sampleId, const std::vector<SmartExtraction>& extractions)
{
    {
        detail::Statement remove(h, "DELETE FROM smart_extractions WHERE sample_id=?");
        remove.integer(1, sampleId); remove.step();
    }
    detail::Statement insert(h,
        "INSERT OR REPLACE INTO smart_extractions"
        "(sample_id,source,extractor_version,status,error,updated_at) VALUES(?,?,?,?,?,?)");
    for (const auto& e : extractions)
    {
        insert.reset(); insert.integer(1, sampleId); insert.text(2, e.source);
        insert.integer(3, e.extractorVersion); insert.text(4, e.status);
        insert.text(5, e.error); insert.integer(6, e.updatedAt);
        insert.step();
    }
}
void writeResult(sqlite3* h, int64_t sampleId, const SmartResult& r)
{
    detail::Statement s(h,
        "INSERT OR REPLACE INTO smart_results"
        "(sample_id,type,structure,bpm,has_bpm,key_tonic,key_mode,root_note,"
        "type_state,structure_state,bpm_state,key_state,root_state,"
        "pitch_midi,pitch_cents,pitch_state,"
        "type_source,structure_source,bpm_source,key_source,pitch_source,root_source,"
        "type_override,structure_override,bpm_override,key_override,pitch_override,root_override,"
        "explanation,revision,generation,pipeline_state,extractor_version,resolver_version,dsp_version,"
        "analyzed_at,error_code,error_detail,updated_at)"
        " VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    s.integer(1, sampleId);
    s.text(2, r.type); s.text(3, r.structure);
    s.real(4, r.bpm); s.integer(5, r.hasBpm ? 1 : 0);
    s.integer(6, r.keyTonic); s.text(7, r.keyMode); s.integer(8, r.rootNote);
    s.text(9, smartStateKey(r.typeState)); s.text(10, smartStateKey(r.structureState));
    s.text(11, smartStateKey(r.bpmState)); s.text(12, smartStateKey(r.keyState));
    s.text(13, smartStateKey(r.rootState));
    s.integer(14, r.pitchMidi); s.real(15, r.pitchCents); s.text(16, smartStateKey(r.pitchState));
    s.text(17, r.typeSource); s.text(18, r.structureSource); s.text(19, r.bpmSource);
    s.text(20, r.keySource); s.text(21, r.pitchSource); s.text(22, r.rootSource);
    s.text(23, overrideStateKey(r.typeOverride)); s.text(24, overrideStateKey(r.structureOverride));
    s.text(25, overrideStateKey(r.bpmOverride)); s.text(26, overrideStateKey(r.keyOverride));
    s.text(27, overrideStateKey(r.pitchOverride)); s.text(28, overrideStateKey(r.rootOverride));
    s.text(29, r.explanation);
    s.integer(30, r.revision); s.integer(31, r.generation);
    s.text(32, smartPipelineStateKey(r.pipelineState));
    s.integer(33, r.extractorVersion); s.integer(34, r.resolverVersion); s.integer(35, r.dspVersion);
    s.integer(36, r.analyzedAt); s.text(37, r.errorCode); s.text(38, r.errorDetail);
    s.integer(39, juce::Time::currentTimeMillis());
    s.step();
}
int64_t nextGeneration(sqlite3* h)
{
    detail::exec(h, "INSERT OR IGNORE INTO smart_meta(key,value) VALUES('generation',0);");
    detail::exec(h, "UPDATE smart_meta SET value=value+1 WHERE key='generation';");
    detail::Statement s(h, "SELECT value FROM smart_meta WHERE key='generation'");
    return s.step() ? s.integer(0) : 0;
}
void clearFailure(sqlite3* h, int64_t sampleId)
{
    detail::Statement s(h, "DELETE FROM smart_failures WHERE sample_id=?");
    s.integer(1, sampleId); s.step();
}
void writeFailure(sqlite3* h, int64_t sampleId, const juce::String& code, const juce::String& message)
{
    detail::Statement s(h,
        "INSERT INTO smart_failures(sample_id,code,message,attempts,updated_at) VALUES(?,?,?,1,?)"
        " ON CONFLICT(sample_id) DO UPDATE SET code=excluded.code,message=excluded.message,"
        "attempts=smart_failures.attempts+1,updated_at=excluded.updated_at");
    s.integer(1, sampleId); s.text(2, code); s.text(3, message);
    s.integer(4, juce::Time::currentTimeMillis()); s.step();
}
// Re-resolves a stored result from its candidates and the current overrides. Used by override
// edits, so a display value never waits for the next background pass.
SmartResult resolvedFromStored(sqlite3* h, int64_t sampleId, bool* found)
{
    auto candidates = readCandidates(h, sampleId);
    auto overrides = readOverrides(h, sampleId);
    auto result = resolveSmart(candidates, overrides);
    if (auto existing = readResult(h, sampleId))
    {
        *found = true;
        result.revision = existing->revision;
        result.generation = existing->generation;
        result.pipelineState = existing->pipelineState;
        result.extractorVersion = existing->extractorVersion;
        result.resolverVersion = existing->resolverVersion;
        result.dspVersion = existing->dspVersion;
        result.analyzedAt = existing->analyzedAt;
        result.errorCode = existing->errorCode;
        result.errorDetail = existing->errorDetail;
    }
    else
    {
        *found = false;
        result.pipelineState = SmartPipelineState::NotAnalyzed;
        result.extractorVersion = smartExtractorsVersion;
        result.resolverVersion = smartResolverVersion;
        result.dspVersion = smartDspVersion;
    }
    return result;
}
} // namespace

void SampleRepository::replaceSmartCandidates(int64_t sampleId, const std::vector<SmartCandidate>& candidates)
{
    std::lock_guard lock(db.mutex);
    detail::exec(db.handle, "BEGIN IMMEDIATE;");
    try
    {
        writeCandidates(db.handle, sampleId, candidates);
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
    return readCandidates(db.handle, sampleId);
}
void SampleRepository::setSmartResult(int64_t sampleId, const SmartResult& r)
{
    std::lock_guard lock(db.mutex);
    writeResult(db.handle, sampleId, r);
}
std::optional<SmartResult> SampleRepository::smartResult(int64_t sampleId) const
{
    std::lock_guard lock(db.mutex);
    return readResult(db.handle, sampleId);
}
int64_t SampleRepository::smartRevision(int64_t sampleId) const
{
    std::lock_guard lock(db.mutex);
    detail::Statement s(db.handle, "SELECT revision FROM smart_results WHERE sample_id=?");
    s.integer(1, sampleId);
    return s.step() ? s.integer(0) : 0;
}
SmartPublicationOutcome SampleRepository::publishSmartAnalysis(const SmartPublication& publication)
{
    std::lock_guard lock(db.mutex);
    SmartPublicationOutcome outcome;
    if (publication.catalogPath.isNotEmpty()
        && publication.catalogPath != db.file().getFullPathName())
    {
        outcome.reason = "catalog-changed";
        return outcome;
    }
    detail::exec(db.handle, "BEGIN IMMEDIATE;");
    try
    {
        bool hasRow = false;
        int64_t currentRevision = 0;
        {
            detail::Statement current(db.handle, "SELECT revision FROM smart_results WHERE sample_id=?");
            current.integer(1, publication.sampleId);
            hasRow = current.step();
            currentRevision = hasRow ? current.integer(0) : 0;
        }
        if ((hasRow && currentRevision != publication.expectedRevision)
            || (! hasRow && publication.expectedRevision != 0))
        {
            detail::exec(db.handle, "ROLLBACK;");
            outcome.reason = "revision-changed";
            outcome.revision = currentRevision;
            return outcome;
        }
        const auto candidates = publication.candidates;
        const auto overrides = readOverrides(db.handle, publication.sampleId);
        auto result = resolveSmart(candidates, overrides);
        result.revision = publication.expectedRevision + 1;
        result.generation = publication.generation > 0 ? publication.generation
                                                       : nextGeneration(db.handle);
        result.pipelineState = SmartPipelineState::Complete;
        result.extractorVersion = publication.extractorVersion;
        result.resolverVersion = publication.resolverVersion;
        result.dspVersion = publication.dspVersion;
        result.analyzedAt = juce::Time::currentTimeMillis();
        result.errorCode.clear();
        result.errorDetail.clear();
        writeCandidates(db.handle, publication.sampleId, candidates);
        writeInputs(db.handle, publication.sampleId, publication.inputs);
        writeExtractions(db.handle, publication.sampleId, publication.extractions);
        writeResult(db.handle, publication.sampleId, result);
        clearFailure(db.handle, publication.sampleId);
        detail::exec(db.handle, "COMMIT;");
        outcome.published = true;
        outcome.revision = result.revision;
        outcome.result = result;
        return outcome;
    }
    catch (...)
    {
        try { detail::exec(db.handle, "ROLLBACK;"); } catch (...) {}
        throw;
    }
}
void SampleRepository::markSmartStale(int64_t sampleId)
{
    std::lock_guard lock(db.mutex);
    detail::Statement s(db.handle,
        "INSERT INTO smart_results(sample_id,pipeline_state,updated_at) VALUES(?,'stale',?)"
        " ON CONFLICT(sample_id) DO UPDATE SET pipeline_state='stale',"
        " error_code='',error_detail='',updated_at=excluded.updated_at");
    s.integer(1, sampleId);
    s.integer(2, juce::Time::currentTimeMillis());
    s.step();
}
void SampleRepository::markSmartError(int64_t sampleId, const juce::String& code,
                                      const juce::String& message)
{
    std::lock_guard lock(db.mutex);
    detail::exec(db.handle, "BEGIN IMMEDIATE;");
    try
    {
        {
            detail::Statement s(db.handle,
                "INSERT INTO smart_results(sample_id,pipeline_state,error_code,error_detail,updated_at)"
                " VALUES(?,'error',?,?,?)"
                " ON CONFLICT(sample_id) DO UPDATE SET pipeline_state='error',"
                " error_code=excluded.error_code,error_detail=excluded.error_detail,"
                " updated_at=excluded.updated_at");
            s.integer(1, sampleId); s.text(2, code); s.text(3, message);
            s.integer(4, juce::Time::currentTimeMillis()); s.step();
        }
        writeFailure(db.handle, sampleId, code, message);
        detail::exec(db.handle, "COMMIT;");
    }
    catch (...)
    {
        try { detail::exec(db.handle, "ROLLBACK;"); } catch (...) {}
        throw;
    }
}
std::optional<SmartFailure> SampleRepository::smartFailure(int64_t sampleId) const
{
    std::lock_guard lock(db.mutex);
    detail::Statement s(db.handle,
        "SELECT code,message,attempts,updated_at FROM smart_failures WHERE sample_id=?");
    s.integer(1, sampleId);
    if (! s.step()) return std::nullopt;
    return SmartFailure{ s.text(0), s.text(1), (int) s.integer(2), s.integer(3) };
}
void SampleRepository::setSmartOverride(int64_t sampleId, SmartField field, const SmartOverride& value)
{
    std::lock_guard lock(db.mutex);
    detail::exec(db.handle, "BEGIN IMMEDIATE;");
    try
    {
        SmartOverride stored = value;
        // Keys and pitches are stored in a queryable form as well as text.
        if (field == SmartField::Key && ! value.cleared)
        {
            if (auto key = parseKeyValue(value.textValue))
            {
                stored.numericValue = key->tonic;
                stored.auxText = key->mode;
            }
        }
        {
            detail::Statement s(db.handle,
                "INSERT OR REPLACE INTO smart_overrides"
                "(sample_id,field,cleared,not_applicable,text_value,numeric_value,aux_value,aux_text,updated_at)"
                " VALUES(?,?,?,?,?,?,?,?,?)");
            s.integer(1, sampleId); s.text(2, smartFieldKey(field));
            s.integer(3, stored.cleared ? 1 : 0);
            s.integer(4, stored.notApplicable ? 1 : 0);
            s.text(5, stored.textValue); s.real(6, stored.numericValue);
            s.real(7, stored.auxValue); s.text(8, stored.auxText);
            s.integer(9, juce::Time::currentTimeMillis());
            s.step();
        }
        bool found = false;
        auto result = resolvedFromStored(db.handle, sampleId, &found);
        result.revision += 1;
        result.analyzedAt = juce::Time::currentTimeMillis();
        writeResult(db.handle, sampleId, result);
        detail::exec(db.handle, "COMMIT;");
    }
    catch (...)
    {
        try { detail::exec(db.handle, "ROLLBACK;"); } catch (...) {}
        throw;
    }
}
void SampleRepository::clearSmartOverride(int64_t sampleId, SmartField field)
{
    std::lock_guard lock(db.mutex);
    detail::exec(db.handle, "BEGIN IMMEDIATE;");
    try
    {
        {
            detail::Statement s(db.handle, "DELETE FROM smart_overrides WHERE sample_id=? AND field=?");
            s.integer(1, sampleId); s.text(2, smartFieldKey(field)); s.step();
        }
        bool found = false;
        auto result = resolvedFromStored(db.handle, sampleId, &found);
        result.revision += 1;
        result.analyzedAt = juce::Time::currentTimeMillis();
        if (found || ! readCandidates(db.handle, sampleId).empty())
            writeResult(db.handle, sampleId, result);
        detail::exec(db.handle, "COMMIT;");
    }
    catch (...)
    {
        try { detail::exec(db.handle, "ROLLBACK;"); } catch (...) {}
        throw;
    }
}
SmartOverrideMap SampleRepository::smartOverrides(int64_t sampleId) const
{
    std::lock_guard lock(db.mutex);
    return readOverrides(db.handle, sampleId);
}
std::vector<SmartExtraction> SampleRepository::smartExtractions(int64_t sampleId) const
{
    std::lock_guard lock(db.mutex);
    return readExtractions(db.handle, sampleId);
}
std::map<int64_t, SmartDisplay> SampleRepository::smartDisplays(const std::vector<int64_t>& sampleIds) const
{
    std::map<int64_t, SmartDisplay> out;
    if (sampleIds.empty()) return out;
    std::lock_guard lock(db.mutex);
    constexpr size_t chunkSize = 400;
    for (size_t start = 0; start < sampleIds.size(); start += chunkSize)
    {
        const size_t end = std::min(sampleIds.size(), start + chunkSize);
        juce::String placeholders;
        for (size_t i = start; i < end; ++i)
        {
            if (i != start) placeholders += ",";
            placeholders += "?";
        }
        detail::Statement results(db.handle,
            ("SELECT sample_id,type,type_state,structure,structure_state,bpm,has_bpm,bpm_state,"
             "key_tonic,key_mode,key_state,pitch_midi,pitch_cents,pitch_state,"
             "pipeline_state FROM smart_results WHERE sample_id IN (" + placeholders + ")").toRawUTF8());
        for (size_t i = start; i < end; ++i) results.integer((int) (i - start + 1), sampleIds[i]);
        while (results.step())
        {
            SmartDisplay d;
            const auto id = results.integer(0);
            d.type = results.text(1);
            d.typeState = smartStateFromKey(results.text(2)).value_or(SmartState::Unknown);
            d.structure = results.text(3);
            d.structureState = smartStateFromKey(results.text(4)).value_or(SmartState::Unknown);
            d.bpm = results.real(5);
            d.hasBpm = results.integer(6) != 0;
            d.bpmState = smartStateFromKey(results.text(7)).value_or(SmartState::Unknown);
            const int tonic = (int) results.integer(8);
            const auto mode = results.text(9);
            d.keyState = smartStateFromKey(results.text(10)).value_or(SmartState::Unknown);
            d.key = keyDisplayName(tonic, mode);
            d.pitchMidi = (int) results.integer(11);
            d.pitchCents = results.real(12);
            d.pitchState = smartStateFromKey(results.text(13)).value_or(SmartState::Unknown);
            d.hasPitch = d.pitchMidi >= 0 && d.pitchState != SmartState::Unknown
                         && d.pitchState != SmartState::NotApplicable;
            d.pipelineState = smartPipelineStateFromKey(results.text(14))
                                  .value_or(SmartPipelineState::NotAnalyzed);
            // A pre-v7 row can carry values without a pipeline state. It is not "never analyzed":
            // show the values with the stale marker until a pass rewrites them.
            if (d.pipelineState == SmartPipelineState::NotAnalyzed
                && (d.type.isNotEmpty() || d.hasBpm || d.key.isNotEmpty() || d.hasPitch))
                d.pipelineState = SmartPipelineState::Stale;
            out[id] = d;
        }
        detail::Statement overrides(db.handle,
            ("SELECT sample_id,field,cleared,not_applicable,text_value,numeric_value,aux_value"
             " FROM smart_overrides WHERE sample_id IN (" + placeholders + ")"
             " AND field IN ('type','structure','bpm','key','pitch')").toRawUTF8());
        for (size_t i = start; i < end; ++i) overrides.integer((int) (i - start + 1), sampleIds[i]);
        while (overrides.step())
        {
            const auto id = overrides.integer(0);
            auto& d = out[id];
            const auto field = overrides.text(1);
            const bool cleared = overrides.integer(2) != 0;
            const bool notApplicable = overrides.integer(3) != 0;
            const auto state = notApplicable ? OverrideState::NotApplicable
                            : cleared ? OverrideState::Unknown : OverrideState::Value;
            if (field == "type")
            {
                d.typeOverride = state;
                if (state == OverrideState::Value) d.type = overrides.text(4);
                else d.type.clear();
            }
            else if (field == "structure")
            {
                d.structureOverride = state;
                if (state == OverrideState::Value) d.structure = overrides.text(4);
                else d.structure.clear();
            }
            else if (field == "bpm")
            {
                d.bpmOverride = state;
                const auto value = overrides.real(5);
                const bool valid = state == OverrideState::Value && std::isfinite(value)
                                   && value >= 20.0 && value <= 400.0;
                d.hasBpm = valid;
                d.bpm = valid ? value : 0.0;
            }
            else if (field == "key")
            {
                d.keyOverride = state;
                if (state == OverrideState::Value)
                {
                    if (auto key = parseKeyValue(overrides.text(4)))
                        d.key = keyDisplayName(key->tonic, key->mode);
                    else
                        d.key = overrides.text(4);
                }
                else d.key.clear();
            }
            else if (field == "pitch")
            {
                d.pitchOverride = state;
                const auto value = overrides.real(5);
                const bool valid = state == OverrideState::Value && std::isfinite(value)
                                   && value >= 0.0 && value <= 127.0;
                d.hasPitch = valid;
                d.pitchMidi = valid ? (int) std::lround(value) : -1;
                d.pitchCents = valid
                    ? (value - std::round(value)) * 100.0 + overrides.real(6) : 0.0;
            }
        }
    }
    // Rows without a smart result still need a display entry with the default state.
    for (auto id : sampleIds) if (out.find(id) == out.end()) out[id] = {};
    return out;
}
void SampleRepository::setSmartInputs(int64_t sampleId, const std::vector<SmartInput>& inputs)
{
    std::lock_guard lock(db.mutex);
    detail::exec(db.handle, "BEGIN IMMEDIATE;");
    try
    {
        writeInputs(db.handle, sampleId, inputs);
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
    return readInputs(db.handle, sampleId);
}
std::optional<SampleRecord> SampleRepository::findById(int64_t id) const
{
    std::lock_guard lock(db.mutex);
    const auto sql = juce::String("SELECT ") + columns + " FROM samples WHERE id=?";
    detail::Statement s(db.handle, sql.toRawUTF8());
    s.integer(1, id);
    return s.step() ? std::optional<SampleRecord>(read(s)) : std::nullopt;
}
std::vector<int64_t> SampleRepository::samplesNeedingSmartAnalysis(int64_t afterId, int limit,
                                                                   int extractorVersion,
                                                                   int resolverVersion,
                                                                   int dspVersion) const
{
    std::lock_guard lock(db.mutex);
    const auto ignored = ignoredPathsLocked();
    const auto visibility = ignored.empty() ? juce::String() : juce::String(" AND s.ignored=0");
    const auto sql = juce::String(
        "SELECT s.id FROM samples s LEFT JOIN smart_results r ON r.sample_id=s.id"
        " WHERE s.id>? AND s.container_path='' AND (r.sample_id IS NULL"
        " OR r.pipeline_state IN ('stale','error','not_analyzed')"
        " OR r.extractor_version<>? OR r.resolver_version<>? OR r.dsp_version<>?"
        " OR r.revision<=0)") + visibility + " ORDER BY s.id LIMIT ?";
    detail::Statement s(db.handle, sql.toRawUTF8());
    s.integer(1, afterId);
    s.integer(2, extractorVersion); s.integer(3, resolverVersion); s.integer(4, dspVersion);
    s.integer(5, limit);
    std::vector<int64_t> out;
    while (s.step()) out.push_back(s.integer(0));
    return out;
}
std::vector<int64_t> SampleRepository::samplesWithSmartResultAfterId(int64_t afterId, int limit) const
{
    std::lock_guard lock(db.mutex);
    const auto ignored = ignoredPathsLocked();
    const auto visibility = ignored.empty() ? juce::String() : juce::String(" AND s.ignored=0");
    const auto sql = juce::String(
        "SELECT r.sample_id FROM smart_results r JOIN samples s ON s.id=r.sample_id"
        " WHERE r.sample_id>? AND s.container_path='' AND r.pipeline_state='complete'") + visibility
        + " ORDER BY r.sample_id LIMIT ?";
    detail::Statement s(db.handle, sql.toRawUTF8());
    s.integer(1, afterId);
    s.integer(2, limit);
    std::vector<int64_t> out;
    while (s.step()) out.push_back(s.integer(0));
    return out;
}

std::vector<juce::String> SampleRepository::customTypes() const
{
    std::lock_guard lock(db.mutex);
    detail::Statement s(db.handle, "SELECT name FROM custom_types ORDER BY name COLLATE NOCASE");
    std::vector<juce::String> out;
    while (s.step()) out.push_back(s.text(0));
    return out;
}

bool SampleRepository::addCustomType(const juce::String& name)
{
    const auto trimmed = name.trim();
    if (trimmed.isEmpty()) return false;
    std::lock_guard lock(db.mutex);
    try
    {
        detail::Statement s(db.handle, "INSERT INTO custom_types (name, created_at) VALUES (?, ?)");
        s.text(1, trimmed);
        s.integer(2, juce::Time::currentTimeMillis());
        s.step();
        return true;
    }
    catch (...)
    {
        return false;
    }
}

void SampleRepository::recordFeedback(int64_t sampleId, const juce::String& targetType,
                                      const juce::StringArray& tags, const std::vector<float>& features)
{
    std::lock_guard lock(db.mutex);
    juce::String hash;
    {
        detail::Statement s(db.handle, "SELECT hash FROM samples WHERE id=?");
        s.integer(1, sampleId);
        if (s.step()) hash = s.text(0);
    }
    const auto tagsStr = tags.joinIntoString(",");
    detail::exec(db.handle, "BEGIN IMMEDIATE;");
    try
    {
        detail::Statement s(db.handle,
            "INSERT INTO ml_feedback_samples (sample_id, file_hash, target_type, tags, features, updated_at, contribution_id) "
            "VALUES (?, ?, ?, ?, ?, ?, ?) "
            "ON CONFLICT(sample_id) DO UPDATE SET "
            "file_hash=excluded.file_hash, target_type=excluded.target_type, tags=excluded.tags, "
            "features=excluded.features, updated_at=excluded.updated_at");
        s.integer(1, sampleId);
        s.text(2, hash);
        s.text(3, targetType.trim());
        s.text(4, tagsStr);
        s.blob(5, features.data(), static_cast<int>(features.size() * sizeof(float)));
        s.integer(6, juce::Time::currentTimeMillis());
        s.text(7, juce::Uuid().toDashedString().toLowerCase());
        s.step();
        detail::exec(db.handle, "COMMIT;");
    }
    catch (...)
    {
        try { detail::exec(db.handle, "ROLLBACK;"); } catch (...) {}
        throw;
    }
}

void SampleRepository::removeFeedback(int64_t sampleId)
{
    std::lock_guard lock(db.mutex);
    try
    {
        detail::Statement s(db.handle, "DELETE FROM ml_feedback_samples WHERE sample_id=?");
        s.integer(1, sampleId);
        s.step();
    }
    catch (...) {}
}

std::vector<SampleRepository::FeedbackRecord> SampleRepository::allFeedback() const
{
    std::lock_guard lock(db.mutex);
    detail::Statement s(db.handle,
        "SELECT sample_id, file_hash, target_type, tags, features, updated_at FROM ml_feedback_samples ORDER BY sample_id");
    std::vector<FeedbackRecord> out;
    while (s.step())
    {
        FeedbackRecord r;
        r.sampleId = s.integer(0);
        r.fileHash = s.text(1);
        r.targetType = s.text(2);
        r.tags.addTokens(s.text(3), ",", "");
        r.tags.trim();
        r.tags.removeEmptyStrings();
        const auto* blobData = reinterpret_cast<const float*>(s.blob(4));
        const int numBytes = s.bytes(4);
        if (blobData != nullptr && numBytes > 0)
        {
            const size_t numFloats = numBytes / sizeof(float);
            r.features.assign(blobData, blobData + numFloats);
        }
        r.updatedAt = s.integer(5);
        out.push_back(std::move(r));
    }
    return out;
}

std::vector<SampleRepository::ContributionRecord> SampleRepository::contributionRecords(
    int64_t afterUpdatedAt, int limit) const
{
    std::lock_guard lock(db.mutex);
    // Only the fields the model uses. smart_results supplies the key and tempo the user saw.
    detail::Statement s(db.handle,
        "SELECT f.contribution_id, f.target_type, f.tags, f.features, f.updated_at,"
        " r.key_tonic, r.key_mode, r.bpm, r.has_bpm"
        " FROM ml_feedback_samples f LEFT JOIN smart_results r ON r.sample_id=f.sample_id"
        " WHERE f.updated_at > ? ORDER BY f.updated_at, f.sample_id LIMIT ?");
    s.integer(1, afterUpdatedAt);
    s.integer(2, juce::jlimit(1, 5000, limit));
    std::vector<ContributionRecord> out;
    while (s.step())
    {
        ContributionRecord r;
        r.recordId = s.text(0);
        if (r.recordId.isEmpty()) continue;
        r.targetType = s.text(1);
        r.tags.addTokens(s.text(2), ",", "");
        r.tags.trim();
        r.tags.removeEmptyStrings();
        const auto* blobData = reinterpret_cast<const float*>(s.blob(3));
        const int numBytes = s.bytes(3);
        if (blobData != nullptr && numBytes > 0)
        {
            const size_t numFloats = numBytes / sizeof(float);
            r.features.assign(blobData, blobData + numFloats);
        }
        r.updatedAt = s.integer(4);
        r.key = keyDisplayName((int) s.integer(5), s.text(6));
        r.bpm = s.real(7);
        r.hasBpm = s.integer(8) != 0 && r.bpm > 0.0;
        out.push_back(std::move(r));
    }
    return out;
}

int64_t SampleRepository::contributionWatermark() const
{
    std::lock_guard lock(db.mutex);
    detail::Statement s(db.handle, "SELECT value FROM contribution_state WHERE key='watermark'");
    return s.step() ? s.integer(0) : 0;
}

void SampleRepository::setContributionWatermark(int64_t value)
{
    std::lock_guard lock(db.mutex);
    detail::Statement s(db.handle,
        "INSERT INTO contribution_state(key,value) VALUES('watermark',?) "
        "ON CONFLICT(key) DO UPDATE SET value=excluded.value");
    s.integer(1, value);
    s.step();
}

juce::StringArray SampleRepository::builtinTagNames() const
{
    std::lock_guard lock(db.mutex);
    detail::Statement s(db.handle, "SELECT name FROM tags WHERE builtin=1");
    juce::StringArray out;
    while (s.step()) out.add(s.text(0));
    return out;
}
} // namespace sonicator
