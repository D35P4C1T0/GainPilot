# Repeatable build and performance validation

Build timing reports separately from correctness and allocation regressions:

```sh
cmake -S . -B build-perf -DCMAKE_BUILD_TYPE=Release \
  -DGAINPILOT_ENABLE_PLUGINS=OFF -DGAINPILOT_ENABLE_BENCHMARKS=ON
cmake --build build-perf --config Release --parallel
ctest --test-dir build-perf -C Release --output-on-failure
build-perf/gainpilot_benchmark --quick > quick.csv
build-perf/gainpilot_benchmark > complete.csv
```

The quick run covers 48 kHz, 256-frame buffers, mono/stereo, and one instance;
it is a smoke report, not a stable timing estimate. The complete run covers
44.1/48/96 kHz, 32/256/1024 frames, mono/stereo and one/four serial instances.
Each component (processor, limiter, meter) runs ordinary audio, bursts,
sustained limiting, audio followed by silence, per-callback reset, and mode
switch scenarios. Mode switching applies to the processor; the component rows
retain ordinary input for comparable baseline timing. Reset callbacks include
reset work plus processing, not isolated reset alone.

The full run warms each repetition with 0.5 seconds of audio and measures three
3-second runs per case. Quick runs use two repetitions of 40 callbacks. Input
signals, instance storage, output buffers, and timing vectors are allocated
before measured callbacks. Percentiles aggregate all repeated callbacks. Output
includes median/p95/p99/max, aggregate deadline misses, percent of a serial
core's callback budget, initial preparation time, final isolated reset time,
meter allocation bytes when that API exists, and a checksum preventing dead
output elimination. Zero meter bytes means unavailable accounting on an older
branch or a component without meter storage; it does not claim zero total
memory. Meter storage excludes limiter and processor object overhead.

Retain the report, configure log (including skipped references), exact revision,
OS version, CPU model, compiler flags and host power settings. Compiler,
architecture, configuration, and available thread count are in each report.
Compare matched Release builds on the same machine with the same workloads;
avoid competing processes and use multiple complete reports. No timing pass/fail
gate is imposed on shared CI runners. CPU percentages from different components
are separate measurements and should not be added. Clock reads and harness
bookkeeping are included, so tiny callbacks have some measurement overhead.

`gainpilot_realtime` remains the allocation check. Timing reports do not check
allocation freedom or numerical correctness. Independent tests need
libebur128 and libsoxr through pkg-config. Set
`-DGAINPILOT_REQUIRE_REFERENCES=ON` to fail configuration rather than skip these
checks. Linux build and release jobs require them; local and other platform
jobs print available/skipped checks. The Linux build job uploads a quick timing
artifact without interpreting runner noise as a regression.

## DPF source isolation

When a enabled plugin format needs a GainPilot patch, configuration snapshots
`GAINPILOT_DPF_PATH` into `build/gainpilot-dpf-source`, excludes all `.git`
metadata, and patches only this private copy. Working-tree edits and recursive
source dependencies are retained. The source path may point to a git checkout
or a source archive. Already-applied patches pass reverse validation; conflicting
edits fail configuration without changing the supplied checkout. Reconfiguration
refreshes the snapshot to incorporate source edits. Do not edit the generated
snapshot: edit the source checkout instead. The copy has a private git repository
solely to anchor `git apply`, even when the build directory sits in a worktree.

Separate build directories have separate copies. A lock serializes the copy and
patch phase within one build directory; do not run a build while configuring
that same directory. Concurrent separate build directories are supported.

## Initial validation (2026-09-30)

ARM64 macOS, AppleClang 17, Release: the opt-in benchmark builds and produces
all 36 quick scenario rows. The original five DSP regressions pass without
reference dependencies. Configuration with AU/CLAP patches leaves the source
DPF tree byte-identical. Concurrent independent configurations, repeated configuration, a custom git
checkout, and a clean unpatched source with an intentional local edit all pass.
libebur128 1.2.6 and soxr 0.1.3 were subsequently built locally; configuration
with required references, reconfiguration, and compilation pass. Combined
branch correctness tests are recorded separately at integration time. Runtime timing numbers are artifacts,
not guaranteed host budgets or certification claims.

The offline branch is enabled by `GAINPILOT_ENABLE_OFFLINE=ON` (default) when
its sources and libebur128/soxr are present. Its CLI additionally needs sndfile.
Set the option OFF to omit that workflow. Conditional registrations for
`gain_history`, `meter_windows`, `limiter_kernel`, and `offline` allow the
independent branches to build before integration; the combined branch must
contain and execute all their tests.
