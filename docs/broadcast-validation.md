# Broadcast ceiling validation — September 10, 2026

The reproduced true-peak failures are fixed in the shared limiter. The stricter
final-output tests pass. This is not a claim of universal reconstruction-filter
equivalence or Orban certification. The separate whole-file loudness target
requirement is **not yet met for arbitrary material in Auto mode**.

## Cause and change

The output path is input trim/channel handling, leveling gain, lookahead limiter,
then output metering and host buffers. No gain is applied after the limiter.
Host track/master gain, export normalization, resampling and lossy encoding
remain outside the plugin's output boundary.

Two independent problems were reproduced:

- A deterministic 32-sample noise burst at 44.1/48 kHz measured **−8.96781 dBTP**
  with libebur128 despite a −9 dBTP setting and the previous 0.3 dB reserve.
  The original 128-tap, 8-phase reconstruction did not bound peaks reported by
  shorter reconstruction filters. More taps alone do not imply a conservative
  peak estimate for every transient.
- Lowering the ceiling from −1 to −9 while processing a tone left the smoothed
  attack gain above the newly required gain. Post-change measurements reached
  **−1.31165 dBTP** at 48 kHz. Recomputing the requested gain with the new ceiling
  was insufficient while its applied envelope still approached that gain slowly.

The detector now checks 16 phases with 128-, 24- and 12-tap full-band sinc
reconstructions, plus a 128-tap reconstruction with a 0.95-Nyquist cutoff.
The last guard addresses ringing from analyzers whose interpolation transition
band starts below Nyquist. All channels contribute to the same peak bound.
Attack attenuation is immediate when necessary; release remains 100 ms.
The applied envelope is capped by the current required gain on every sample.
The existing lookahead and peak-hold support are retained, with no new latency,
audio resampling stage, parameter IDs, or audio-thread allocation.

A 0.1 dB reserve was evaluated and rejected: it passed libebur128 but failed
independent 16x SoX reconstruction. Even with the extra bandwidth guard, one
burst measured −8.93924 dBTP. The final implementation retains **0.3 dB** of
internal reserve. The worst static-ceiling synthetic result under the independent
SoX reconstruction is approximately **−9.13924 dBTP**. Tones generally reach
approximately −9.3 dBTP. Ceiling assertions allow **no positive tolerance**.

Changing a ceiling cannot retroactively change audio already emitted under the
previous setting. The automation test checks immediate post-change sample bounds
and libebur128 reconstruction; the SoX check retains the entire varying-ceiling
waveform and uses its original −1 dBTP maximum. Cropping a file at the change
would manufacture a new discontinuity and is not equivalent to measuring the
actual complete rendered waveform.

## Automated coverage

`gainpilot_ceiling` processes the final shared-processor output, with +12 dB input
trim, at 44.1/48/96 kHz and block sizes 1/127/1024. Its 60 cases include phase-shifted
fs/4 tones, impulses, hard-clipped tones, near-full-scale noise, near-Nyquist
tones, amplitude steps, short noise bursts of multiple lengths/seeds, and ceiling
automation. It flushes the plugin delay and reconstruction tail, checks finite
samples and stereo linking, and checks both output channels against libebur128.
When libsoxr is available, the same final samples are independently reconstructed
at 16x, VHQ with 33-bit precision. Both Linux CI workflows install that dependency.

The analytic fs/4 and existing independent reference tests now fail on any peak
above their −1 dBTP setting; their former +0.1 dB allowance was removed.
The render-target regression now checks its final true peak too.

All 15 Release CTest cases pass, including the allocation/reset tests and LV2/CLAP
host regressions. The larger detector costs more CPU; fixed memory and latency
are preserved. This work does not include a listening assessment of the harder
attack or a guaranteed worst-case CPU budget for every host.

## Actual VST3 renders

`gainpilot_vst3_render` loads the actual Linux VST3 module, obtains parameter IDs
and normalization from its controller, and processes via its audio processor.
It writes float WAV without post-limiter gain, compensates the reported delay,
flushes the tail, and preserves source duration. Output is checked with
libebur128 1.2.6; the written WAVs were also reconstructed with FFmpeg/SoX.
Original files were only read. Derived WAVs remain in the ignored build directory.

Settings: target −23 LUFS, ceiling −9 dBTP, default Auto mode and gain limits.

| Material | Duration | Plugin LUFS-I | External LUFS-I | External peak dBTP |
|---|---:|---:|---:|---:|
| Local podcast | 138.71 s | −23.4726 | −23.4691 | −9.29283 |
| Local music excerpt | 36.18 s | −23.4832 | −23.4810 | −9.29493 |
| Raw percussion overheads | 198.10 s | −27.9044 | −27.9110 | −9.29215 |
| Music boosted 12 dB and hard-clipped at 0.99 | 36.18 s | −18.5987 | −18.6042 | −9.29215 |

These are **meter-agreement and ceiling passes, not target-convergence passes**.
The external/internal differences are below 0.01 LU, but the requested ±0.1 LU
whole-file target tolerance is not achieved. High-crest-factor percussion loses
level under limiting; short clipped material also exposes startup/adaptation
error. No global loudness offset was introduced to hide those content-dependent
results. A live adaptive leveler cannot undo already-rendered startup audio or
know the duration/content still to come. Exact finite-file integrated targeting
requires a separately validated whole-program strategy, typically another pass;
any such gain correction must still precede final limiting.

Orban was not installed in the inspected environment, so no Orban result is
claimed. Its [official download page](https://www.orban.com/freeorbanloudnessmeter)
provides an additional manual validation route using the generated final WAVs.
The [ITU BS.1770 specification](https://www.itu.int/rec/R-REC-BS.1770) also explains
why finite oversampling can under-read reconstructed peaks. Passing this finite
test set cannot prove a ceiling for every possible signal, resampler, or encoder.

## Reproduce

Install the optional test dependencies `libebur128-dev`, `libsoxr-dev` and
`libsndfile1-dev`, then configure/build normally. No runtime plugin dependency
on these libraries is added.

```sh
cmake -S . -B build-plan -DCMAKE_BUILD_TYPE=Release
cmake --build build-plan --parallel
ctest --test-dir build-plan --output-on-failure

./build-plan/gainpilot_vst3_render \
  build-plan/bin/GainPilot.vst3/Contents/x86_64-linux/GainPilot.so \
  /path/to/input.wav /path/to/new-output.wav -23 -9 256 1
```

The final arguments are target, ceiling, block size, and offline mode (1) versus
real-time processing mode (0). Select `GainPilotMono` for mono input. The tool
refuses to overwrite an existing output. It returns failure for peak violations;
its printed loudness values must also be reviewed against the desired target.

```sh
ffmpeg -hide_banner -nostats -i /path/to/new-output.wav \
  -af 'aresample=768000:resampler=soxr:precision=33:osf=dbl,astats=metadata=0:reset=0' \
  -c:a pcm_f64le -f null -
```

768000 Hz is 16x for a 48 kHz source; use 705600 for 44.1 kHz. Keeping floating
point throughout prevents an implicit integer conversion from contaminating
the independent reconstruction check.
