# Improvement workstream integration

Validated on 2026-09-30, ARM64 macOS, AppleClang 17.0.0. Baseline: `fdd15f8`.
All eight plan stages were assigned to five agents in separate branches/worktrees.
Their commits were combined on `improvements/integration`; main and its existing
DPF edits/untracked files were preserved.

| Group | Plan stages | Branch | Implementation commits |
|---|---|---|---|
| Host/UI | 1, 2 | `improvements/host-ui` | `d7b12fe`, `2f1c8d0` |
| Meter/controller | 3, 5, 6 | `improvements/meter-controller` | `68454d2`, `dfeb4d7`, `be05826`, `baabaf8` |
| Build/benchmarks | 4 | `improvements/build-benchmarks` | `711fdac` |
| Limiter | 7 | `improvements/limiter` | `4459151` |
| Whole-file targeting | 8 | `improvements/offline-targeting` | `d66321f`, `16cbf05` |

The group branches retain focused changes. Build infrastructure supplies the
conditional test/tool registrations; use the combined branch for all targets.
Integration also wires the included human speech fixture into the offline CTest.

## Combined results

- Release builds succeed for both mono/stereo VST3, CLAP, and Audio Unit bundles,
  including the shared editor, benchmark, and offline renderer.
- All 19 CTests pass: limiter equivalence, meter window/gate/reset equivalence,
  editor history clock, offline fixtures, smoke/learning/state/presets, allocation
  checks, mono/stereo CLAP and AU hosts, independent final-output ceiling/reference,
  and four synthetic live target probes.
- Independent dependencies: libebur128 1.2.6, libsoxr 0.1.3; CLI uses libsndfile 1.2.2.
  Reference libraries were staged in `/private/tmp/GainPilot-reference-deps`.
- Four focused tests pass with AddressSanitizer and UndefinedBehaviorSanitizer:
  limiter kernel, meter windows, gain history, and offline rendering. Project C++
  code used Debug symbols with `-O1 -fsanitize=address,undefined
  -fno-omit-frame-pointer`; external reference libraries were Release builds.
- The integrated CLI renders the included eight-second speech source at -23 LUFS
  with approximately -0.000000402 LU error. Peaks: -8.82013 dBTP (libebur128) and
  -8.81860 dBTP (16x SoX). Existing-output and malformed-number rejection pass;
  source and existing-output hashes remain unchanged.
- The integrated benchmark quick run completes all 36 scenario rows, with populated
  meter-storage accounting. Report: `/private/tmp/GainPilot-integration-benchmark.csv`.
  Quick timings are smoke measurements, not guaranteed callback budgets.
- Diff whitespace checks pass. The original main worktree still has only the
  pre-existing modified DPF/untracked test results and the previously written plan.

Stage-specific evidence:

- [Limiter optimization](limiter-optimization.md): matched ARM64 measurements show
  58.4–60.3% less limiter processing time, with all reconstruction guards retained.
- [Meter/controller optimization](meter-optimization-validation.md): approximately
  55% lower mono meter storage and 10% lower stereo meter storage at 48 kHz;
  bounded histogram queries and lazy reset replace bulk scans/clears.
- [Build/performance infrastructure](performance-validation.md): source-preserving
  DPF patching, concurrent separate configurations, repeatable timing commands.
- [Offline renderer](offline-rendering.md): fixed whole-file gain ahead of shared
  limiting, bounded rerenders, independent final-output measurement, exact duration,
  and explicit convergence outcomes.

## Reproduction

From the integration worktree, with dependencies visible to pkg-config:

```sh
cmake -S . -B build-integration -DCMAKE_BUILD_TYPE=Release \
  -DGAINPILOT_REQUIRE_REFERENCES=ON -DGAINPILOT_ENABLE_BENCHMARKS=ON
cmake --build build-integration --parallel
ctest --test-dir build-integration --output-on-failure
build-integration/gainpilot_benchmark --quick > benchmark.csv
```

AU host tests need process-local component registration outside restrictive
sandboxes. They load the just-built bundles without installation.

## Remaining platform and listening checks

Windows/Linux execution, x86-64 performance measurements, external VST3/CLAP
validator reruns, and manual editor resize/capture/close/reopen checks were not
performed in this integration run. Existing documented CLAP validator limitations
remain relevant. Matched-level listening and a broad real-music corpus remain
necessary before making stronger sound-quality or convergence claims. The offline
workflow is a standalone fixed-gain renderer; live Auto/Speech behavior and host
offline-mode integration remain separate. Finite passing tests imply no certification.
