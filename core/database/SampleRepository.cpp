#include "SampleRepository.h"
#include "SqliteHelpers.h"
namespace sonicator {
namespace {
constexpr auto columns = "id,path,name,hash,format,error,size,modified,analyzed,duration,rate,bits,channels,version,peak,rms,centroid,rolloff,flatness,missing";
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
}
void SampleRepository::createTables()
{
    std::lock_guard lock(db.mutex);
    detail::Statement version(db.handle, "PRAGMA user_version");
    if (version.step() && version.integer(0) > 1) throw std::runtime_error("Database schema is newer than this application");
    detail::exec(db.handle,
        "CREATE TABLE IF NOT EXISTS samples (id INTEGER PRIMARY KEY, path TEXT NOT NULL UNIQUE,"
        "name TEXT NOT NULL, hash TEXT NOT NULL, format TEXT NOT NULL, error TEXT NOT NULL,"
        "size INTEGER, modified INTEGER, analyzed INTEGER, duration REAL, rate REAL, bits INTEGER,"
        "channels INTEGER, version INTEGER, peak REAL, rms REAL, centroid REAL, rolloff REAL, flatness REAL, missing INTEGER);"
        "CREATE INDEX IF NOT EXISTS samples_hash ON samples(hash);"
        "CREATE TABLE IF NOT EXISTS roots(path TEXT PRIMARY KEY); PRAGMA user_version=1;");
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
int SampleRepository::count() const
{
    std::lock_guard lock(db.mutex);
    detail::Statement s(db.handle,"SELECT COUNT(*) FROM samples");
    return s.step() ? (int) s.integer(0) : 0;
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
