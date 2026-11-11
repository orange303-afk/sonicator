#include "core/diagnostics/Diagnostics.h"
#include <atomic>
#include <mutex>

namespace sonicator::diagnostics
{
namespace
{
constexpr const char* issueBaseUrl = "https://github.com/orange303-afk/sonicator/issues/new";
constexpr int64_t logLimitBytes = 2 * 1024 * 1024;
constexpr int logKeepCount = 1;
constexpr int bodyCharLimit = 7000;

std::mutex& logMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::unique_ptr<juce::Logger>& installedLogger()
{
    static std::unique_ptr<juce::Logger> logger;
    return logger;
}

std::atomic<bool>& installed()
{
    static std::atomic<bool> flag { false };
    return flag;
}

juce::String timestamp()
{
    return juce::Time::getCurrentTime().formatted ("[%Y-%m-%d %H:%M:%S] ");
}

juce::File rotatedFile (const juce::File& logFile, int index)
{
    return logFile.getSiblingFile (logFile.getFileNameWithoutExtension()
                                   + "." + juce::String (index) + logFile.getFileExtension());
}

void rotateLocked (const juce::File& logFile, int keep)
{
    if (keep <= 0)
    {
        logFile.deleteFile();
        return;
    }
    rotatedFile (logFile, keep).deleteFile();
    for (int index = keep - 1; index >= 1; --index)
    {
        const auto older = rotatedFile (logFile, index);
        if (older.existsAsFile()) older.moveFileTo (rotatedFile (logFile, index + 1));
    }
    if (logFile.existsAsFile()) logFile.moveFileTo (rotatedFile (logFile, 1));
}

/** Appends with a timestamp and rotates before the file grows past its limit. The lock is held for
    the whole write so lines from different threads never interleave. */
class RotatingFileLogger final : public juce::Logger
{
public:
    RotatingFileLogger (juce::File file, int64_t limit, int keep)
        : logFile (std::move (file)), maxBytes (limit), keepCount (keep) {}

    void logMessage (const juce::String& message) override
    {
        const std::lock_guard<std::mutex> guard (logMutex());
        try
        {
            logFile.getParentDirectory().createDirectory();
            if (logFile.existsAsFile() && logFile.getSize() >= maxBytes)
                rotateLocked (logFile, keepCount);
            logFile.appendText (timestamp() + message + "\n", false, false, "\n");
        }
        catch (...) {}
    }

private:
    juce::File logFile;
    int64_t maxBytes;
    int keepCount;
};

/** Runs inside a dying process: no locks, no logging, only a small marker file. JUCE terminates the
    process after this returns, so the file just has to reach the filesystem. */
void crashHandler (void* context)
{
    try
    {
        const auto signalNumber = (int) (juce::pointer_sized_int) context;
        const auto marker = defaultCrashMarker();
        marker.getParentDirectory().createDirectory();
        juce::String text;
        text << "signal " << signalNumber << "\n"
             << "at " << juce::Time::getCurrentTime().toString (true, true) << "\n"
             << juce::SystemStats::getStackBacktrace() << "\n";
        marker.replaceWithText (text, false, false, "\n");
    }
    catch (...) {}
}
} // namespace

juce::File defaultLogFile()
{
   #if JUCE_MAC || JUCE_IOS
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory)
               .getChildFile ("Library/Logs/Sonicator/sonicator.log");
   #else
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Sonicator/logs/sonicator.log");
   #endif
}

juce::File defaultCrashMarker()
{
    return defaultLogFile().getSiblingFile ("crash.txt");
}

void install()
{
    if (installed().exchange (true)) return;
    auto& logger = installedLogger();
    logger = std::make_unique<RotatingFileLogger> (defaultLogFile(), logLimitBytes, logKeepCount);
    juce::Logger::setCurrentLogger (logger.get());
    juce::SystemStats::setApplicationCrashHandler (crashHandler);
}

void uninstall()
{
    if (! installed().exchange (false)) return;
    juce::Logger::setCurrentLogger (nullptr);
    installedLogger().reset();
    // JUCE offers no way to remove the crash handler; the process is shutting down anyway.
}

void rotateIfNeeded (const juce::File& logFile, int64_t maxBytes, int keep)
{
    const std::lock_guard<std::mutex> guard (logMutex());
    if (logFile.existsAsFile() && logFile.getSize() >= maxBytes) rotateLocked (logFile, keep);
}

