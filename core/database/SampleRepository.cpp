#include "SampleRepository.h"
#include "SqliteHelpers.h"
namespace sonicator {
namespace {
constexpr auto columns = "id,path,name,hash,format,error,size,modified,analyzed,duration,rate,bits,channels,version,peak,rms,centroid,rolloff,flatness,missing";
constexpr int schemaVersion = 2;
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
    return r;
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
            "channels INTEGER, version INTEGER, peak REAL, rms REAL, centroid REAL, rolloff REAL, flatness REAL, missing INTEGER);"
            "CREATE INDEX IF NOT EXISTS samples_hash ON samples(hash);"
            "CREATE INDEX IF NOT EXISTS samples_name_id ON samples(name COLLATE NOCASE, id);"
            "CREATE TABLE IF NOT EXISTS roots(path TEXT PRIMARY KEY);"
            "PRAGMA user_version=2;");
        // Rows written by the build that bumped the version to 2 measured the same feature set, so
        // they are relabelled rather than reanalyzed. Only during this one-time upgrade.
        if (current >= 1 && current < 2) detail::exec(db.handle, "UPDATE samples SET version=1 WHERE version=2;");
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
        "INSERT INTO samples(path,name,hash,format,error,size,modified,analyzed,duration,rate,bits,channels,version,peak,rms,centroid,rolloff,flatness,missing)"
        " VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) ON CONFLICT(path) DO UPDATE SET "
        "name=excluded.name,hash=excluded.hash,format=excluded.format,error=excluded.error,size=excluded.size,"
        "modified=excluded.modified,analyzed=excluded.analyzed,duration=excluded.duration,rate=excluded.rate,"
        "bits=excluded.bits,channels=excluded.channels,version=excluded.version,peak=excluded.peak,rms=excluded.rms,"
        "centroid=excluded.centroid,rolloff=excluded.rolloff,flatness=excluded.flatness,missing=excluded.missing RETURNING id");
    s.text(1,r.filePath); s.text(2,r.fileName); s.text(3,r.fileHash); s.text(4,r.format); s.text(5,r.error);
    s.integer(6,r.fileSize); s.integer(7,r.dateModified); s.integer(8,r.dateAnalyzed);
    s.real(9,r.duration); s.real(10,r.sampleRate); s.integer(11,r.bitsPerSample); s.integer(12,r.numChannels);
    s.integer(13,r.version); s.real(14,r.peakLevel); s.real(15,r.rmsLevel); s.real(16,r.spectralCentroid);
    s.real(17,r.spectralRolloff); s.real(18,r.spectralFlatness); s.integer(19,r.missing ? 1 : 0);
    if (! s.step()) throw std::runtime_error("Insert did not return a sample ID");
    return s.integer(0);
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
namespace {
std::pair<juce::String,juce::String> folderRange(const juce::String& folder)
{
    auto prefix = juce::File(folder).getFullPathName();
    if (! prefix.endsWithChar(juce::File::getSeparatorChar())) prefix += juce::File::getSeparatorString();
    return {prefix, prefix.dropLastCharacters(1) + juce::String::charToString(juce::File::getSeparatorChar()+1)};
}
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
}
