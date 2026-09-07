#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <vector>
#include "gainpilot/dsp/processor.hpp"

namespace {
thread_local bool tracking = false;
thread_local std::size_t allocations = 0, deallocations = 0;
void* allocate(std::size_t size) {
  if (tracking) ++allocations;
  if (void* p = std::malloc(std::max<std::size_t>(size, 1))) return p;
  throw std::bad_alloc();
}
void release(void* p) noexcept {
  if (tracking && p) ++deallocations;
  std::free(p);
}
}
void* operator new(std::size_t n) { return allocate(n); }
void* operator new[](std::size_t n) { return allocate(n); }
void operator delete(void* p) noexcept { release(p); }
void operator delete[](void* p) noexcept { release(p); }
void operator delete(void* p, std::size_t) noexcept { release(p); }
void operator delete[](void* p, std::size_t) noexcept { release(p); }

int main() {
  constexpr std::size_t block = 256;
  gainpilot::dsp::GainPilotProcessor processor;
  processor.prepare(48000, 2, block);
  gainpilot::ParameterState state;
  std::vector<float> left(block), right(block), outLeft(block), outRight(block);
  const float* inputs[]{left.data(), right.data()};
  float* outputs[]{outLeft.data(), outRight.data()};
  tracking = true;
  for (std::size_t b = 0; b < 4000; ++b) {
    for (std::size_t n = 0; n < block; ++n) {
      left[n] = .15f * std::sin(static_cast<float>(b * block + n) * .13f);
      right[n] = -.3f * left[n];
    }
    state.set(gainpilot::ParamId::meterReset, b % 400 == 399 ? 1 : 0);
    state.set(gainpilot::ParamId::channelMode, (b / 500) % 2);
    processor.setParameters(state);
    processor.process({inputs, outputs, 2, block});
    if (b % 900 == 899) processor.reset(); // Transport rewind path.
    (void)processor.currentInputIntegratedLufs();
    (void)processor.currentOutputIntegratedLufs();
  }
  tracking = false;
  if (allocations || deallocations) {
    std::cerr << "Audio-thread allocations=" << allocations << " frees=" << deallocations << '\n';
    return 1;
  }
  // One hour at 8 kHz exercises duration-independent histogram storage and
  // lookup, without spending that hour in the more expensive limiter.
  gainpilot::dsp::LoudnessMeter meter;
  meter.prepare(8000, 1);
  tracking = true;
  const auto start = std::chrono::steady_clock::now();
  for (std::size_t n = 0; n < 8000 * 3600; ++n) {
    float frame[]{(n % 8 < 4 ? .1f : -.1f)};
    if (meter.processFrame(frame)) (void)meter.integratedLufs();
  }
  tracking = false;
  const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  if (allocations || deallocations || !std::isfinite(meter.integratedLufs())) return 1;
  const float before = meter.integratedLufs();
  meter.resetIntegrated();
  float silence[]{0};
  for (int n = 0; n < 3199; ++n) (void)meter.processFrame(silence);
  if (meter.integratedBlockCount() != 0 || meter.integratedLufs() != -70.f) {
    std::cerr << "Integrated reset reused pre-reset audio\n";
    return 1;
  }
  std::cout << "No C++ heap operations during processing, reset, or one-hour meter run; "
            << "one-hour meter=" << before << " LUFS, wall time=" << elapsed << " s\n";
}
