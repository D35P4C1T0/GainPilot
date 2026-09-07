# GainPilot implementation plan

Prepared after the comparison with LUmacOSveler and Livellatore on 2026-09-06.
Implementation is now present in the working tree. See [validation results](validation.md)
for commands, measurements, design decisions, and limits.

## Implementation status

- Stages 1–3: implemented and regression-tested. Production metering is now shared across platforms; libebur128 is retained as an independent test oracle.
- Stage 4: local DSP/sanitizer, VST3 pluginval, direct AU host, installed-build auval, and dedicated CLAP host checks pass. CLAP latency/short-read state fixes are installed; the full external CLAP validator retains documented transient-parameter failures. CI is updated; remote Windows/Linux runs and listening evaluation remain.
- Stage 5: learn-and-lock, state migration, and factory/user presets implemented. Factory presets are starting settings pending listening evaluation.
- Render follow-up: reported 25-minute -15 LUFS-I export at -14 target is not reproduced by three independent synthetic 25-minute probes; actual files/settings are needed.
- Stage 6: optional controls and lower latency remain deferred until listening identifies a concrete need.
- Mono/stereo AU, VST3, and CLAP builds installed in user plugin folders on September 7, 2026, at user request. Previous copies backed up to `/private/tmp/gainpilot-preinstall-1d3ixdp8`. Nothing published; unrelated DPF and test-results changes preserved.

The original staged plan follows for traceability.

## Starting point

- GainPilot reviewed at `19c3d54`.
- LUmacOSveler reviewed at `307142419767e7ab10325973e0ad72f86ec1ba67`.
- Livellatore reviewed at `bb58d65be40d6cd49d3aad9e76383bd4832a8ae4`.
- GainPilot's existing smoke suite passed when compiled directly with Clang and the internal meter backend.
- Competitor plugins were inspected from source, without building or listening tests.
- Existing local changes in the DPF submodule and untracked `test-results/` were present before this work. Preserve them.

At the start of implementation, inspect the current checkout and applicable repository instructions. Reconfirm that each finding still applies before editing. Work in small, independently reviewable changes; complete each stage's validation before expanding scope.

## 1. Correct automatic input-learning timing

Priority: first implementation change.

Finding: `GainPilotProcessor::prepare()` calculates the input learner's 3-second and 12-second smoothing coefficients for sample-rate updates. `process()` applies them only on the approximately 100 ms control hop. At 48 kHz, the nominal effective time constants are approximately 4 and 16 hours.

Work:

- Calculate learning coefficients using the actual control-hop duration, including sample rounding.
- Keep the other per-sample smoothers on their existing timebase.
- Add a focused regression that distinguishes seconds-scale learning from the current hours-scale behavior. Account for integrated-meter history separately from learner smoothing.
- Check startup, Reset/Relearn, mono/stereo switching, and representative program-level transitions. Faster learning changes interactions with the baseline and output feedback, so check overshoot and settling as well as final loudness.

Acceptance:

- Learning follows the intended time constants across 44.1, 48, and 96 kHz and multiple host block sizes.
- Existing target-convergence, silence-recovery, and controller anti-windup regressions pass.
- Any audible behavior change is documented with a before/after measurement.

## 2. Make true-peak protection independently verifiable

Priority: correctness before additional controls.

Finding: `TruePeakLimiter` estimates intersample peaks using Catmull–Rom interpolation at eight positions. The existing peak test uses the same interpolation and can miss the same peaks.

Reproduction to preserve as a regression:

- Prepare the limiter at 48 kHz, one channel, current 35.375 ms lookahead, and a -1 dB ceiling.
- Feed two seconds of `1.2 * sin(2*pi*0.25*n + pi/4)` with unity pre-gain.
- After settling, recover the sinusoidal amplitude from adjacent quadrature output samples using `hypot(previous, current)`.
- The reviewed implementation produced approximately +0.07 dB reconstructed peak, exceeding the ceiling by approximately 1.07 dB.

Work:

