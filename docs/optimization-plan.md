# GainPilot optimization and improvement plan

Prepared on 2026-09-30. Status: implementation delegated to five isolated improvement branches.

This plan covers all eight findings from the project audit, ordered by expected
implementation difficulty. Each stage should be a separate reviewable change.
The order favors small behavior-preserving fixes before storage changes, DSP
optimization, and a new whole-file rendering workflow.

## Scope and baseline

Preserve public parameter IDs, existing state migration, cross-format behavior,
mono downmix, stereo linking, and reported latency unless a stage explicitly
requires a separately documented change. Preserve existing local DPF edits and
untracked `test-results/` files.

The audit's short ARM64 Release benchmark at 48 kHz / 256-frame blocks measured
4.95% / 8.43% of one core for mono / stereo processing, and 4.20% / 8.29% for the
limiter alone in separate runs. These are indicative timings, not guaranteed
CPU budgets or additive component measurements. Four meter windows/histograms
currently require approximately 6 MiB per processor instance at 48 kHz.

All five available DSP tests passed in a fresh Release build. libebur128 was
unavailable, so independent meter, ceiling, and render-target tests did not run.
Before implementation, repeat the baseline on the current revision and record
compiler, architecture, flags, dependencies, and enabled/skipped tests. Before
meter or limiter changes, establish a baseline with libebur128 and libsoxr.

| Stage | Original audit point | Change | Relative difficulty |
|---|---|---|---|
| 1 | 4 | Avoid DSP rebuilds on buffer-size changes | Low |
| 2 | 6 | Make gain history advance on a fixed clock | Low |
| 3 | 5 | Cache block/control-hop values | Low–medium |
| 4 | 8 | Repeatable performance checks and builds | Medium |
| 5 | 2 | Reduce redundant metering and meter memory | Medium |
| 6 | 3 | Reduce meter-hop and reset spikes | Medium–high |
| 7 | 1 | Optimize limiter FIR processing | High |
| 8 | 7 | Add a whole-file loudness targeting workflow | Highest |

## 1. Avoid DSP rebuilds on buffer-size changes

Files: `src/dpf/GainPilotPlugin.cpp`, `tests/clap_host.cpp`, `tests/au_host.cpp`,
and other host tests where the format supports the relevant lifecycle sequence.

Work:

- Confirm that processing remains independent of maximum block size; the current
  core ignores `maxBlockSize` and has no block-sized scratch storage.
- Separate buffer-size notifications from sample-rate preparation. A buffer-size
  change alone should not recreate meters, filters, delay lines, or the learner.
- Preserve any reset required by a host's explicit deactivate/activate sequence;
  avoid adding another reset solely because its buffer-size notification arrived.
- Keep full preparation for actual sample-rate changes and initial construction.

Validation and completion:

- Exercise supported buffer-size changes and variable process lengths, including
  1, 127, 256, and 1024 frames, for mono and stereo.
- A buffer-size notification alone preserves meter/reset epoch, reference state,
  and delayed audio continuity. Reported latency remains accurate.
- Sample-rate changes still rebuild correctly; existing host and DSP checks pass.

## 2. Make gain history advance on a fixed clock

File: `src/dpf/GainPilotUI.cpp`.

Work:

- Keep parameter callbacks responsible for updating the latest readout values.
- Sample applied gain using the editor's supported timer/idle mechanism, with
  timestamps or a fixed cadence independent of parameter-value changes.
- Make the 180-point history represent an actual 60-second interval; handle
  missed timer ticks explicitly instead of compressing elapsed time.
- Coalesce repaint requests where useful and stop the timer when the editor closes.
- Retain current capture/reset acknowledgement behavior.

Validation and completion:

- Constant gain advances through the graph, and a known gain step appears at the
  correct elapsed position regardless of host notification frequency.
- Check close/reopen, resize, UI inactivity, and delayed callbacks. Document
  whether the graph clock represents editor wall time or playback time.
- Manually verify representative plugin editors and Learn Input / Stop & Lock.

## 3. Cache block and control-hop values

Files: `src/dsp/processor.cpp`, `include/gainpilot/dsp/processor.hpp` as needed.

Work:

- Capture block-constant mode selection, meter references, gain bounds, and
  smoothing coefficients once per `process()` call.
- Cache baseline targets, reference-dependent thresholds, and readiness/activity
  values on the control hops where their inputs actually change.
- Refresh caches after parameter changes, reset, mode changes, and learner updates;
  preserve the current ordering between input control and output metering.
- Keep per-sample smoothing, crossfades, and limiter constraints at sample rate.
- Before this change, save a small repeatable timing/output baseline; stage 4
  turns that measurement into maintained benchmark infrastructure.

