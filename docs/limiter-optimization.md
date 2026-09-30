# Limiter FIR optimization validation

Stage 7 changes coefficient storage from phase-major to tap-major. Sixteen
independent accumulators expose SIMD across phases without changing each phase's
tap accumulation order. The 15-phase families use one zero lane; the bandlimited
family retains all 16 phases. All 128/12/24-tap supports, coefficients, lookahead,
stereo constraints, immediate attenuation, release, latency, and 0.3 dB reserve
remain in place. No architecture intrinsics or fast-math flags are required.
Coefficient padding adds 656 bytes per limiter instance.

## Reproduction

Build Release with independent dependencies enabled, then run CTest and:

```
gainpilot_limiter_kernel --benchmark
```

The new kernel regression compares final output against the frozen pre-change
scalar limiter in `tests/support/`. Both kernels compile in separate translation
units, matching the production call boundary. It covers mono/stereo at
44.1/48/96 kHz, silence and flush tails, bursts, broadband and near-Nyquist audio,
reset, ceiling automation and rapidly modulated upstream gain. It bounds rounding
differences to 5e-7; this is not a ceiling tolerance. The peak/ceiling tests enforce
the configured ceiling separately. `tests/ceiling.cpp` adds input-trim automation
and amplitude modulation to the independent libebur128 and 16x libsoxr fixtures.

The benchmark pre-generates one second of signal or silence, alternates kernel
order over seven rounds, discards one warmup, and reports six-round medians and
ranges. A 10% repeatable reduction is the minimum worthwhile local improvement.
Preparation and signal creation are outside timing. These timings measure limiter
throughput, not callback deadlines or host-wide performance.

## Local results (2026-09-30)

ARM64 macOS, Apple Clang 17.0.0, C++20, `-O3 -DNDEBUG`, without LTO or fast-math.
Reference dependencies: libebur128 1.2.6 and libsoxr 0.1.3, static Release builds.

| Rate | Channels | Input | Scalar median ms | Optimized median ms | Reduction |
|---|---:|---|---:|---:|---:|
| 44.1 kHz | 1 | signal | 39.446 | 16.422 | 58.4% |
| 44.1 kHz | 1 | silence | 37.572 | 15.525 | 58.7% |
| 44.1 kHz | 2 | signal | 76.994 | 31.486 | 59.1% |
| 44.1 kHz | 2 | silence | 74.435 | 30.689 | 58.8% |
| 48 kHz | 1 | signal | 43.002 | 17.815 | 58.6% |
| 48 kHz | 1 | silence | 40.816 | 16.848 | 58.7% |
| 48 kHz | 2 | signal | 84.191 | 34.282 | 59.3% |
| 48 kHz | 2 | silence | 81.801 | 33.477 | 59.1% |
| 96 kHz | 1 | signal | 86.266 | 35.636 | 58.7% |
| 96 kHz | 1 | silence | 83.297 | 34.096 | 59.1% |
| 96 kHz | 2 | signal | 172.081 | 69.249 | 59.8% |
| 96 kHz | 2 | silence | 171.632 | 68.144 | 60.3% |

At 48 kHz active mono, scalar measured 42.733–43.517 ms and optimized
17.702–17.995 ms. Stereo measured 83.243–84.453 ms and optimized
33.255–34.412 ms. Ranges remained disjoint across all cases.

Maximum final-output numerical difference: 1.78814e-7. All 11 pre-existing Release
CTest cases passed, including realtime allocation, analytic peak/link/latency,
reference meters and target renders. After adding automation fixtures, ceiling,
peak and realtime tests passed again: 66 independent shape/rate ceiling cases
using both libebur128 and 16x libsoxr. The new numerical regression also passed.

x86-64 timings and matched-level listening on real speech/music were not available
in this environment. The portable kernel and regression should be run there
before claiming an architecture-independent speedup. Finite fixtures do not
prove ceiling safety for every possible input.