- Establish an independent peak-measurement oracle using a validated band-limited reconstruction or external reference meter.
- Evaluate a band-limited oversampling detector or oversampled limiting path. Choose based on measured ceiling accuracy, CPU cost, and latency; do not transplant a competitor's limiter without validation.
- Verify detector timing, delayed audio alignment, attack/release behavior, stereo linking, and host latency reporting together.
- Test phase-shifted high-frequency tones, impulses, bursts, near-Nyquist signals, and representative audio at supported sample rates.
- Keep the existing latency initially unless a change is needed for correctness. Treat lower latency as a subsequent measured optimization.

Acceptance:

- The reproduction passes an explicitly documented peak tolerance established against the independent oracle.
- Tests measure the final output, including any downsampling stage.
- Reported latency matches measured delay; mono and linked stereo both pass.
- Document the limits of validation without claiming certification.

## 3. Bound audio-thread memory use and processing cost

Priority: reliability for long sessions.

Findings: the internal meter stores an ever-growing integrated history and rescans it; the limiter uses a dynamically managed deque; Reset/Relearn recreates libebur128 integrated state from the processing path.

Work:

- Measure baseline processing time and audio-thread allocations for both meter backends.
- Replace limiter queue storage with a preallocated bounded structure while preserving its sliding-window behavior.
- Choose an integrated-meter strategy with bounded processing cost and memory. Evaluate histogram-based accumulation or another justified approach against an independent reference, documenting any approximation.
- Make Reset/Relearn safe for the audio thread. Audit dependency calls as well as project containers.
- Check whether meter readouts repeatedly perform expensive calculations that can instead be cached at a suitable update interval.
- Run long-duration synthetic sessions, including repeated resets and transport rewinds.

Acceptance:

- No audio-thread heap allocation/deallocation in steady-state processing or supported reset actions, verified with instrumentation.
- Processing time and memory do not grow with program duration.
- Integrated loudness and limiter behavior remain within documented numerical tolerances.

## 4. Strengthen metering and plugin validation

Work:

- Compare the internal K-weighting/meter backend with libebur128 and independent reference fixtures. Include absolute loudness checks; measuring output with the same implementation alone cannot establish accuracy.
- Cover mono, duplicated stereo, single-sided stereo, silence, gating transitions, sample-rate changes, and block-size variation.
- Run the smoke suite in the Windows CI job, which currently builds without executing it.
- Exercise both meter backends explicitly in CI where available.
- Add suitable plugin-host validation for built formats, including latency, parameter automation, state restore, and mono/stereo layouts. Use pluginval and auval where supported.
- Keep measured DSP results separate from listening assessments. Audition matched-level speech, pauses, music transitions, and transient-heavy material after correctness fixes.

Acceptance:

- CI clearly identifies which platform, backend, and plugin checks ran.
- Meter tolerances are backed by independent measurements.
- Any remaining host or signal-specific failures are recorded with reproducible cases.

## 5. Add explicit learning modes and presets

Priority: feature work after the preceding correctness checks pass.

Work:

- Design an explicit automatic-follow versus learn-and-lock workflow, retaining the existing automatic mode as the default.
- Define capture start/stop, insufficient signal, reset, transport changes, and restored-session behavior before implementation.
- Persist a locked input reference as intentional state; keep transient meter readings out of saved state.
- Add a small set of documented factory presets and user preset save/load. Base presets on measured and auditioned behavior.
- Preserve existing parameter IDs and add versioned state migration for new persistent settings.

Acceptance:

- A locked reference stays fixed across playback and session restore until explicitly changed.
- Automatic mode retains validated behavior.
- Old states load with safe defaults, and preset round trips preserve intended settings.

## 6. Consider additional controls only after listening evaluation

Candidates:

- Separate limiter gain-reduction metering from controller gain reduction.
- Advanced response-speed controls if the Automatic/Speech choices prove insufficient.
- Dialogue-specific detector gating, with behavior distinguished from the existing Speech controller tuning.
- A lower-latency mode if measured peak protection and sound quality remain acceptable.

For each candidate, first describe the user problem, proposed behavior, and validation criteria. Avoid expanding the primary interface without evidence from use.

## Handoff for a future coding session

Read the implementation status above and docs/validation.md before continuing. Review the current diff and run the existing validation commands. Prioritize the remaining listening, remote-platform and installed-host checks; do not reimplement completed stages. Evaluate stage 6 only when those checks identify a concrete need. Preserve unrelated local changes. Do not commit, publish, or install plugins unless requested. Report what changed, what was verified, and the next unfinished check.
