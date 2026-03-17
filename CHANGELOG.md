# Changelog

All notable changes to this project will be documented in this file.

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
