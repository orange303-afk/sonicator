#import "../style.typ": *

#chapter(23, "Settings",
  lead: [One panel holds appearance, catalog maintenance, editing policy, audio devices, keys, plug-ins and the three actions that can reach the network.],
  inthis: (
    [Theme, interface scale and language],
    [Move, open, relocate and back up the catalog],
    [Editing policy and workspace reset],
    [Audio & MIDI, Hotkeys and Plugins],
    [Update check, contribution and REPORT],
  ),
)[

#shot("/docs/manual/assets/settings-general.png", [Figure 23.1 — Settings → General. The catalog path is the shared database; the buttons below it move, open, relocate or back it up.], width: 100%)

Settings opens from the header. The last used page is remembered. In the DAW plug-in the panel is
embedded in the editor and the pages that need the application — audio devices, catalog switching —
are hidden.

#section("Appearance and language")

- *Theme*: System, Dark or Light. System follows the operating system and changes live.
- *Interface scale*: 100%, 110%, 125% or 150%. The chosen scale is applied to the whole window; the
  plug-in scales its own component tree, never the host.
- *Interface language*: English is the only language in this build. The setting is stored so later
  translations can use it; it changes nothing yet.

#section("The catalog")

The *Library* block shows the current database path and four actions:

- *Move catalog…* copies a consistent snapshot to a new location and switches to it. The previous file
  is left in place, not deleted, so you can go back.
- *Open catalog…* opens an existing SONICATOR catalog. A file that is not a catalog is refused with a
  message.
- *Relocate folders…* maps old roots to new ones after the samples moved. Each old root is paired with
  a new folder, relative structure is preserved, and analysis, tags, stars, hashes and metadata are
  kept. A `<catalog>.relocate-<timestamp>.sqlite` backup is written before anything is rewritten.
- *Backup database* writes a consistent SQLite copy to `Sonicator/backups/` beside the database. The
  live file and its WAL are never copied as raw files.

#warning[
  There is no automatic backup rotation. A very large catalog can pause the window while the copy runs,
  because the backup goes through SQLite on the UI thread. Make a backup before a big relocation or a
  rescan, not during one.
]

Relocation checks a sample of 200 paths and warns when the chosen folder looks wrong. Folder structure
inside each root must be preserved: the tool rewrites stored paths, it does not copy audio. Running it
twice on the same library is unnecessary; one successful relocation per machine is enough.

#section("Editing")

*Always overwrite original files* makes *Save* in the waveform editor and *Normalize selected* replace
a WAV, AIFF or FLAC without asking. Other formats still save as a new copy. The prompt for unsaved
edits before leaving a file stays on either way.

Leave this off until you trust the editor. With it off, both actions ask with the file name and the
choice between overwrite, save copy and cancel.

#section("Workspace")

*Reset panel layout* restores the folder, Analysis and preview dividers to their defaults, plus the
Favorites height. It does not touch the window size, table columns or shortcuts. Double-clicking a
single divider restores only that panel.

#section("Audio & MIDI")

This page selects the output device and, for MIDI Preview, the input devices. The application opens no
microphone input. The plug-in has no device page at all: the host owns the device.

#section("Hotkeys and Plugins")

*Hotkeys* is the command browser described in chapter 24. *Plugins* lists the installed plug-in
components and their status. Neither page changes what the library contains.

#section("Update check")

*Settings → About → Check for updates* asks `api.github.com` for the newest published release and
compares it with the running version. The request carries the application version in its user agent
and the project name in the URL. It sends no catalog, file, path, machine or account data. The
application never checks on its own, and a failed check changes nothing.

#section("Contribute anonymous feedback")

*Settings → General → Library → Contribute anonymous feedback to improve classification models* is off
until you answer the question shown once at startup. While it is off, nothing is sent.

When it is on, a background request about once a day may carry:

- 14 acoustic measurements of a sample, rounded to 0.001;
- the type or tags you confirmed, and whether each is built in or your own;
- key and tempo when known;
- a random record id, the application version and `mac` or `win`;
- a random installation id created when you opted in.

It never carries audio, file name, folder or pack name, path, file hash, your name, computer name,
e-mail or address. An IP address is used for rate limiting and is not stored. Only rows recorded from
your own action are sent: a manual edit, or an AutoTag proposal you reviewed and applied. Tags applied
without review, and rows written before this version, stay local.

*What will be sent* shows the exact JSON of the next request. *Delete my contributed data* removes
every batch under your installation id, issues a new id and turns contribution off. A dataset or model
already released is rebuilt from the bucket each time, so the deleted data leaves the next release;
a copy someone already downloaded cannot be recalled.

A custom tag is text you typed, so it is the one field that could carry something personal by
accident. Tags that look like a path, an address or a URL are dropped, and tags are limited to 32
characters.

#section("REPORT")

*Settings → REPORT* opens a pre-filled GitHub issue in your browser. It contains the version, the
operating system, the last 60 log lines and, if the previous session crashed, its backtrace. Your home
folder becomes `~`, the catalog path becomes `<catalog>` and your account name becomes `<user>`. File
names stay, because a report without the failing file is rarely actionable. You read and edit the
report and submit it yourself; the application never posts it.

]
