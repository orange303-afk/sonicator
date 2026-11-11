#pragma once
#include "Database.h"
#include "LibraryQuery.h"
#include "SampleRecord.h"
#include "core/smart/SmartTypes.h"
#include <functional>
#include <map>
#include <optional>
#include <vector>
namespace sonicator {
struct PathEntry
{
    int64_t id = 0;
    juce::String path;
    bool missing = false;
    juce::String containerPath;
};
struct DiscIndexState
{
    int64_t imageSize = 0, imageModified = 0, entryCount = 0;
};
struct IgnoredPath
{
    juce::String path;
    bool folder = false;
    int64_t createdAt = 0;
};
// Lock-free matcher shared by the repository, folder tree, file list and scanner.
bool isPathIgnored(const juce::String& path, const std::vector<IgnoredPath>& entries);
struct RootRelocation
{
    juce::String oldRoot, newRoot;
};
struct FolderRemoval
{
    int rowsRemoved = 0;
    bool wasRoot = false;
};
struct RelocationResult
{
    int64_t movedFiles = 0, conflicts = 0;
};
// One atomic publication of a finished analysis generation.
struct SmartPublication
{
    int64_t sampleId = 0;
    int64_t expectedRevision = 0;      // revision read before the job started
    std::vector<SmartCandidate> candidates;
    std::vector<SmartInput> inputs;
    std::vector<SmartExtraction> extractions;
    int extractorVersion = smartExtractorsVersion;
    int resolverVersion = smartResolverVersion;
    int dspVersion = smartDspVersion;
    int64_t generation = 0;            // 0 = allocate a new catalog generation
    juce::String catalogPath;          // empty = no catalog guard
};
struct SmartPublicationOutcome
{
    bool published = false;
    juce::String reason;               // revision-changed, catalog-changed
    int64_t revision = 0;
    SmartResult result;
};
class SampleRepository
{
public:
    explicit SampleRepository(Database& d) : db(d) {}
    void createTables();
    int64_t upsert(const SampleRecord&);
    std::optional<SampleRecord> findByPath(const juce::String&) const;
    std::vector<SampleRecord> page(int limit = 10000, int offset = 0) const;
    std::vector<PathEntry> pathsAfterId(int64_t lastId, int limit = 5000) const;
    std::vector<IgnoredPath> ignoredPaths() const;
    // True when the exact path or any ancestor folder is ignored.
    bool isIgnored(const juce::String& path) const;
    // True only when this exact path has its own ignore entry.
    bool hasExactIgnore(const juce::String& path) const;
    void ignorePath(const juce::String& path, bool folder);
    void unignorePath(const juce::String& path);
    int ignorePaths(const std::vector<juce::String>& paths, bool folder = false);
    int unignorePaths(const std::vector<juce::String>& paths);
    // Clears every folder ignore that covers this path (the path itself or an ancestor). Used when
    // the user explicitly adds a folder that a previous Remove had kept out of parent scans.
    int unignoreCoveringFolders(const juce::String& path);
    // Paths with one of the given formats whose stored analysis failed. Used to migrate rows for
    // formats this build no longer accepts.
    std::vector<juce::String> errorPathsWithFormats(const juce::StringArray& formats) const;
    // Deletes catalog rows for paths whose files were removed, together with exact ignore entries.
    void removeByPaths(const juce::StringArray& paths);
    void setMissing(const std::vector<int64_t>& ids);
    int count(bool includeIgnored = false) const;
    int countInFolder(const juce::String& folder, bool includeDiscContents = true) const;
    std::vector<SampleRecord> pageInFolder(const juce::String& folder, int limit = 10000, int offset = 0) const;
    // Whole-catalog query. Text, folder and tags are combined with AND; the returned total is the
    // full match count, independent of the requested page.
    LibraryPage query(const LibraryFilter&, int limit = 10000, int offset = 0) const;
    // Every matching ID in the same order as query(). Used by the virtual all-on-one-page view.
    std::vector<int64_t> queryIds(const LibraryFilter&) const;
    std::vector<SampleRecord> recordsByIds(const std::vector<int64_t>&) const;
    // The subset of ids that is not hidden by an ignore entry, preserving input order.
    std::vector<int64_t> visibleIds(const std::vector<int64_t>& ids) const;
    // Keeps input order; drops rows that live inside disc images.
    std::vector<int64_t> nonDiscIds(const std::vector<int64_t>& ids) const;
    // Representative id -> partner path for every valid mono L/R stereo pair.
    std::map<int64_t, juce::String> stereoPairPartners(const std::vector<int64_t>& ids) const;
    int countHash(const juce::String&) const;
    void addRoot(const juce::String&);
    // Disc-image contents. Rows are ordinary catalog rows with container_path set and a synthetic
    // path; this API keeps them isolated from real-file bookkeeping and stores the image state so
    // an unchanged image is not walked again.
    std::optional<DiscIndexState> discIndexState(const juce::String& imagePath) const;
    // removeMissing=false is used for a partial listing: upsert what was found, keep earlier rows.
    void replaceDiscContents(const juce::String& imagePath, int64_t imageSize, int64_t imageModified,
                             const std::vector<SampleRecord>& rows, bool removeMissing = true);
    void deleteDiscContents(const juce::String& imagePath);
    // Forget that this image was fully walked (used after a partial listing) so a later scan
    // retries it while keeping the rows already found.
    void invalidateDiscState(const juce::String& imagePath);
    // Drops contents of images that are gone, ignored or no longer catalogued. Returns row count.
    int pruneDiscContents();
    // Deletes every catalog row below a folder. A registered root also loses its root entry; any
    // other folder gets an exact ignore entry so a parent rescan does not restore it. Files on
    // disk are never touched.
    FolderRemoval removeFolder(const juce::String& folder);
    // Drops catalog rows for the given format labels, e.g. formats this build no longer indexes.
    int removeSamplesWithFormats(const juce::StringArray& formats);
    juce::StringArray roots() const;
    int countUnderRoot(const juce::String& root) const;
    // Rewrites every sample path below each old root to the matching new root, preserving the
    // relative structure. Runs in one transaction and keeps ids, tags, stars and analysis data.
    RelocationResult relocateRoots(const std::vector<RootRelocation>&);
    // Consistent SQLite backup through the backup API (WAL-safe). The destination must be new.
    void backupTo(const juce::File& destination);
    // True only for an existing SONICATOR catalog (samples and roots tables with a schema version).
    static bool isCatalog(const juce::File& file);
    std::vector<Tag> tags() const;
    std::vector<Tag> tagsForSample(int64_t sampleId) const;
    std::map<int64_t, std::vector<Tag>> tagsForSamples(const std::vector<int64_t>& sampleIds) const;
    int64_t createTag(const juce::String& name, uint32_t colour);
    void setTagColour(int64_t tagId, uint32_t colour);
    bool renameTag(int64_t tagId, const juce::String& name);
    void deleteTag(int64_t tagId);
    void setSampleTag(int64_t sampleId, int64_t tagId, bool assigned);
    void setSampleTag(const std::vector<int64_t>& sampleIds, int64_t tagId, bool assigned);
    // Assigns only missing links and returns how many were actually created. Existing links are
    // never removed, so a repeated bulk assignment is idempotent.
    int assignTag(const std::vector<int64_t>& sampleIds, int64_t tagId);
    struct TagAssignmentOutcome { int added = 0; bool cancelled = false; };
    // One transaction. Cancellation before the commit rolls the whole batch back, so a cancelled
    // batch leaves no partial links. Ids that are not in the catalog are skipped, never errors.
    TagAssignmentOutcome assignTagBatch(const std::vector<int64_t>& sampleIds, int64_t tagId,
                                        const std::function<bool()>& isCancelled);
    struct TagPair { int64_t sampleId = 0, tagId = 0; };
    // AutoTag applies one class per file, so a batch may carry different tag ids. Missing samples
    // or tags are skipped; previous batches stay committed when a later batch fails.
    TagAssignmentOutcome assignTagPairs(const std::vector<TagPair>& pairs,
                                        const std::function<bool()>& isCancelled);
    // Removes every tag link in one transaction. Cancellation rolls the batch back. Tag
    // definitions, stars, smart data and recent-tag history are untouched.
    TagAssignmentOutcome clearTagsBatch(const std::vector<int64_t>& sampleIds,
                                        const std::function<bool()>& isCancelled);
    int countTaggedInFolder(const juce::String& folder) const;
    int countTaggedSamples(const std::vector<int64_t>& sampleIds) const;
    // Last explicitly assigned tags, newest first. Stored per catalog; deleted tags fall out of
    // the join and renamed tags keep their current name.
    void recordRecentTag(int64_t tagId);
    std::vector<Tag> recentTags(int limit = 5) const;
    // Every catalogued sample inside a folder and its descendants. Separator-aware, independent
    // of search, tags, stars, duplicate filters and pagination. Empty folder means no scope.
    std::vector<int64_t> idsInFolder(const juce::String& folder, bool includeDiscContents = true) const;
    int countWithTagInFolder(const juce::String& folder, int64_t tagId) const;
    void setStarred(const std::vector<int64_t>& sampleIds, bool starred);
    // Rebuilds the byte-identical duplicate groups (same full SHA-256 and size).
    void refreshDuplicates();
    void updateDuplicateRepresentative(int64_t sampleId);
    void updateDuplicateRepresentatives(const std::vector<int64_t>& sampleIds);
    std::map<int64_t,int> duplicateCounts(const std::vector<int64_t>& sampleIds) const;
    struct DuplicateInfo { int groupSize = 0; int64_t representativeId = 0; juce::String representativePath; };
    std::optional<DuplicateInfo> duplicateInfo(int64_t sampleId) const;
    std::vector<SampleRecord> duplicateMembers(int64_t sampleId) const;
    // Lower-cased name + newline + lower-cased path, used by whole-catalog text search.
    static juce::String searchKey(const juce::String& name, const juce::String& path);
    // Phase 2 smart analysis. Candidates and overrides are stored separately from measured
    // Phase 1 features, so reanalysis never rewrites them and a rescan never clears them.
    void replaceSmartCandidates(int64_t sampleId, const std::vector<SmartCandidate>&);
    std::vector<SmartCandidate> smartCandidates(int64_t sampleId) const;
    void setSmartResult(int64_t sampleId, const SmartResult&);
    std::optional<SmartResult> smartResult(int64_t sampleId) const;
    // Revision of the stored result; 0 when nothing was published yet. A job reads it before
    // analysis and passes it back to publishSmartAnalysis, so a stale job cannot overwrite a
    // newer result.
    int64_t smartRevision(int64_t sampleId) const;
    // Publishes candidates, extractions, input fingerprints and the resolved result in one
    // transaction. Overrides are read inside that transaction and survive untouched.
    SmartPublicationOutcome publishSmartAnalysis(const SmartPublication&);
    // Keeps the last completed values but marks them as not current. Used when a dependency
    // changed during analysis or an extractor version is behind.
    void markSmartStale(int64_t sampleId);
    // Records a structured failure without discarding the previous completed generation.
    void markSmartError(int64_t sampleId, const juce::String& code, const juce::String& message);
    std::optional<SmartFailure> smartFailure(int64_t sampleId) const;
    void setSmartOverride(int64_t sampleId, SmartField field, const SmartOverride&);
    void clearSmartOverride(int64_t sampleId, SmartField field);
    SmartOverrideMap smartOverrides(int64_t sampleId) const;
    std::vector<SmartExtraction> smartExtractions(int64_t sampleId) const;
    // Effective Type/BPM/Key for a block of rows, with overrides applied. Used by the library
    // columns and the selection status bar; never loads the whole catalog.
    std::map<int64_t, SmartDisplay> smartDisplays(const std::vector<int64_t>& sampleIds) const;
    // Samples whose result is missing, stale, failed or produced by another version, ordered by
    // id. Discovery of changed sidecars happens in the service by comparing fingerprints.
    std::vector<int64_t> samplesNeedingSmartAnalysis(int64_t afterId, int limit,
        int extractorVersion = smartExtractorsVersion,
        int resolverVersion = smartResolverVersion,
        int dspVersion = smartDspVersion) const;
    // Completed results, used by the incremental pass to verify their stored fingerprints.
    std::vector<int64_t> samplesWithSmartResultAfterId(int64_t afterId, int limit) const;
    void setSmartInputs(int64_t sampleId, const std::vector<SmartInput>&);
    std::vector<SmartInput> smartInputs(int64_t sampleId) const;
    std::optional<SampleRecord> findById(int64_t) const;

