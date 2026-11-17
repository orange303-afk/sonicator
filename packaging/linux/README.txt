SONICATOR — application

This build is not signed and not distributed by a Linux distribution's package
repository. The AppImage runs from any directory without root; the .deb installs
the same files through apt.

The application owns the library: on first launch it creates
~/.config/Sonicator/sonicator.sqlite and indexes folders you add. Exported audio
and the drag cache live under ~/.config/Sonicator/exports and are never cleaned
up automatically: DAW projects may reference them.

A VST3 plug-in for Linux is not part of this release.

Audio uses ALSA by default; JACK (and PipeWire's JACK layer) is offered when the
client library is installed. A missing device is reported in Audio/MIDI Settings
rather than hidden.

The package does not touch your library, settings or exported audio on upgrade or
removal.
