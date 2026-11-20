#include "DuplicateCleanup.h"
#include <cstring>
#include <juce_core/juce_core.h>

namespace sonicator::files
{
namespace
{
constexpr int compareChunkBytes = 65536;

struct JournalEntry
{
    juce::File staging, link, original;
};

juce::String journalToJson(const std::vector<JournalEntry>& entries)
{
    juce::Array<juce::var> list;
    for (const auto& entry : entries)
    {
        auto* object = new juce::DynamicObject();
        object->setProperty("staging", entry.staging.getFullPathName());
        object->setProperty("link", entry.link.getFullPathName());
        object->setProperty("original", entry.original.getFullPathName());
        list.add(juce::var(object));
    }
    auto* root = new juce::DynamicObject();
    root->setProperty("entries", list);
    return juce::JSON::toString(juce::var(root), false);
}

std::vector<JournalEntry> readJournal(const juce::File& file)
{
    std::vector<JournalEntry> entries;
    if (! file.existsAsFile()) return entries;
    const auto parsed = juce::JSON::parse(file);
    const auto* root = parsed.getDynamicObject();
    if (root == nullptr) return entries;
    const auto* list = root->getProperty("entries").getArray();
    if (list == nullptr) return entries;
    for (const auto& value : *list)
    {
        const auto* object = value.getDynamicObject();
        if (object == nullptr) continue;
        const auto staging = object->getProperty("staging").toString();
        const auto link = object->getProperty("link").toString();
        const auto original = object->getProperty("original").toString();
        if (staging.isEmpty() || link.isEmpty()) continue;
        entries.push_back({ juce::File(staging), juce::File(link), juce::File(original) });
    }
    return entries;
}

void writeJournal(const juce::File& file, const std::vector<JournalEntry>& entries)
{
    if (file == juce::File()) return;
    if (entries.empty())
    {
        file.deleteFile();
        return;
    }
    file.getParentDirectory().createDirectory();
    juce::TemporaryFile temporary(file);
    if (temporary.getFile().replaceWithText(journalToJson(entries)))
        temporary.overwriteTargetFileWithTemporary();
}

struct JournalGuard
{
    juce::File file;
    std::vector<JournalEntry> entries;
    void add(const JournalEntry& entry) { entries.push_back(entry); writeJournal(file, entries); }
    void remove(const JournalEntry& entry)
    {
        for (auto it = entries.begin(); it != entries.end(); ++it)
            if (it->staging == entry.staging && it->link == entry.link)
            { entries.erase(it); break; }
        writeJournal(file, entries);
    }
};
} // namespace

juce::File DuplicateCleanupService::defaultJournalFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Sonicator/duplicate-cleanup.json");
}

bool filesAreIdentical(const juce::File& a, const juce::File& b, juce::String& error)
{
    const auto first = a.isSymbolicLink() ? a.getLinkedTarget() : a;
    const auto second = b.isSymbolicLink() ? b.getLinkedTarget() : b;
    if (! first.existsAsFile()) { error = "Missing file: " + first.getFullPathName(); return false; }
    if (! second.existsAsFile()) { error = "Missing file: " + second.getFullPathName(); return false; }
    if (first.getSize() != second.getSize()) return false;
    juce::FileInputStream left(first), right(second);
    if (! left.openedOk() || ! right.openedOk())
    {
        error = "Cannot read one of the files for comparison";
        return false;
    }
    juce::HeapBlock<char> leftBuffer((size_t) compareChunkBytes), rightBuffer((size_t) compareChunkBytes);
    int64_t remaining = first.getSize();
    while (remaining > 0)
    {
        const auto count = (int) juce::jmin<int64_t>(compareChunkBytes, remaining);
        if (left.read(leftBuffer, count) != count || right.read(rightBuffer, count) != count)
        {
            error = "Could not read the whole file for comparison";
            return false;
        }
        if (std::memcmp(leftBuffer, rightBuffer, (size_t) count) != 0) return false;
        remaining -= count;
    }
    // A link whose target changed after the size check still reads through the open streams.
    return true;
}

