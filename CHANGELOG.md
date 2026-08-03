# Changelog

All notable changes to this project will be documented in this file.

## Unreleased

- Migrated LV2 and VST3 wrappers to the DISTRHO Plugin Framework.
- Added DPF-native Audio Unit, CLAP, and optional standalone targets.
- Fixed DPF Audio Unit buffer routing for hosts such as REAPER that replace input callback pointers.
- Replaced the Cocoa, wxWidgets, and GTK editors with one resizable DGL/NanoVG editor.
- Preserved the mono/stereo variants, parameter IDs, symbols, and versioned state payload.
- Removed direct dependencies on the Steinberg VST3 SDK, LV2 SDK, GTK, and wxWidgets.
- Assigned new LV2, VST3, and CLAP identities; pre-DPF sessions require migration through the old build.

## [0.2.0] - 2026-07-26

- Added automatable Stereo/Mono processing with state migration for existing sessions.
- Added BS.1770-consistent mono measurement, linked stereo processing, mono downmixing, and click-free mode crossfades.
- Raised the fresh-instance boost limit to 30 dB and exposed when that limit is reached.
- Redesigned the Cocoa, wxWidgets, and GTK editors around a target fader and live applied-gain graph.
- Expanded deterministic loudness, channel-layout, silence, mode-switching, and render-consistency tests.

## [0.1.1] - 2026-03-17

- Reworked the loudness control path to behave more like LUveler/TriLeveler on real program material.
- Simplified the main control surface to `Target Level`, `True Peak`, and `Max Gain`.
- Added a native macOS VST3 editor and shipped a working macOS VST3 build path.
- Hid legacy/internal VST3 parameters from the host-facing parameter list.
- Extended the `True Peak` control range down to `-10 dB`.

## [0.1.0] - 2026-03-12

- Added shared BS.1770 loudness metering, gain computer, and lookahead true-peak limiting.
- Added mono/stereo LV2 plugins with latency reporting and LV2 state save/restore.
- Added mono/stereo VST3 plugin entries in one bundle with a bundled custom editor.
- Added GitHub Actions builds, staged installs, and distributable package generation.
