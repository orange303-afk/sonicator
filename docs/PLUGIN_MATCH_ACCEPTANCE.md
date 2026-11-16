# Plugin Match BPM / Match Scale — acceptance notes

Date: 2026-11-16. Branch: `stage-plugin-match`. Spec:
`docs/superpowers/specs/2026-11-16-plugin-match-bpm-scale-design.md`. Plan:
`docs/superpowers/plans/2026-11-16-plugin-match-bpm-scale.md`.

This file records what was actually run and what is still open. It is not a claim that the manual
host checklist has passed.

## Automated results

| Check | Command | Result |
|---|---|---|
| Debug, whole suite | `cmake --build --preset debug --target SonicatorTests` then `~/Library/Caches/sonicator-build/debug/tests/SonicatorTests` | exit 0, 1275 test cases, 272454 assertions |
| Release, whole suite | `cmake --build --preset release --target SonicatorTests` then `~/Library/Caches/sonicator-build/release/tests/SonicatorTests` | exit 0, 1275 test cases, 272456 assertions |
| Match subset, Release | `…/release/tests/SonicatorTests '[plugin-match]'` | exit 0, 61 test cases, 1836 assertions |
| Existing plugin suite | `…/debug/tests/SonicatorTests '[plugin]'` | exit 0, 25 test cases, 324 assertions |
| Preview editing suite | `…/debug/tests/SonicatorTests '[ui][edit]'` | exit 0, 15 test cases, 62 assertions |
| Standalone without plugin/tests | `cmake -S . -B ~/Library/Caches/sonicator-build/app-nomatch -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=OFF -DSONICATOR_BUILD_PLUGIN=OFF` then build `Sonicator` | exit 0; no Rubber Band fetched, Match code is not linked |
| VST3 + AU build (native Release) | `cmake -S . -B ~/Library/Caches/sonicator-build/plugin-match -G Ninja -DCMAKE_BUILD_TYPE=Release -DSONICATOR_BUILD_PLUGIN=ON -DBUILD_TESTING=OFF` then build targets `SonicatorPlugin_VST3 SonicatorPlugin_AU` | exit 0; the VST3 manifest step loaded the built bundle |

The Render tests use a 440 Hz sine and measure zero crossings: the 120→128 result is within 5 cents,
the Am→Cm result is 523.25 Hz within 5 cents, and lengths are exact (330750 frames for 8 s at 120
into 128). This proves length and a steady tone, not musical quality.

## Open gates

- **auval / pluginval** were not run against the new build. `auval` only validates an installed
  component and a previous SONICATOR build is installed in `~/Library/Audio/Plug-Ins`; replacing the
  owner's installed plug-in was not done. Run `auval -v aumu Sncr Orng` after an explicit install.
- **Windows CI** was not run. Rubber Band is configured for MSVC (single-file amalgamation, built-in
  FFT, no threading) but has not been compiled there.
- **Universal macOS** (`plugin-universal`) was not built; the native Release build was.
- **Listening test** (drum/bass/pad/vocal loops at 0.75/1/1.25/2 and ±3/±6 semitones) and
  preparation latency/memory numbers on 4/16-bar loops were not measured here.
- **Cubase/Logic manual checklist** (120 BPM 8 s loop → host 128 / 7.5 s, Am→Cm +3, both toggles,
  both off, reopen project, editor closed/open, two instances, drag into timeline, old files still
  open after a tempo change) is not executed.
- **Screenshots** of the transport at 100% and 125%, dark and light, were not captured.

## Known scope limits

- REX2 loops, sampler banks, tracker songs and disc entries are refused with `Unsupported source for
  Match`; they do not silently match only their first part.
- Match Scale never changes major/minor; `Mode mismatch; cannot match scale` is the honest answer.
- Match is per selected instance; batch drag with Match on is refused.
- A corrupt file at a matched key is never deleted: the renderer publishes beside it as `-r1`,
  `-r2`…; after eight corrupt names the UI reports the corruption and asks for a manual cleanup.
- The ten-minute limit is a duration check, not a frame count, so a short high-rate file is not
  refused by mistake.
- A plain application build (`SONICATOR_ENABLE_MATCH=OFF`, i.e. no plugin and no tests) does not
  fetch or compile Rubber Band and does not link the Match renderer.
- Exports under `~/Library/Sonicator/exports/match/` are project media and are never cleaned
  automatically.
