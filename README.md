# GainPilot

GainPilot is an adaptive loudness auto-leveler built around BS.1770 / EBU-R128
workflows. A shared C++ DSP core is exported through the
[DISTRHO Plugin Framework](https://github.com/DISTRHO/DPF), giving every format
the same processing, parameters, state model, and cross-platform DGL editor.

## Status

This repository builds two plugin variants:

- `GainPilot`: stereo input and output with an automatable stereo/mono mode
- `GainPilotMono`: dedicated mono input and output

DPF exports both variants as:

- VST3 on Windows, Linux, and macOS
- Audio Unit on macOS
- CLAP on Windows, Linux, and macOS
- LV2 on Linux by default
- an optional standalone application for development

## Features

- Target-based loudness auto-leveling with learned input loudness
- BS.1770 / EBU-R128 integrated, short-term, and momentary metering
- Linked stereo processing and a defined `0.5 * (L + R)` mono downmix
- True-peak ceiling and lookahead limiting
- Automatic and speech program modes
- Input trim, maximum gain, and advanced correction controls
- Versioned cross-format parameter state
- One resizable DGL/NanoVG editor across all plugin formats and platforms
- Live applied-gain history and loudness readouts

<!-- gainpilot-ui-snapshot:start -->

## Plugin UI

![GainPilot plugin UI](docs/assets/gainpilot-ui.png)

<!-- gainpilot-ui-snapshot:end -->

## Build Requirements

Core requirements:

- CMake 3.25 or newer
- A C++20 compiler
- Git submodules initialized recursively

Linux UI builds also require OpenGL and X11 development packages. On Debian or
Ubuntu:

```sh
sudo apt-get install build-essential cmake pkg-config \
  libgl1-mesa-dev libx11-dev libxcursor-dev libxext-dev libxrandr-dev
```

`libebur128` is used when available; GainPilot retains its internal meter when
it is not installed. DPF and its nested Pugl dependency are pinned as Git
submodules. The Steinberg VST3 SDK, wxWidgets, GTK, and native LV2 SDK are no
longer direct project dependencies.

## Quick Start

Clone with submodules:

```sh
git clone --recurse-submodules <gainpilot-repository-url>
cd GainPilot
```

For an existing checkout:

```sh
git submodule update --init --recursive
```

Configure, build, and test:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Generated plugins are placed in `build/bin`. Install into a staging prefix and
create distributable archives with:

```sh
cmake --install build --prefix "$HOME/.local"
cpack --config build/CPackConfig.cmake
```

Typical install locations under the selected prefix are:

- Linux: `lib/vst3`, `lib/lv2`, and `lib/clap`
- macOS: `Library/Audio/Plug-Ins/VST3`, `Library/Audio/Plug-Ins/Components`, and `Library/Audio/Plug-Ins/CLAP`
- Windows: `VST3` and `CLAP`

## CMake Options

- `GAINPILOT_ENABLE_VST3` — build VST3 bundles; default `ON`
- `GAINPILOT_ENABLE_LV2` — build LV2 bundles; default `ON` on Linux
- `GAINPILOT_ENABLE_CLAP` — build CLAP plugins; default `ON`
- `GAINPILOT_ENABLE_AU` — build Audio Unit components; default `ON` on macOS
- `GAINPILOT_ENABLE_STANDALONE` — build DPF standalone applications; default `OFF`
- `GAINPILOT_ENABLE_TESTS` — build DSP/state smoke tests; default `ON`
- `GAINPILOT_DPF_PATH` — use another DPF checkout instead of `dpf/`

## Project Layout

- `dpf/` — pinned DISTRHO Plugin Framework submodule
- `include/gainpilot/` — shared parameter, state, and DSP interfaces
- `src/dsp/` — format-independent loudness and limiter implementation
- `src/dpf/` — DPF plugin adapter, DGL editor, and mono/stereo metadata
- `tests/` — deterministic DSP and state smoke tests

## Compatibility Note

The GainPilot parameter IDs, symbols, and versioned state payload are retained.
DPF requires a different LV2 port topology and generates new VST3 components,
so the DPF builds intentionally use new LV2, VST3, and CLAP identities. Projects
saved with a pre-DPF build will not substitute the new plugin automatically;
render the old instance or copy its settings before replacing it. The new DPF
formats share the same GainPilot state payload with each other.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

GainPilot is MIT licensed. See [LICENSE](LICENSE). DPF is distributed under its
own ISC license in the `dpf/` submodule.

## Changelog

Versioned changes are tracked in [CHANGELOG.md](CHANGELOG.md).