Validation and completion:

- Compare old/new outputs for deterministic signals and automation, including
  events that cause processing to be split into smaller blocks.
- Require identical results where arithmetic/order is unchanged; investigate
  differences rather than accepting a new broad tolerance.
- Existing learning, silence recovery, state, peak, and reset regressions pass.
- Record the measured CPU change; do not claim a speedup if it is within noise.

## 4. Make performance checks and builds repeatable

Files: `CMakeLists.txt`, new benchmark source under `tests/` or `tools/`,
`.github/workflows/build.yml`, `.github/workflows/release.yml`,
`cmake/ApplyDPFPatch.cmake`, and validation documentation.

Work:

- Add an opt-in Release benchmark for the processor, limiter, meter, and resets.
  Preallocate measurement storage and keep signal generation outside timed work.
- Cover mono/stereo; 44.1/48/96 kHz; small and large blocks; silence tails, ordinary
  audio, bursts, sustained limiting, mode switches, resets, and multiple instances.
- Report throughput, median/p95/p99/max callback duration, deadline misses,
  preparation/reset time, and storage estimates. Warm up and repeat runs.
- Retain allocation checks; measure tail latency as a separate requirement.
- Enable parallel builds in build/release workflows. Publish benchmark reports
  first; use hard timing gates only on stable, comparable runners.
- Evaluate a build-local DPF source copy for patch application. Preserve recursive
  dependencies, custom `GAINPILOT_DPF_PATH` support, and intentional local edits.
  Do not replace the checkout or discard modifications to make patching easier.
- Make required reference dependencies explicit in the CI jobs intended to run
  independent checks. Record skipped checks in local results.

Validation and completion:

- A clean checkout configures/builds/tests for the supported platforms and formats.
- Reconfiguration and separate concurrent build directories work without modifying
  source DPF files; applied patch contents are reproducible.
- Benchmark commands and environment metadata are documented. Allocation,
  correctness, and timing results remain distinct.

## 5. Reduce redundant metering and meter memory

Files: `src/dsp/processor.cpp`, `include/gainpilot/dsp/processor.hpp`,
`src/dsp/loudness_meter.cpp`, `include/gainpilot/dsp/loudness_meter.hpp`, and
K-weighting interfaces if shared filtering is introduced.

Work, in increasing scope:

- First allocate/process only input and output mono meters for the dedicated
  mono variant. Remove its unused stereo measurement work.
- Preserve both mono and stereo histories in the switchable stereo variant;
  stopping the inactive meter would alter behavior when switching modes.
- Evaluate deriving weighted mono samples from shared left/right K-weighting
  output. Confirm rounding differences against independent references.
- Replace duplicate momentary/short-term energy rings with a shared ring where
  practical. Then evaluate hop-energy storage for a larger memory reduction.
- For aggregated storage, preserve exact window/hop boundaries and reset freshness,
  including rates where rounded window lengths are not exact hop multiples.

Validation and completion:

- Record per-instance memory at all benchmark rates and before/after CPU timings.
- Reference agreement, silence/gating behavior, long-session stability, and learned
  references remain within existing justified tolerances.
- Anti-phase, left-only/right-only, mono/stereo switches, crossfades, and fresh
  capture after reset pass. No audio-thread heap operations are introduced.
- Land mono specialization separately from more invasive shared storage/filtering.

## 6. Reduce meter-hop and reset spikes

Files: loudness meter implementation/header, processor reset paths, and realtime
and reference tests.

Work:

- Use stage 4 measurements after stage 5 to identify remaining hop/reset spikes.
- Replace full histogram scans with a hierarchical sum/count structure or another
  bounded query scheme. Preserve exact stored energies/counts and the existing
  quantized gate-bin membership rule.
- Separate histogram reset from rolling-window reset. Evaluate generation tags
  or lazy invalidation so a callback does not clear all allocated storage.
- Ensure unread old samples contribute zero after a full reset; integrated-only
  reset must still wait for a complete fresh measurement block.
- Define generation wraparound and long-session counter behavior explicitly.
  Prefer simpler storage if the measured benefit of lazy reset is negligible.

Validation and completion:

- Compare gated results against the baseline and libebur128, including signals
  concentrated around absolute and relative gate boundaries.
- Repeated reset, rewind, capture, long-session, and allocation checks pass.
- Report reset duration and callback tail latency improvements; average throughput
  alone does not demonstrate completion of this stage.

## 7. Optimize limiter FIR processing

Files: `src/dsp/true_peak_limiter.cpp`, `include/gainpilot/dsp/true_peak_limiter.hpp`,
peak/ceiling tests, and benchmark infrastructure.

