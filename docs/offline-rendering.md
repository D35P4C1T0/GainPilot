# Whole-file rendering

`gainpilot_offline_render` is a separate fixed-gain normalization tool using the
shared GainPilot true-peak limiter. Live Auto/Speech behavior is unchanged. It
analyzes the entire source with libebur128, chooses upstream gain, renders from a
reset limiter, independently measures the complete result, and adjusts upstream
gain with a bounded bracket/secant search. No gain is applied after limiting.

Build with Release, libebur128, libsoxr, and libsndfile visible to pkg-config:

```sh
cmake -S . -B build-offline -DCMAKE_BUILD_TYPE=Release -DGAINPILOT_ENABLE_PLUGINS=OFF
cmake --build build-offline --parallel
build-offline/gainpilot_offline_render input.wav output.wav -23 -1 -24 24
ctest --test-dir build-offline -R offline --output-on-failure
```

The last four arguments are target LUFS, ceiling dBTP, minimum upstream gain dB,
and maximum upstream gain dB. Defaults are -23, -1, -24, +24. The default tolerance
is +/-0.1 LU and maximum passes is 16. The C++ API can lower tolerance or change
pass count (1..64); supported target range is -70..0 LUFS, ceiling -24..0 dBTP,
and gain bounds -60..+60 dB.

Input is finite mono/stereo WAV PCM or float at 8..192 kHz, with its original
channel layout and sample rate retained. Output is 32-bit float WAV. No resampling,
channel downmixing, adaptive control, dither, metadata copying or multichannel
layout handling is performed. Source and output may never be the same existing
file: output uses exclusive creation and refuses overwrite. Failed writes remove
only the newly created owned output. CLI exit codes: 0 delivered output, 2 no
converged deliverable, 1 argument/I/O/measurement failure.

Limiter lookahead is explicitly 35.375 ms, matching the live processor. Its
initial sample delay is trimmed. Zero input drains every delayed source sample;
exactly the original frame count is written, including original pauses. The
limiter delays and scales samples without a synthesis filter, so it has no
extra audible tail requiring a longer output. Integrated loudness is measured
on that exact duration. libebur128 true-peak analysis receives 256 extra zero
frames after the integrated result is read to flush reconstruction; streaming
16x SoX VHQ (33-bit precision) is fully drained independently. Both complete
reconstruction peaks must be at or below the configured ceiling with no added
acceptance tolerance. Output floating-point samples are the measured samples.

Result statuses:

- `achieved`: measured error within tolerance and both independent peaks safe.
- `unattainable`: search interval exhausted within the permitted fixed-gain/limiter
  configuration. This is a constraint-limited result of this bounded search,
  rather than proof that other processing could not hit the target.
- `not_converged`: pass budget exhausted; best safe measured result is diagnostic.
- `silent`: exactly zero source; unchanged silence may be delivered without LUFS.
- `unmeasurable`: nonzero source has no finite gated integrated measurement (for
  example sub-400 ms material); CLI does not deliver a normalized result.

No nonconverged deliverable is written. The C++ API returns the best safe render
and its error for diagnostics. Limiting intensity is reported as the maximum
per-sample attenuation relative to upstream gain. Impossible high targets can
flatten transients considerably before being rejected. Listen to any strongly
limited successful result before using it in production; finite passing tests
are not certification.

This version deliberately keeps source, best output and current pass in memory:
roughly three interleaved float buffers, plus limiter/reference state. For stereo
48 kHz that is about 69 MiB per minute; very large files require substantial
memory. Independent SoX reconstruction streams through a fixed buffer rather
than allocating a 16x full-file copy. It is offline code, not audio-thread safe.

## Validation recorded 2026-09-30

Apple ARM64, Clang Release `-O3`; libebur128 1.2.6, libsoxr 0.1.3, libsndfile 1.2.2.
24 fixtures cover mono/stereo and 44.1/48/96 kHz, tone, pauses with speech-shaped
modulation, clipped multitone, and high-crest percussion. All meet +/-0.1 LU and
strict -1 dBTP under both references. Percussion at 44.1/48 kHz with 0.01 LU
criteria exercises three reset/rerender passes; final errors -0.002785 and
+0.0000434 LU. Tests cover deterministic output, uncompromised delay alignment,
endpoint transients in a half-second clip, bounded iteration failure, gain- and
ceiling-limited targets, silence, sub-window clips, and invalid samples.

A redistributable eight-second human speech recording is included with source
attribution in `tests/fixtures/offline/README.md`. At -23 LUFS/-1 dBTP it measured
-0.000000402 LU error, -8.82013 dBTP (libebur128), -8.81860 dBTP (SoX16). Synthetic
music/percussion tests remain convergence probes rather than an arbitrary music
corpus. Broad real-music corpus testing and listening evaluation remain future
validation; host offline-mode integration is not supplied by this standalone tool.
