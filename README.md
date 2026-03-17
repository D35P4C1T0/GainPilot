# GainPilot

GainPilot is a native loudness auto-leveler built around
BS.1770 / EBU-R128 loudness workflows. It uses a shared C++ DSP core with
native `VST3` and `LV2` wrappers, targets Windows, Linux, and macOS for `VST3`,
and focuses on practical program loudness control with true-peak protection.

## Status

This repository is usable and builds locally, but it should still be treated as
an actively evolving project rather than a finished 1.0 release.

Current scope:

- Native `VST3`
  - Windows: custom wxWidgets editor
  - Linux: generic host UI fallback
  - macOS: native Cocoa editor
- Native `LV2`
  - Linux: custom GTK3 UI
- Mono and stereo plugin variants
- Shared DSP core across formats
- BS.1770 / EBU-R128 loudness metering
- True-peak limiting
- Cross-format state serialization
- GitHub Actions packaging for Windows and Linux

## Features

- Simplified main controls: `Target Level`, `True Peak`, `Max Gain`
- Target-based loudness auto-leveling with learned input loudness
- Input loudness meter with `Integrated` readout in the editor
- True-peak ceiling control
- Mono and stereo builds with shared behavior
- LV2 state save/restore
- VST3 and LV2 artifact packaging from CMake/CPack

## Build Requirements

Core requirements:

- CMake `3.25+`
- C++20 compiler

Linux:

- `pkg-config`
- `libebur128`
- `lv2`
- `gtk+-3.0`
- optional: `wxWidgets` if you want to build the shared Windows-oriented UI

macOS:

- Xcode Command Line Tools or another C++20-capable toolchain
- full Xcode is recommended for the cleanest Apple build flow

Windows:

- Visual Studio 2022 or another C++20-capable toolchain
- `wxWidgets` for the custom VST3 editor

VST3:

- Steinberg VST3 SDK checked out at `external/vst3sdk`

## Quick Start

Clone the VST3 SDK into the expected location:

```sh
git clone --depth 1 --recurse-submodules https://github.com/steinbergmedia/vst3sdk external/vst3sdk
```

Configure and build:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Run the local tests:

```sh
ctest --test-dir build --output-on-failure
```

Install into a staging prefix:

```sh
cmake --install build --prefix "$HOME/.local"
```

Typical install layout:

- Linux
  - `lib/lv2/*.lv2`
  - `lib/vst3/GainPilot.vst3`
- macOS
  - `Library/Audio/Plug-Ins/VST3/GainPilot.vst3`
- Windows
  - `VST3/GainPilot.vst3`

Generate distributable archives:

```sh
cpack --config build/CPackConfig.cmake
```

## CMake Options

- `GAINPILOT_ENABLE_LV2`
- `GAINPILOT_ENABLE_LV2_UI`
- `GAINPILOT_ENABLE_TESTS`
- `GAINPILOT_ENABLE_WX_UI`
- `GAINPILOT_ENABLE_GTK_UI`
- `GAINPILOT_VST3_SDK_PATH`

## Project Layout

- `include/`
  Public headers and shared interfaces
- `src/dsp/`
  Shared DSP implementation
- `src/vst3/`
  Native VST3 wrapper and controller
- `src/lv2/`
  Native LV2 wrapper and UI bridge
- `src/ui/`
  Shared editor code
- `tests/`
  Smoke tests

## CI and Packaging

GitHub Actions builds:

- Linux packages and staged install trees
- Windows packages and staged install trees

Artifacts are produced as archives from the CMake install layout rather than
raw build folders.

## Known Notes

- The Linux `VST3` build currently relies on the host's generic UI instead of a
  custom editor.
- The Steinberg VST3 SDK is not redistributed with this repository.
- `reference/` is ignored and is not part of the public source tree.

## Contributing

See `CONTRIBUTING.md`.

## License

MIT. See `LICENSE`.

## Changelog

Versioned changes are tracked in `CHANGELOG.md`.