Work:

- Profile the current 4,508 taps per channel per sample. The audited Clang build
  already vectorizes tap loops; establish compiler/architecture-specific baselines.
- Prototype processing multiple phases together, alternative coefficient layouts,
  and mono/stereo kernels. Compare these against compiler-generated vectorization.
- Consider portable SIMD or architecture-specific kernels only when their measured
  benefit justifies maintenance; keep a portable fallback.
- Evaluate removing ring-index division and redundant scaling after FIR work.
- Retain all reconstruction families, phase coverage, lookahead support, linked
  channel constraints, immediate required attenuation, and the 0.3 dB reserve.
- Treat shorter filters, fewer phases, approximate math, or detector shortcuts as
  separate algorithm changes. Do not enable global fast-math as a routine fix.

Validation and completion:

- Strict final-output ceiling checks pass under libebur128 and independent 16x
  libsoxr reconstruction, including automation, bursts, high-frequency tones,
  impulses, clipping, gain modulation, and flush tails.
- Measure numerical differences, stereo linking, reported/measured latency,
  allocation behavior, and silence-tail performance on ARM64 and x86-64.
- Establish a minimum worthwhile speedup from repeated baseline runs before
  selecting a kernel. Retain only candidates that meet it without ceiling failures.
- Perform matched-level listening on representative speech, music, and transients.

## 8. Add a whole-file loudness targeting workflow

Files: a new offline analysis/render module or tool, `tests/vst3_render.cpp`,
`tests/render_target.cpp`, processor APIs if needed, and broadcast validation docs.

Work:

- Begin with a standalone render tool using the shared DSP or actual plugin module,
  before designing an editor/host integration. A host's offline processing flag
  alone does not supply the full file or permit another pass.
- Define supported input formats, analysis settings, gain limits, limiter settings,
  latency compensation, tail handling, and exact output-duration behavior.
- Analyze the complete source, select initial gain/controller settings, render
  through final limiting, and independently measure the complete result.
- Where needed, iterate upstream gain/settings with bounded attempts and explicit
  convergence criteria. Re-render from reset each time; never normalize after
  final limiting.
- Detect/report unattainable targets under the chosen ceiling, gain limits, and
  allowed processing. Do not imply that every target/crest-factor combination is
  achievable, or require integrated loudness for silent/unmeasurable input.
- Resolve the unused `setOfflineMode()` API: either give it a narrowly documented
  implemented behavior or remove it from internal callers. Do not make it imply
  whole-file normalization.
- Add real-material fixtures that can be distributed, alongside synthetic tests.
  Include short clips, pauses, clipped music, speech, and percussion with high peaks.

Validation and completion:

- For measurable, achievable fixtures, agree on a whole-file tolerance before
  tuning; evaluate the previously requested +/-0.1 LU target using independent
  final-output measurements and strict configured peak bounds.
- Cases outside the supported/achievable set return a clear result instead of
  silently claiming success. Record final error and limiting activity.
- Verify deterministic output, duration, reported delay compensation, flushing,
  iteration limits, and behavior across mono/stereo and supported sample rates.
- Document measured limits and audition processing intensity. Existing live Auto
  and Speech behavior stays covered by its own regressions.

## Completion record

For each stage, record the revision, files changed, enabled/skipped tests,
before/after measurements, and any behavior differences in the validation notes.
Complete the relevant correctness checks before starting the next invasive DSP
change. Keep timing improvements and whole-file loudness accuracy as separate
outcomes, with no certification claim inferred from a finite passing test set.

## Parallel macro groups

| Macro group | Stages | Branch | Responsibility |
|---|---|---|---|
| Host lifecycle and editor history | 1, 2 | `improvements/host-ui` | Buffer-size notifications and wall-clock gain graph |
| Meter and controller efficiency | 3, 5, 6 | `improvements/meter-controller` | Control caching, mono specialization, meter storage and reset queries |
| Build and measurement infrastructure | 4 | `improvements/build-benchmarks` | Timing reports, isolated DPF patching, parallel CI and required references |
| Limiter FIR efficiency | 7 | `improvements/limiter` | Portable reconstruction kernel optimization and equivalence validation |
| Whole-file loudness targeting | 8 | `improvements/offline-targeting` | Analysis/render iterations, final independent measurement and CLI |

Each group uses a separate worktree and owns its files. The build group owns
CMake wiring, using conditional source registration so each branch remains
buildable before integration. Integrate the build branch first, then host/UI,
meter/controller, limiter, and offline targeting; rerun correctness and timing
checks on the combined result. Timing artifacts do not establish certification.
