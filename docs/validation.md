# DSP and plugin validation

Implementation based on the September 2026 comparison plan. These are measured
engineering checks, not a standards certification or a listening assessment.

## Design decisions and compatibility

- Input-reference smoothing now uses the actual 100 ms control-hop duration.
  Its nominal descent/ascent time constants are 3/12 seconds, rather than the
  approximately 4/16 hours produced by the previous 48 kHz implementation.
- The limiter uses seven fractional phases plus the original samples for 8x
  peak detection. Each fractional phase has 128 windowed-sinc taps. Detection
  operates on the signal after the rider gain; audio remains at its native
  sample rate. A 0.3 dB internal margin accommodates finite reconstruction,
  phase spacing, and gain modulation. Existing ~35.375 ms plugin latency is
  retained (1,698 samples at 48 kHz).
- A preallocated monotonic ring stores inverse peak bounds. Bounds are retained
  through the detector's FIR support and audio lookahead. Computing gain from
  the current ceiling avoids retaining gains calculated for an old ceiling.
- Integrated metering uses 17,001 bins of width 0.01 LU over -70 to +100 LUFS.
  Each bin stores exact accumulated energy and count; only relative-gate
  membership is quantized. This is not a universal 0.01 LU bound on final
  integrated error: material concentrated at a gate boundary can be sensitive
  to membership changes. Readouts update every 100 ms and take constant time
  to retrieve. Reset waits for a full fresh 400 ms input block.
- Every plugin uses the same internal BS.1770 filter/meter implementation.
  libebur128 is now an optional test oracle, not a production backend. This
  avoids its allocation-heavy reset path and backend-dependent audio behavior.
  Correcting the former generic filter coefficients can change measured levels
  and processed sound compared with older internal-meter builds.
- Existing parameter IDs remain in order. New parameters are appended; state
  version 5 adds reference mode and locked loudness. Versions 1–4 remain readable
  and default to automatic following. Transient capture/measurement state is
  excluded from presets and session state.

## Results on this machine

Clang Release build on macOS; libebur128 1.2.6 for independent comparisons.

| Check | Result |
|---|---|
| Existing DSP/state smoke suite | Pass |
| Learning timing, 44.1/48/96 kHz, 127/1024-frame blocks | Pass |
| Original phase-shifted fs/4 limiter reproduction | Approximately -1.30 dBTP at a -1 dBTP ceiling; previously +0.07 dBTP |
| Independent meter comparison, mono/stereo and gating transitions | Maximum observed difference 0.00000222 LU |
| libebur128 peak check: tones, bursts, impulses, frequencies through 0.49 fs | Maximum observed peak -1.25981 dBTP at a -1 dBTP ceiling |
| C++ heap tracking during processing, reset, mode switches and rewind | Zero allocations/deallocations; baseline recorded 26/20 |
| Simulated one-hour 8 kHz metering | Pass; fixed storage, finite result, no C++ heap operations |
| AddressSanitizer and UndefinedBehaviorSanitizer | DSP/state tests pass |
| macOS VST3/AU/CLAP and standalone builds, mono and stereo | Build successfully |
| pluginval 1.0.4, strictness 10, seed 12345, mono/stereo VST3 | Pass, including editor, automation and state checks |
| Direct AU host: mono/stereo routing, latency and locked state | Pass without installation |
| AU short Reset/Relearn pulse before a render callback | Pass |
| Installed mono/stereo AU, Apple auval | Pass on September 7, 2026 |
| Installed mono/stereo AU/VST3/CLAP bundles | Match build byte-for-byte; signatures verified |
| Native editor preset save/load, across preview restarts | Pass: target, program, gain limits and locked reference restored |
| Dedicated CLAP host, mono/stereo, 44.1/48/96 kHz, blocks 1/127/1024 | Pass: routing/delay, activation notifications, automation, foreign namespace, reset pulse and state |
| CLAP host and loaded plugins under ASan/UBSan | Both pass |
| clap-validator 0.4.1, unfiltered, both variants | 54 pass, 12 fail, 22 skip; limitations detailed below |
| Independent 120-second render-target probes | Constant, stepped and paused signals in Auto; paused signal in Speech pass within 0.5 LU |
| Independent 25-minute render-target probes, -14 LUFS target | Constant -14.0005, stepped -13.8055, paused -14.0871 LUFS-I |

The existing DPF submodule has local AU compatibility changes from before this
implementation. They were preserved; local AU results include those changes.
Pluginval emits existing DPF component/connection-point lifetime warnings.
The initial editor validation found an empty-status-text assertion; it was fixed.
The native preset round trip saved a locked reference, restarted the isolated AU
preview, selected the Speech factory preset, then loaded the saved file. The
editor restored the -23 LUFS target, Auto program, +30/-24 dB gain limits and
locked -26.013895 LUFS reference (displayed as -26.0).

