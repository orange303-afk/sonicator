#pragma once
#include <juce_core/juce_core.h>

namespace sonicator::diagnostics
{
/** Where the application writes its rolling log. macOS keeps it in ~/Library/Logs, other platforms
    in the per-user application data directory. Nothing is ever uploaded automatically. */
juce::File defaultLogFile();

/** Written by the crash handler and read once on the next start. */
juce::File defaultCrashMarker();

/** Installs the file logger and the crash handler. Safe to call once per process. */
void install();
void uninstall();

/** One live file plus up to `keep` rotated copies (`sonicator.1.log`, ...). */
void rotateIfNeeded (const juce::File& logFile, int64_t maxBytes = 2 * 1024 * 1024, int keep = 1);

/** Replaces the catalog path, the home directory and the account name, so a report can be pasted in
    public without leaking where the library lives. */
juce::String redact (juce::String text, const juce::File& catalogFile = {});

/** The last lines of a log file, capped by both line count and character count. */
juce::String readTail (const juce::File& logFile, int maxLines, int maxChars);

/** readTail plus redaction. */
juce::String logTail (const juce::File& logFile, const juce::File& catalogFile,
                      int maxLines = 60, int maxChars = 6000);

/** Text of a crash left by the previous session, redacted; empty when there is none. */
juce::String pendingCrash (const juce::File& marker, const juce::File& catalogFile = {});
void clearPendingCrash (const juce::File& marker);

enum class ReportKind { bug, feature, formatSupport };

struct ReportContext
{
    juce::String appVersion;
    juce::File catalogFile;
};

juce::String templateName (ReportKind kind);
juce::String defaultTitle (ReportKind kind);

/** The issue body: environment, the user's own text placeholder, the log tail and, when present, the
    previous crash. Redacted and length-capped so the URL stays usable in a browser. */
juce::String reportBody (ReportKind kind, const ReportContext& context,
                         const juce::File& logFile, const juce::File& marker);

/** Prefilled "new issue" URL for github.com/orange303-afk/sonicator. The file arguments default to the
    live log and crash marker; tests pass temporary files. */
juce::String issueUrl (ReportKind kind, const ReportContext& context,
                       const juce::File& logFile = defaultLogFile(),
                       const juce::File& marker = defaultCrashMarker());
} // namespace sonicator::diagnostics
