# Linux acceptance

Recorded by the owner on the Ubuntu stand (`192.168.2.89`, Ubuntu 24.04.5, Celeron N4100, ALC269VC
analog output). Status: **not run**. Nothing in the README claims these results until they are filled
in here.

Each line is filled with the machine's own result, not with an expectation. A line that was not
tried stays `Not run` instead of being removed.

## Install and launch

| Check | Result |
|---|---|
| `.deb` installs with `sudo apt install ./SONICATOR-<version>-Linux-amd64.deb` | Not run |
| Desktop entry appears and launches the application | Not run |
| AppImage runs from a download folder without root | Not run |
| First launch creates `~/.config/Sonicator/sonicator.sqlite` | Not run |
| Second launch reuses the catalog and the window position | Not run |

## Library

| Check | Result |
|---|---|
| Adding a real folder tree indexes its audio files | Not run |
| Search, tags, stars and duplicate grouping behave as on macOS | Not run |
| Folder names differing only by case stay separate | Not run |
| Rescan picks up an added file | Not run |

## Audio

| Check | Result |
|---|---|
| Preview plays through ALSA (PipeWire's ALSA path) | Not run |
| Preview plays through JACK (`pipewire-jack`) | Not run |
| Audio/MIDI Settings lists both device types | Not run |
| Switching device while a preview plays keeps the player usable | Not run |
| Hover audition in the Cloud view is audible and does not stutter | Not run |
| Unplugging the output device stops playback without wedging the app | Not run |

## Export and drag

| Check | Result |
|---|---|
| Export writes a WAV to `~/.config/Sonicator/exports` | Not run |
| Selection export writes exactly the selected range | Not run |
| Dragging a library row drops a file into REAPER | Not run |
| Dragging a library row drops a file into Bitwig Studio | Not run |
| Dragging a library row drops a file into Ardour | Not run |
| Dragging a waveform selection drops the prepared WAV | Not run |
| Copy writes newline-separated paths that paste into a terminal | Not run |

## Interface

| Check | Result |
|---|---|
| Window, panels and dialogs match the macOS look | Not run |
| Interface scale 100 %, 125 % and 200 % under HiDPI | Not run |
| X11 session behaves as expected | Not run |
| Wayland session (through XWayland) behaves as expected | Not run |
| Cloud view draws and hovers as on macOS | Not run |
| Keyboard shortcuts use Ctrl where macOS uses Cmd | Not run |

## Known limits at the time of writing

- No VST3 plug-in for Linux in this release.
- Copy carries paths as text; a native file clipboard is not implemented.
- Nothing is signed, and no distribution repository is offered: upgrades are a manual `.deb`
  install or a replaced AppImage.
