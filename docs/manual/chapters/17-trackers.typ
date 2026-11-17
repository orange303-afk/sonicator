#import "../style.typ": *

#chapter(17, "Tracker modules and XI",
  lead: [MOD, XM and S3M songs play as songs, not as one waveform; their samples can be extracted one by one. FastTracker II XI instruments open as a single sample.],
  inthis: (
    [How a module is recognised],
    [Whole song versus a source sample],
    [MIDI audition and export],
    [Coverage and the variants that are refused],
  ),
)[

#section("Recognition")

Tracker files are recognised from their content, not their extension: a 31-sample MOD signature, a
structurally valid 15-sample MOD, an XM header or an S3M `SCRM` header. A renamed S3M under a `.mod`
name opens as S3M; an unrecognised `.mod` is catalogued with an explicit error instead of an audio
row. Scanning only probes headers; no module is rendered during a scan.

#section("Whole song")

Opening a module selects *Whole song*. *Auto Preview* starts the composition; a silent session start
loads it without playback. The header shows the detected format, channel count, sample-slot count and
an estimated duration from the player.

There is no song overview waveform. The waveform area shows a full-height progress and scrub bar with
current and total time (`00:14:787 / 04:47:647`); click or drag anywhere to seek. Play, Pause, Stop
and the seek keys apply to the composition. *Loop* follows the preview toggle.

Selecting a sample from the selector stops the song and keeps its position. The waveform then shows
that sample's PCM, and Play auditions exactly that source at its own rate. Returning to *Whole song*
resumes from the kept position; *Stop* resets the composition to the beginning.

#section("MIDI and export")

MIDI Preview plays the *selected sample chromatically*, not the module's instrument map: note 60 at
the sample's reference rate, 128 preallocated voices, a 2 ms attack and a 2-second NoteOff release.
A tracker loop is used only when *Loop* was on at note-on.

Export uses the waveform selection, or the *Drag sample* button when no range is selected. The WAV
carries the decoded source frames at the reference rate and channel count, with no tracker volume,
transpose, velocity, pan, envelope, effects or resampling. 8-bit MOD/S3M/XM sources are widened to
16-bit WAV; wider sources are not reduced.

#section("XI")

An XI file is one FastTracker II instrument: a fixed header, one sample header and delta-encoded PCM.
It is not a song. SONICATOR opens it as a tracker document with a single source slot, plays it through
the ordinary sample path and extracts the PCM. The 40-byte sample header and delta encoding are the
same shape as XM.

#section("Coverage")

- MOD: 15- and 31-sample layouts, `M.K.`, `M!K!`, `FLT4`, `FLT8`, `OCTA`/`OKTA`, `CD81`, `xCHN`,
  `xxCH` and `xxCN` tags, signed 8-bit PCM, finetune tuning, forward loops. A small payload shortfall
  is clamped and reported as truncated; larger truncation is refused.
- XM: versions 0x0102–0x0104, the standard instrument and sample headers, instrument-local sample
  lists, relative note and finetune, forward and ping-pong loops. Stereo-stored samples are reported
  as unsupported.
- S3M: sample instruments, signed and unsigned 8/16-bit PCM, C2 speed, forward loops and `SCRS`
  headers. AdLib instruments and packed samples are listed with a reason.

MT2, IT, STM, MTM, 669 and the other rare families are not claimed. There is no tracker editing,
pattern display, stem export, Amiga filter emulation or exact historical-player equivalence.

#note[
  Playback runs libxmp on a worker and fills a bounded ring; the audio callback only copies from it.
  A slow or pathological module cannot block the audio device.
]

]
