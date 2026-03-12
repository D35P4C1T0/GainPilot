# GainPilot

GainPilot is a modern native clone of LUveler focused on faithful LUFS-based
auto-leveling with a shared C++ DSP core and native VST3/LV2 wrappers.

Current version: `0.1.0`

Current repository status:

- Shared DSP core scaffold is in place.
- Mono and stereo LV2 audio plugins build locally on Linux.
- LV2 includes a native GTK3 UI with the input LUFS meter and grouped controls.
- Mono and stereo VST3 plugin entries build from one bundle when the Steinberg
  SDK is present at `external/vst3sdk`.
- VST3 includes a bundled custom editor with the input LUFS meter and grouped
  control layout.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

To install the built plugins into a custom prefix:

```sh
cmake --install build --prefix "$HOME/.local"
```

On Linux this stages:

- `lib/lv2/*.lv2`
- `lib/vst3/GainPilot.vst3`

On Windows this stages:

- `VST3/GainPilot.vst3`

To generate distributable packages from the install rules:

```sh
cpack --config build/CPackConfig.cmake
```

## Release Notes

See [CHANGELOG.md](/home/matteo/Documents/prog/lv2/lufs_leveler/CHANGELOG.md) for versioned release notes.

## VST3 SDK

The project expects the Steinberg VST3 SDK at `external/vst3sdk`.
The CI workflow clones it with submodules on demand. Local builds can do the same:

```sh
git clone --depth 1 --recurse-submodules https://github.com/steinbergmedia/vst3sdk external/vst3sdk
```

## Scope

- Mono and stereo plugin variants
- BS.1770 / EBU loudness metering
- Stereo-linked gain computer
- Lookahead true-peak limiting
- Cross-format parameter/state model