    // Custom types & ML feedback prototypes
    struct FeedbackRecord
    {
        int64_t sampleId = 0;
        juce::String fileHash;
        juce::String targetType;
        juce::StringArray tags;
        std::vector<float> features;
        int64_t updatedAt = 0;
    };
    std::vector<juce::String> customTypes() const;
    bool addCustomType(const juce::String& name);
    void recordFeedback(int64_t sampleId, const juce::String& targetType,
                        const juce::StringArray& tags, const std::vector<float>& features);
    void removeFeedback(int64_t sampleId);
    std::vector<FeedbackRecord> allFeedback() const;

    // A confirmed sample shaped for the contribution intake: no path, no file hash and no
    // sample id, only what the model learns from.
    struct ContributionRecord
    {
        juce::String recordId;       // stable per row; a correction reuses it
        juce::String targetType;
        juce::StringArray tags;
        std::vector<float> features;
        juce::String key;            // empty when unknown
        double bpm = 0.0;
        bool hasBpm = false;
        int64_t updatedAt = 0;
    };
    // Confirmed samples whose feedback changed after `afterUpdatedAt`, oldest first.
    std::vector<ContributionRecord> contributionRecords(int64_t afterUpdatedAt, int limit) const;
    // Per-catalog marker: the highest updatedAt already accepted by the intake, or 0.
    int64_t contributionWatermark() const;
    void setContributionWatermark(int64_t);
    juce::StringArray builtinTagNames() const;

    // Identity of the catalog this repository writes to. A background job captures it at start
    // and refuses to publish into a different catalog.
    juce::String catalogPath() const { return db.file().getFullPathName(); }
private:
    std::vector<IgnoredPath> ignoredPathsLocked() const;
    // Caller holds db.mutex and an open transaction.
    void unignorePathLocked(const juce::String& path);
    void updateDuplicateRepresentativesLocked(const std::vector<int64_t>& sampleIds);
    int64_t upsertLocked(const SampleRecord&, const std::vector<IgnoredPath>* ignoredEntries = nullptr);
    std::vector<juce::String> discPathsLocked(const juce::String& imagePath) const;
    Database& db;
};
} // namespace sonicator
