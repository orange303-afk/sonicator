# Third-party components

SONICATOR is distributed under the GNU Affero General Public License v3.0 or later; see
[LICENSE](LICENSE). The binary also contains the third-party code listed here. Each entry names the
version that is compiled into the build, how it is obtained, and where its licence text lives.

This file is an inventory for distribution, not legal advice. `docs/DEPENDENCIES.md` explains why each
component is present and what the project does not ship.

## Compiled into the application, the tests and the plugin

| Component | Version | Included as | Licence | Licence text |
|---|---|---|---|---|
| JUCE | 9.0.5 | FetchContent, static modules | AGPL-3.0-only or JUCE commercial | <https://github.com/juce-framework/JUCE/blob/9.0.5/LICENSE.md> |
| SQLite | 3.47.2 | `resources/sqlite/` amalgamation | Public domain | <https://sqlite.org/copyright.html> |
| libxmp | 4.6.2 | FetchContent, linked statically | LGPL-2.1-or-later | <https://github.com/libxmp/libxmp/blob/libxmp-4.6.2/docs/COPYING> |
| ONNX Runtime | 1.20.1 | Prebuilt release binary (macOS universal2, Windows x64, Linux x64) | MIT | <https://github.com/microsoft/onnxruntime/blob/v1.20.1/LICENSE.txt> |
| zlib | system copy on macOS (1.2.12 in the SDK), fetched 1.3.1 on Windows | Linked by JUCE and by `core/sampler/FileInstrument.cpp` | Zlib | <https://zlib.net/zlib_license.html> |

### libxmp and the LGPL

libxmp is linked statically. The project compiles it with depackers and the ProWizard loader disabled
(`LIBXMP_DISABLE_DEPACKERS`, `LIBXMP_DISABLE_PROWIZARD`) and does not
modify its source. Distribution therefore keeps libxmp's source and licence available, and anyone who
receives the binary may relink it against a modified libxmp. The libxmp sources are fetched from
<https://github.com/libxmp/libxmp> at tag `libxmp-4.6.2` by the build.

## Third-party code inside JUCE modules

The JUCE modules compiled by this project bundle the code below. Names, versions and licence identifiers
are taken from `JUCE.spdx.json` in the JUCE 9.0.5 sources.

| Component | Version | Licence |
|---|---|---|
| zlib | 1.3.2 | Zlib |
| HarfBuzz | 15.0.0 | MIT (Modern Variant) |
| SheenBidi | 2.9.0 | Apache-2.0 |
| LunaSVG | 3.5.0 | MIT |
| PlutoVG | 1.3.2 | MIT |
| libpng | 1.6.58 | libpng-2.0 |
| jpeglib | 10.0 | IJG |
| libwebp | 1.6.0 | BSD-3-Clause |
| Chromium `cubic_bezier` | pinned commit | BSD-3-Clause |
| FLAC | 1.5.0 | BSD-3-Clause |
| libogg | 1.3.6 | BSD-3-Clause |
| libvorbis | 1.3.7 | BSD-3-Clause |
| Opus / opusfile / libopusenc | 1.6.1 / 0.12 / 0.3 | BSD-3-Clause |
| VST3 SDK | 3.8.0 | MIT |
| AudioUnitSDK | 1.1.0 | Apache-2.0 |

Full per-file notices, including the copyright lines each licence requires, ship with the JUCE sources:
`JUCE.spdx.json` and the `LICENSE.md` files inside each module's source directory. A binary distribution
must keep those notices next to the binary.

## Development and test only (not in the shipped binary)

| Component | Version | Licence | Licence text |
|---|---|---|---|
| Catch2 | 3.7.1 | Boost Software License 1.0 | <https://github.com/catchorg/Catch2/blob/v3.7.1/LICENSE.txt> |

## Reference implementations

| Source | Used for | Licence |
|---|---|---|
| [VelociLoops](https://github.com/kunitoki/VelociLoops) | The DWOP predictive decoder in `core/rex/Rex2Decoder.cpp` is adapted from `src/velociloops.cpp` | The Unlicense (public-domain dedication) |
| [samplerdisc](https://github.com/bmxcode/samplerdisc) | E-mu Emulator X `.ebl` format documentation only (`docs/formats/emu-ebl.md`, `sample/emu_ebl.py`); the C++ decoder in `core/emu/` is an independent implementation, no code copied, linked or executed | MIT |
| Kurzweil «KurzFile» notes (Geoffrey Mayer) | Kurzweil K2000/K2500/K2600 `.krz` object layout, keymap entry methods and sample header fields, recorded in `docs/KURZWEIL_KRZ.md`; the reader in `core/kurzweil/` is an independent implementation, no code copied | Publicly circulated format notes |
| [acidcat «krz anatomy»](https://hed0rah.github.io/audio_files_anatomy/krz-anatomy.html) | Object framing, keymap methods, program segment table and the sample-period encoding; documentation only | Reference documentation |

No other source code was copied into this repository. Format specifications and public documentation were
used as references; they are not redistributed.

## Media and branding

All audio fixtures in `tests/fixtures/` are generated tones created for this project. The application
icon and wordmark in `resources/branding/` are original artwork by the project author.

## Training data

The shipped global model is built from public audio datasets and from contributed records. Every
dataset, its version, its licence and the attribution line the licence requires are listed in
[docs/DATA_CREDITS.md](docs/DATA_CREDITS.md); that file is generated from
`services/training/datasets/manifest.json`. No dataset audio is redistributed with this repository or
with the application: only feature statistics derived from it.