DuplicateCleanupService::DuplicateCleanupService() : Thread("Duplicate cleanup") {}
DuplicateCleanupService::~DuplicateCleanupService()
{
    cancel();
    waitForThreadToExit(-1);
}

bool DuplicateCleanupService::start(const std::vector<DuplicateCleanupJob>& next,
                                    juce::File journalFile)
{
    if (isThreadRunning() && ! waitForThreadToExit(2000)) return false;
    if (next.empty()) return false;
    {
        std::lock_guard lock(stateMutex);
        jobs = next;
        journal = journalFile == juce::File() ? defaultJournalFile() : journalFile;
        state = {};
        state.running = true;
        state.total = (int) jobs.size();
        state.current = "Preparing";
        fileResults.clear();
    }
    cancelled = false;
    recoverInterrupted(journal);
    if (! startThread())
    {
        std::lock_guard lock(stateMutex);
        state.running = false;
        state.current = "Cannot start the cleanup thread";
        return false;
    }
    return true;
}
void DuplicateCleanupService::cancel() { cancelled = true; }
DuplicateCleanupProgress DuplicateCleanupService::progress() const
{
    std::lock_guard lock(stateMutex);
    return state;
}
std::vector<DuplicateCleanupResult> DuplicateCleanupService::results() const
{
    std::lock_guard lock(stateMutex);
    return fileResults;
}

juce::StringArray DuplicateCleanupService::recoverInterrupted(const juce::File& journalFile)
{
    juce::StringArray notes;
    auto entries = readJournal(journalFile);
    if (entries.empty()) return notes;
    std::vector<JournalEntry> remaining;
    for (const auto& entry : entries)
    {
        const auto staged = entry.staging;
        const auto link = entry.link;
        if (! staged.existsAsFile())
        {
            // The staged copy is already gone: either the link was committed or the file was
            // removed externally. Nothing to restore.
            continue;
        }
        if (! link.exists())
        {
            if (staged.moveFileTo(link))
                notes.add("Restored " + link.getFullPathName());
            else
            {
                notes.add("Could not restore " + link.getFullPathName()
                          + "; the original copy is at " + staged.getFullPathName());
                remaining.push_back(entry);
            }
            continue;
        }
        // The link is in place and the staged copy is redundant. Keep both when the link does
        // not point at the expected original: that needs a person, not an automatic choice.
        if (link.isSymbolicLink() && link.getLinkedTarget() == entry.original)
        {
            if (! staged.deleteFile())
            {
                notes.add("Old copy could not be removed: " + staged.getFullPathName());
                remaining.push_back(entry);
            }
        }
        else
        {
            notes.add("Left both files in place for review: " + link.getFullPathName()
                      + " and " + staged.getFullPathName());
            remaining.push_back(entry);
        }
    }
    writeJournal(journalFile, remaining);
    return notes;
}

