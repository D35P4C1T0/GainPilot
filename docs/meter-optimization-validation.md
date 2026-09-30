# Meter and controller optimization validation

Implemented stages 3, 5 and 6 of the optimization plan on
`improvements/meter-controller`. Baseline: `fdd15f8`. Measurements below use
Apple Clang 17.0.0, ARM64 macOS, C++20, `-O3 -DNDEBUG`, without LTO or fast-math.
They are local indicative measurements, not host deadline guarantees.

## Changes and behavior

- Resolve selected meters, gain bounds and smoothing coefficients once per block.
  Refresh baseline targets and reference-dependent thresholds on input control
  hops, after learner updates. Check readiness each sample because window length
  need not be an exact hop multiple (48,001 Hz exercises this case).
- Dedicated mono instances allocate and run only their two mono meters. Stereo
  instances retain all four histories, including inactive mode histories. Preparing
  a previously stereo instance as mono releases its unused buffers outside processing.
- Momentary and short-term sums retain the original accumulation order but read
  one shared short-term energy ring. No hop-based approximation of windows is used.
- Group the 17,001 histogram bins in groups of 128. A relative-gate query scans at
  most 128 boundary bins plus 132 group totals. The gate bin calculation, membership,
  exact bin sums/counts and absolute gate are unchanged. Group sums change only
  double-precision addition association; the focused test allows at most one float
  ULP for integrated readouts, while rolling-window readouts must match exactly.
- Full reset ignores stale ring entries until they have been overwritten. Integrated
  reset advances a generation; stale groups are ignored and each touched group
  clears at most 128 bins on its first insertion. A uint64 generation wrap clears
  the fixed 133-entry directory and restarts at 1. The focused test forces this wrap.
  Integrated-only reset still requires a full fresh momentary block.

## Storage

At 48 kHz, the original four meter windows/histograms required 6,310,464 bytes,
plus K-weighting filter buffers. Both plugin layouts previously allocated these.
The optimized measured buffer storage, including filter buffers and the fixed group
directories in all four meter objects, is:

| Rate | Dedicated mono | Switchable stereo |
|---|---:|---:|
| 44.1 kHz | 2,673,824 bytes | 5,335,104 bytes |
| 48 kHz | 2,861,024 bytes | 5,709,504 bytes |
| 96 kHz | 5,165,024 bytes | 10,317,504 bytes |

`LoudnessMeter::storageBytes()` and `GainPilotProcessor::meterStorageBytes()` report
these allocated buffer capacities and group directories, rather than total object
size or all processor/limiter storage. The 48 kHz reductions are approximately
55% for mono and 10% for stereo.

## Timing evidence

A temporary harness generated input outside timed processor calls and wrote a
binary stream of output samples/readouts for comparison. Meter timings used eight
seconds per rate/layout with integrated reset at 2.37 seconds and full reset at
5.13 seconds; processor timings used 1,500 variable blocks (127/256 frames),
parameter/mode/reference automation, integrated reset and full reset. The baseline
and optimized binaries were alternated for three runs. Whole-processor durations
include signal generation and output serialization; they are useful comparisons, not standalone DSP
throughput measurements.

| Measurement at 48 kHz | Baseline | Optimized |
|---|---:|---:|
| Mono meter control-hop mean, run range | 4.28–4.49 us | 0.056–0.062 us |
| Stereo meter control-hop mean, run range | 6.79–7.16 us | 0.078–0.089 us |
| Mono processor median harness duration | 0.372 s | 0.364 s |
| Stereo processor median harness duration | 0.728 s | 0.722 s |

The approximately 2% mono processor difference is indicative; the stereo difference
is within expected noise. The limiter still dominates the complete processor.
Meter hop readings include clock overhead, so the optimized sub-microsecond values
should not be interpreted as precise instruction costs.

A separate 1,000-event stereo-meter reset experiment fed one frame before each
reset and observed readiness/block count afterward. Each event was timed individually
with `steady_clock`; meter methods were compiled in a separate translation unit
without LTO. Baseline p50/p95/p99/max: 12.459/14.583/17.333/21.083 us. Optimized:
0.041/0.042/0.042/0.083 us, close to the clock's measurement floor. An initial
10,000-call batched measurement reported roughly 0.002 us amortized after optimization;
that hot repeated-call value is **not** an individual callback-reset measurement.
These values cover a single meter, not the limiter or complete transport-reset path.

## Correctness checks

- Baseline versus optimized binary dumps matched bitwise for meters at
  44.1/48/48.001/96 kHz, mono/stereo, resets, and processor outputs under variable
  block lengths, channel/program-mode switches and reference automation.
- `tests/meter_windows.cpp` independently retains the original separate-window and
  full-histogram algorithms. It sweeps occupied bins, gate boundaries, silence and
  abrupt transitions, checking every frame's readiness and block freshness, every
  hop's readouts, generation wrap, and stereo-to-mono buffer release.
- Release CTest: all 11 existing tests passed with libebur128 1.2.6 and libsoxr 0.1.3.
  Independent reference worst meter error: 0.000002218 LU, within the unchanged
  0.05 LU threshold. Strict true-peak/reference checks also passed.
- The realtime allocation regression now runs both dedicated mono and stereo,
  including channel-mode switches, capture resets and transport resets. No C++ heap
  operations occurred during processing/reset or the one-hour meter run.

Build/reference dependencies were staged under `/private/tmp/GainPilot-reference-deps`.
Configure a core validation build with that prefix's `lib/pkgconfig` in
`PKG_CONFIG_PATH`, `-DCMAKE_BUILD_TYPE=Release -DGAINPILOT_ENABLE_PLUGINS=OFF`,
then build and run CTest. The maintained benchmark infrastructure supplies the
repeatable processor/callback scenarios beyond the temporary comparison harness.

## Scope retained

Independent mono/stereo K-weighting filters remain. Deriving weighted mono from
weighted left/right changes float rounding and must be independently reviewed;
this change preserves their existing arithmetic. Hop-aggregated windows also remain
out of scope because independently rounded window/hop lengths at arbitrary rates
require extra boundary storage. Sharing the exact sample history already reduces
memory without those algorithm changes. No meter tolerance was broadened, no
fast-math was enabled, and sample-rate gain smoothing/limiter constraints are unchanged.