juce::String redact (juce::String text, const juce::File& catalogFile)
{
    // The catalog is usually inside the home directory, so it has to be replaced first.
    const auto catalog = catalogFile.getFullPathName();
    if (catalog.isNotEmpty()) text = text.replace (catalog, "<catalog>");
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName();
    if (home.isNotEmpty()) text = text.replace (home, "~");
    const auto account = juce::SystemStats::getLogonName();
    if (account.length() > 2) text = text.replace (account, "<user>");
    return text;
}

juce::String readTail (const juce::File& logFile, int maxLines, int maxChars)
{
    if (! logFile.existsAsFile()) return {};
    juce::StringArray lines;
    lines.addLines (logFile.loadFileAsString());
    while (lines.size() > juce::jmax (0, maxLines)) lines.remove (0);
    auto text = lines.joinIntoString ("\n");
    if (maxChars >= 0 && text.length() > maxChars) text = text.substring (text.length() - maxChars);
    return text;
}

juce::String logTail (const juce::File& logFile, const juce::File& catalogFile, int maxLines, int maxChars)
{
    return redact (readTail (logFile, maxLines, maxChars), catalogFile);
}

juce::String pendingCrash (const juce::File& marker, const juce::File& catalogFile)
{
    if (! marker.existsAsFile()) return {};
    return redact (marker.loadFileAsString(), catalogFile).trim();
}

void clearPendingCrash (const juce::File& marker)
{
    marker.deleteFile();
}

juce::String templateName (ReportKind kind)
{
    switch (kind)
    {
        case ReportKind::bug:           return "bug_report.md";
        case ReportKind::feature:       return "feature_request.md";
        case ReportKind::formatSupport: return "format_support.md";
    }
    return "bug_report.md";
}

juce::String defaultTitle (ReportKind kind)
{
    switch (kind)
    {
        case ReportKind::bug:           return "Bug report";
        case ReportKind::feature:       return "Feature request";
        case ReportKind::formatSupport: return "Format or bank support";
    }
    return "Bug report";
}

juce::String reportBody (ReportKind kind, const ReportContext& context,
                         const juce::File& logFile, const juce::File& marker)
{
    juce::String body;
    body << "### Environment\n"
         << "- Sonicator " << (context.appVersion.isNotEmpty() ? context.appVersion : "unknown") << "\n"
         << "- " << juce::SystemStats::getOperatingSystemName();
   #if JUCE_ARM
    body << " (arm64)";
   #elif JUCE_INTEL
    body << " (x86_64)";
   #endif
    body << "\n"
         << "- Catalog: " << (context.catalogFile.getFullPathName().isNotEmpty() ? "<catalog>" : "none open")
         << "\n\n";

    switch (kind)
    {
        case ReportKind::bug:
            body << "### What happened\n\n\n"
                 << "### What you expected\n\n\n"
                 << "### Steps to reproduce\n1. \n2. \n3. \n\n";
            break;
        case ReportKind::feature:
            body << "### What would you like to do\n\n\n"
                 << "### Why it matters to you\n\n";
            break;
        case ReportKind::formatSupport:
            body << "### Format and extension\n\n\n"
                 << "### What produces these files\n\n\n"
                 << "### Sample file\n"
                 << "Drag the smallest sample into this issue. Do not attach commercial content you "
                 << "are not allowed to share; a short single-note export is enough.\n\n";
            break;
    }

    const auto log = logTail (logFile, context.catalogFile);
    if (log.isNotEmpty())
        body << "### Log\n```text\n" << log << "\n```\n\n";

    const auto crash = pendingCrash (marker, context.catalogFile);
    if (crash.isNotEmpty())
        body << "### Crash from the previous session\n```text\n" << crash << "\n```\n";

    if (body.length() > bodyCharLimit)
        body = body.substring (0, bodyCharLimit)
             + "\n\n(Report truncated. The full log file is at "
             + redact (logFile.getFullPathName(), context.catalogFile) + ".)\n";
    return body;
}

juce::String issueUrl (ReportKind kind, const ReportContext& context,
                       const juce::File& logFile, const juce::File& marker)
{
    auto url = juce::URL (issueBaseUrl)
                   .withParameter ("template", templateName (kind))
                   .withParameter ("title", defaultTitle (kind))
                   .withParameter ("body", reportBody (kind, context, logFile, marker));
    return url.toString (true);
}
} // namespace sonicator::diagnostics