void DuplicateCleanupService::run()
{
    JournalGuard guard;
    {
        std::lock_guard lock(stateMutex);
        guard.file = journal;
    }
    guard.entries = readJournal(guard.file);
    int processed = 0, removed = 0, linked = 0, skipped = 0, failed = 0;
    int64_t freedBytes = 0, trashedBytes = 0;
    std::vector<DuplicateCleanupResult> results;
    for (const auto& job : jobs)
    {
        if (cancelled)
        {
            DuplicateCleanupResult result;
            result.job = job;
            result.state = DuplicateCleanupState::cancelled;
            result.message = "Cancelled";
            results.push_back(result);
            ++skipped;
            continue;
        }
        {
            std::lock_guard lock(stateMutex);
            state.current = job.copy.getFileName();
        }
        DuplicateCleanupResult result;
        result.job = job;
        // Measured now: after Trash or a permanent delete the file may no longer report a size.
        result.bytes = job.copy.existsAsFile() ? job.copy.getSize() : 0;
        const auto fail = [&result](juce::String message)
        {
            result.state = DuplicateCleanupState::failed;
            result.message = std::move(message);
        };
        if (job.copy.isSymbolicLink())
        {
            result.state = DuplicateCleanupState::skipped;
            result.message = "Already a symbolic link";
        }
        else if (job.original.isSymbolicLink())
        {
            // A link must point at real content, never at another link.
            result.state = DuplicateCleanupState::skipped;
            result.message = "The kept original is itself a symbolic link";
        }
        else if (job.original == job.copy)
        {
            result.state = DuplicateCleanupState::skipped;
            result.message = "The kept original and the copy are the same path";
        }
        else if (! job.original.existsAsFile() || ! job.copy.existsAsFile())
        {
            result.state = DuplicateCleanupState::skipped;
            result.message = "One of the files is missing";
        }
        else
        {
            juce::String compareError;
            if (! filesAreIdentical(job.original, job.copy, compareError))
            {
                result.state = DuplicateCleanupState::skipped;
                result.message = compareError.isNotEmpty() ? compareError
                                                           : "The files are no longer identical";
            }
            else if (job.action == DuplicateAction::ReplaceWithLink)
            {
                const JournalEntry entry {
                    job.copy.getSiblingFile("." + job.copy.getFileName() + ".sonicator-cleanup-"
                                            + juce::Uuid().toString()),
                    job.copy, job.original };
                if (! job.copy.moveFileTo(entry.staging))
                {
                    fail("Cannot move the copy aside: " + job.copy.getFullPathName());
                }
                else
                {
                    guard.add(entry);
                    // JUCE names the link by the argument; `this` is the link's target.
                    if (! job.original.createSymbolicLink(job.copy, false))
                    {
                        entry.staging.moveFileTo(job.copy);
                        guard.remove(entry);
                        fail("Cannot create a symbolic link here; the copy was restored");
                    }
                    else if (! job.copy.isSymbolicLink()
                             || job.copy.getLinkedTarget() != job.original)
                    {
                        job.copy.deleteFile();
                        entry.staging.moveFileTo(job.copy);
                        guard.remove(entry);
                        fail("The new link does not resolve to the original; the copy was restored");
                    }
                    else if (! entry.staging.deleteFile())
                    {
                        // The link works and the content still exists at the original. Leaving the
                        // staged copy and its journal entry lets the next start retry removal.
                        result.state = DuplicateCleanupState::linked;
                        result.message = "Linked, but the old copy remains at "
                                       + entry.staging.getFullPathName();
                    }
                    else
                    {
                        guard.remove(entry);
                        result.state = DuplicateCleanupState::linked;
                    }
                }
            }
            else
            {
                const auto policy = job.action == DuplicateAction::DeletePermanently
                    ? DisposalPolicy::DeletePermanently : DisposalPolicy::Trash;
                const auto outcome = disposeFile(job.copy, policy);
                if (outcome.ok) result.state = DuplicateCleanupState::removed;
                else fail(outcome.error);
            }
        }
        switch (result.state)
        {
            case DuplicateCleanupState::removed:
                ++removed;
                if (job.action == DuplicateAction::Trash) trashedBytes += result.bytes;
                else freedBytes += result.bytes;
                break;
            case DuplicateCleanupState::linked:
                ++linked;
                // The old bytes are deleted after the link was verified; the content stays at the
                // kept original, so the disk usage of this path is gone.
                freedBytes += result.bytes;
                break;
            case DuplicateCleanupState::cancelled: break;
            case DuplicateCleanupState::skipped: ++skipped; break;
            case DuplicateCleanupState::failed: ++failed; break;
        }
        results.push_back(result);
        ++processed;
        {
            std::lock_guard lock(stateMutex);
            state.processed = processed;
            state.removed = removed;
            state.linked = linked;
            state.skipped = skipped;
            state.failed = failed;
            state.freedBytes = freedBytes;
            state.trashedBytes = trashedBytes;
        }
    }
    std::lock_guard lock(stateMutex);
    fileResults = std::move(results);
    state.running = false;
    state.cancelled = cancelled;
    state.current.clear();
}
} // namespace sonicator::files
