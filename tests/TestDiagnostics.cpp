#include <catch2/catch_test_macros.hpp>
#include "core/diagnostics/Diagnostics.h"
#include "TestHelpers.h"

using namespace sonicator;

namespace
{
juce::File homeDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory);
}

juce::File catalogUnderHome()
{
    return homeDirectory().getChildFile ("Library/Sonicator/sonicator.sqlite");
}
}

TEST_CASE ("Log rotation keeps one previous copy and never grows the folder", "[diagnostics]")
{
    TestDirectory t;
    const auto log = t.dir.getChildFile ("sonicator.log");
    const auto previous = t.dir.getChildFile ("sonicator.1.log");
    log.replaceWithText ("1234567890", false, false, "\n");

    diagnostics::rotateIfNeeded (log, 10, 1);
    REQUIRE_FALSE (log.existsAsFile());
    REQUIRE (previous.existsAsFile());
    REQUIRE (previous.loadFileAsString() == "1234567890");

    // A second rotation replaces the older copy instead of adding a third file.
    log.replaceWithText ("abcdefghij", false, false, "\n");
    diagnostics::rotateIfNeeded (log, 10, 1);
    REQUIRE (previous.loadFileAsString() == "abcdefghij");
    REQUIRE_FALSE (t.dir.getChildFile ("sonicator.2.log").existsAsFile());

    // Below the limit nothing moves.
    log.replaceWithText ("short", false, false, "\n");
    diagnostics::rotateIfNeeded (log, 10, 1);
    REQUIRE (log.existsAsFile());
    REQUIRE (log.loadFileAsString() == "short");
}

TEST_CASE ("A log tail is capped by lines and characters", "[diagnostics]")
{
    TestDirectory t;
    const auto log = t.dir.getChildFile ("sonicator.log");
    REQUIRE (diagnostics::readTail (log, 10, 1000).isEmpty());

    juce::StringArray lines;
    for (int i = 0; i < 50; ++i) lines.add ("line " + juce::String (i));
    log.replaceWithText (lines.joinIntoString ("\n"), false, false, "\n");

    const auto tail = diagnostics::readTail (log, 5, 100000);
    REQUIRE (tail.contains ("line 49"));
    REQUIRE (tail.contains ("line 45"));
    REQUIRE_FALSE (tail.contains ("line 44"));

    REQUIRE (diagnostics::readTail (log, 1000, 20).length() == 20);
}

TEST_CASE ("Reports do not carry the home directory or the catalog path", "[diagnostics]")
{
    const auto catalog = catalogUnderHome();
    const auto home = homeDirectory().getFullPathName();

    const auto redacted = diagnostics::redact ("opened " + catalog.getFullPathName()
                                                   + " and " + home + "/Music/kick.wav", catalog);
    REQUIRE_FALSE (redacted.contains (home));
    REQUIRE (redacted.contains ("<catalog>"));
    REQUIRE (redacted.contains ("~/Music/kick.wav"));

    // Without a catalog the home directory is still shortened.
    REQUIRE_FALSE (diagnostics::redact (home + "/x", {}).contains (home));
}

TEST_CASE ("The pending crash is read once, redacted and then cleared", "[diagnostics]")
{
    TestDirectory t;
    const auto marker = t.dir.getChildFile ("crash.txt");
    REQUIRE (diagnostics::pendingCrash (marker).isEmpty());

    const auto home = homeDirectory().getFullPathName();
    marker.replaceWithText ("signal 11\nat 2026-11-11\n  " + home + "/libfoo.dylib\n", false, false, "\n");

    const auto crash = diagnostics::pendingCrash (marker);
    REQUIRE (crash.contains ("signal 11"));
    REQUIRE (crash.contains ("libfoo.dylib"));
    REQUIRE_FALSE (crash.contains (home));

    diagnostics::clearPendingCrash (marker);
    REQUIRE_FALSE (marker.existsAsFile());
    REQUIRE (diagnostics::pendingCrash (marker).isEmpty());
}

TEST_CASE ("A report body carries the environment, its own prompt, the log and the crash", "[diagnostics]")
{
    TestDirectory t;
    const auto log = t.dir.getChildFile ("sonicator.log");
    const auto marker = t.dir.getChildFile ("crash.txt");
    log.replaceWithText ("preview opened slowly", false, false, "\n");
    marker.replaceWithText ("signal 6", false, false, "\n");

    diagnostics::ReportContext context;
    context.appVersion = "9.9.9";
    context.catalogFile = catalogUnderHome();

    const auto bug = diagnostics::reportBody (diagnostics::ReportKind::bug, context, log, marker);
    REQUIRE (bug.contains ("Sonicator 9.9.9"));
    REQUIRE (bug.contains ("Catalog: <catalog>"));
    REQUIRE (bug.contains ("Steps to reproduce"));
    REQUIRE (bug.contains ("preview opened slowly"));
    REQUIRE (bug.contains ("Crash from the previous session"));
    REQUIRE (bug.contains ("signal 6"));
    REQUIRE_FALSE (bug.contains (catalogUnderHome().getFullPathName()));

    const auto feature = diagnostics::reportBody (diagnostics::ReportKind::feature, context, log, marker);
    REQUIRE (feature.contains ("What would you like to do"));
    REQUIRE_FALSE (feature.contains ("Steps to reproduce"));

    const auto format = diagnostics::reportBody (diagnostics::ReportKind::formatSupport, context, log, marker);
    REQUIRE (format.contains ("Drag the smallest sample"));
    REQUIRE (format.contains ("What produces these files"));

    // An empty log and no crash produce no empty code blocks.
    const auto empty = diagnostics::reportBody (diagnostics::ReportKind::bug, context,
                                                t.dir.getChildFile ("missing.log"),
                                                t.dir.getChildFile ("missing.txt"));
    REQUIRE_FALSE (empty.contains ("### Log"));
    REQUIRE_FALSE (empty.contains ("Crash from the previous session"));
}

TEST_CASE ("The report URL targets the right template and leaks nothing", "[diagnostics]")
{
    TestDirectory t;
    const auto log = t.dir.getChildFile ("sonicator.log");
    const auto marker = t.dir.getChildFile ("crash.txt");
    log.replaceWithText ("log line " + homeDirectory().getFullPathName(), false, false, "\n");

    diagnostics::ReportContext context;
    context.appVersion = "9.9.9";
    context.catalogFile = catalogUnderHome();

    const auto base = juce::String ("https://github.com/orange303-afk/sonicator/issues/new?");
    const auto bug = diagnostics::issueUrl (diagnostics::ReportKind::bug, context, log, marker);
    const auto feature = diagnostics::issueUrl (diagnostics::ReportKind::feature, context, log, marker);
    const auto format = diagnostics::issueUrl (diagnostics::ReportKind::formatSupport, context, log, marker);

    REQUIRE (bug.startsWith (base));
    REQUIRE (bug.contains ("template=bug_report.md"));
    REQUIRE (feature.contains ("template=feature_request.md"));
    REQUIRE (format.contains ("template=format_support.md"));
    REQUIRE (bug.contains ("9.9.9"));
    REQUIRE_FALSE (bug.contains (homeDirectory().getFullPathName()));
    REQUIRE_FALSE (bug.contains (catalogUnderHome().getFullPathName()));
}
