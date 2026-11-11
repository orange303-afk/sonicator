# Diagnostics, logs and the Report button

Sonicator collects diagnostics locally and asks before anything leaves the computer. There is no
telemetry and no network call from the application: a report opens a prefilled new-issue page in the
user's own browser, and the user submits it.

## What is written locally

| File | Location | Contents |
|---|---|---|
| Log | macOS `~/Library/Logs/Sonicator/sonicator.log`, Windows `%APPDATA%\Sonicator\logs\sonicator.log` | One line per event with a timestamp. Rotated at 2 MB into `sonicator.1.log`, so the folder holds at most two files. |
| Crash marker | `crash.txt` next to the log | Signal number, time and stack backtrace, written by the crash handler. |

The log starts and stops with the application (`app/Main.cpp`): `diagnostics::install()` before any
window is created, `uninstall()` during shutdown. The background logger is a `juce::Logger` subclass
that appends under a mutex, so lines from the indexer, the smart worker and the message thread never
interleave.

The crash handler runs inside a dying process. It writes one small file and nothing else: no logger
call, because taking the log mutex after a crash could deadlock. JUCE passes the signal number and
terminates the process with `SIGKILL` after the handler returns, so the marker is the only chance to
record anything.

## The Report section in Settings

Three buttons open `github.com/orange303-afk/sonicator/issues/new` with the matching issue template
already selected:

| Button | Template | Prompt filled in the body |
|---|---|---|
| Report a bug... | `bug_report.md` | what happened, what you expected, steps |
| Request a feature... | `feature_request.md` | what you would like to do, why it matters |
| Format support... | `format_support.md` | format and extension, what produces it, the sample file |

Every report body starts with the environment (version, operating system, architecture and the catalog
placeholder), then the tail of the log (60 lines, at most 6 000 characters) and, when a crash marker is
present, the previous session's backtrace. The body is capped at 7 000 characters so the URL stays
usable; when the cap is hit, the report says where the full log file is.

Templates are markdown, not issue forms, for one reason: only markdown templates accept a prefilled
`body` parameter in the URL. GitHub issue forms ignore it.

## Privacy

Before a report is built, paths are rewritten:

- the catalog file path becomes `<catalog>`;
- the home directory becomes `~`;
- the account name becomes `<user>`.

File names inside the catalog therefore stay in the log tail. That is deliberate: a report without the
file name that failed is usually useless. The user sees the whole body in the browser and can edit or
delete any line before submitting. The panel states that nothing is sent automatically.

`contributeTrainingData` (Settings → Library) is a separate, opt-in switch and sends nothing today; it
only records the choice for a future feature.

## Tests

`tests/TestDiagnostics.cpp` (`[diagnostics]`) covers log rotation, line and character caps, path
redaction, the crash marker lifecycle, the per-kind report body and the URL, including that neither the
home directory nor the catalog path appears in it. `[ui][settings]` asserts that all three report
buttons exist and that the panel renders in both themes.

## Not implemented in this stage

- Automatic upload and duplicate grouping: both need a server holding a GitHub token, because a token
  shipped inside an open-source binary is public. Filed reports are grouped by hand for now.
- Reading macOS `~/Library/Logs/DiagnosticReports`: the app's own marker is enough and needs no system
  permission.
- Log rotation by date, and any log level configuration.
