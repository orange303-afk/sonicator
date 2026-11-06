# Audio fixtures

`tone.wav`, `tone.aiff`, `tone.flac`, `tone.mp3` are synthetic 440 Hz mono tones,
48 kHz, 0.25 s (compressed streams may report encoder delay/padding).
Generated locally with FFmpeg's lavfi sine source, not third-party sample content.
FFmpeg is only a fixture-generation tool, not an application or build dependency.

Example regeneration:

```sh
ffmpeg -f lavfi -i 'sine=frequency=440:sample_rate=48000:duration=0.25' -c:a pcm_s16le tone.wav
ffmpeg -i tone.wav -c:a pcm_s16be tone.aiff
ffmpeg -i tone.wav -c:a flac tone.flac
ffmpeg -i tone.wav -c:a libmp3lame -b:a 128k tone.mp3
```