The allocation test intercepts C++ new/delete in the DSP executable, not every
possible operating-system allocation. The production DSP no longer depends on
libebur128; its project-owned storage is allocated in prepare(). File operations
run in the editor, outside the processing callback.

## CLAP follow-up

The [official clap-validator 0.4.1](https://github.com/free-audio/clap-validator/releases/tag/0.4.1)
initially found latency callbacks outside activation. A project-owned DPF patch
now reports latency during activation and requests a restart for subsequent
changes. The dedicated host test also exposed short-read state corruption:
a temporary string terminator at a chunk boundary was mistaken for a terminator
in the stream. Parsing now distinguishes those boundaries and rejects missing
or unframed terminators. These CLAP builds were installed and verified on
September 7, with previous CLAP copies in `/private/tmp/gainpilot-before-clap-fixes-9o3prprq`.
The pre-existing AU patch remains intact; CMake applies both patches reproducibly.

The full external validator is deliberately unfiltered and still exits with
failure. Six cases per variant compare transient values as if persistent:
`param-set-events`, `param-set-no-cookies`, `param-set-wrong-namespace`, and the
three `state-reproducibility-*` cases. Its value comparisons include read-only
meter outputs; the Input Reference changes after processing, while Reset/Relearn
is intentionally absent from state. The
[parameter tests](https://github.com/free-audio/clap-validator/blob/152b9823e992d782c5c1fd33bca0295478b919aa/src/tests/plugin_instance/params.rs)
compare all parameter readouts, including the foreign-namespace test before and
after audio processing. Our dedicated host separately verifies that persistent
values remain unchanged for foreign events, agree between flush/process, and
survive instance recreation and 7-byte state reads/writes. It also checks rejected
truncated/unframed states. This is targeted coverage, not a claim that the full
external validator passes. DPF emits a nonfatal `bufferSize >= 2` assertion for
single-frame activation; the actual single-frame routing/delay checks pass.

## Reported render-target discrepancy

The user reported a 25-minute render measuring -15 LUFS-I with a -14 LUFS target.
This remains unresolved without source/export files, meter identity and plugin
settings. Three synthetic 25-minute stereo renders at 48 kHz, default Auto
settings with a -14 target and -1 dBTP ceiling, did not reproduce a 1 LU deficit.
libebur128 and the internal meter agreed on the values above. These signals do
not reproduce real speech/music or substantial limiting, so they do not exclude
a material-dependent controller issue. No gain offset or controller retuning
was applied from this incomplete reproduction.

## Reproducing the checks

DSP-only build (does not require DPF initialization):

```sh
cmake -S . -B build-validation -DCMAKE_BUILD_TYPE=Release -DGAINPILOT_ENABLE_PLUGINS=OFF
cmake --build build-validation --parallel
ctest --test-dir build-validation --output-on-failure
```

Install libebur128 development files, or point `PKG_CONFIG_PATH` at its pkg-config
metadata, before configuring to include `gainpilot_reference`. The configure
output explicitly reports when the reference test is skipped. Linux CI installs
the reference library; production processing remains identical with or without it.

Synthetic render probes (requires the independent reference library):

```sh
./build-validation/gainpilot_render_target 1500 0 0
./build-validation/gainpilot_render_target 1500 1 0
./build-validation/gainpilot_render_target 1500 2 0
```

Patterns are constant tone, stepped amplitude, and modulated amplitude with pauses.
Arguments are duration in seconds, pattern, and Speech mode (0/1).

Plugin build and host checks:

```sh
cmake -S . -B build-host -DCMAKE_BUILD_TYPE=Release -DGAINPILOT_PLUGINVAL=/absolute/path/to/pluginval
cmake --build build-host --parallel
ctest --test-dir build-host --output-on-failure
```

The optional pluginval CTest checks skip GUI creation for CI; local manual
strictness-10 runs also exercised the editors. macOS CI downloads pinned
pluginval 1.0.4. AU tests register the just-built component inside their own
process and check actual rendered samples; they do not install a component or
accidentally test a previously installed release. macOS sandbox restrictions can
prevent this process-local registration; run these host tests in a normal shell.

## Remaining validation and optional work

- Matched-level listening on real speech, pauses, music transitions and
  transient-heavy material; audition and refine the factory presets.
- Official loudness reference material and wider pathological signal coverage.
  Synthetic passing cases do not establish compliance for every input.
- Run the updated Windows/Linux CI jobs; they were configured, not executed
  remotely in this session.
- Dedicated LV2 host validation and resolution of the external CLAP validator
  limitations above. The new dedicated CLAP host tests run in plugin CI builds.
- Reproduce the reported -15 versus -14 LUFS-I render using the actual audio
  and settings before changing controller behavior.
- Stage 6's optional response controls, dialogue-specific detector gating,
  separate limiter reduction readout, and lower-latency mode remain dependent
  on the listening evaluation described in the plan.
