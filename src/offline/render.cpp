#include "gainpilot/offline/render.hpp"
#include "gainpilot/dsp/true_peak_limiter.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <ebur128.h>
#include <soxr.h>

namespace gainpilot::offline {
namespace {
double db(double x) { return x > 0 ? 20 * std::log10(x) : -INFINITY; }
void validate(const Audio& a) {
  if (a.channels < 1 || a.channels > 2 || a.sampleRate < 8000 || a.sampleRate > 192000 ||
      a.samples.empty() || a.samples.size() % a.channels != 0)
    throw std::invalid_argument("Expected nonempty mono/stereo audio at 8..192 kHz");
  for (float x : a.samples) if (!std::isfinite(x))
    throw std::invalid_argument("Nonfinite source sample");
}
}
Measurement measure(const Audio& a) {
  validate(a);
  auto destroy = [](ebur128_state* s) { ebur128_destroy(&s); };
  std::unique_ptr<ebur128_state, decltype(destroy)> meter(
    ebur128_init(static_cast<unsigned>(a.channels), a.sampleRate,
                EBUR128_MODE_I | EBUR128_MODE_TRUE_PEAK), destroy);
  if (!meter || ebur128_add_frames_float(meter.get(), a.samples.data(), a.samples.size()/a.channels))
    throw std::runtime_error("libebur128 analysis failed");
  Measurement m;
  if (ebur128_loudness_global(meter.get(), &m.integratedLufs))
    throw std::runtime_error("libebur128 integrated measurement failed");
  // Read integrated loudness before zero-padding the independent true-peak FIR tail.
  std::array<float, 512> peakTail{};
  if (ebur128_add_frames_float(meter.get(), peakTail.data(), 256))
    throw std::runtime_error("libebur128 true-peak flush failed");
  double peak = 0;
  for (unsigned c = 0; c < a.channels; ++c) {
    double p = 0;
    if (ebur128_true_peak(meter.get(), c, &p)) throw std::runtime_error("True peak measurement failed");
    peak = std::max(peak, p);
  }
  m.eburPeakDb = db(peak);
  // Streaming independent reconstruction keeps memory bounded on long files.
  auto quality = soxr_quality_spec(SOXR_VHQ, 0);
  quality.precision = 33;
  auto format = soxr_io_spec(SOXR_FLOAT32_I, SOXR_FLOAT32_I);
  soxr_error_t error = nullptr;
  auto deleter = [](soxr_t s) { soxr_delete(s); };
  std::unique_ptr<soxr, decltype(deleter)> converter(
      soxr_create(a.sampleRate, a.sampleRate * 16., static_cast<unsigned>(a.channels),
                  &error, &format, &quality, nullptr), deleter);
  if (error || !converter) throw std::runtime_error("SoX creation failed");
  std::array<float, 32768> buffer{};
  std::size_t offset = 0;
  const auto frames = a.samples.size()/a.channels;
  peak = 0;
  for (;;) {
    std::size_t used = 0, produced = 0;
    const auto remaining = frames - offset;
    error = soxr_process(converter.get(), remaining ? a.samples.data() + offset*a.channels : nullptr,
                         remaining, &used, buffer.data(), buffer.size()/a.channels, &produced);
    if (error) throw std::runtime_error("SoX reconstruction failed");
    offset += used;
    for (std::size_t i = 0; i < produced*a.channels; ++i) peak = std::max(peak, std::abs(double(buffer[i])));
    if (offset == frames && produced == 0) break;
    if (remaining && !used && !produced) throw std::runtime_error("SoX made no progress");
  }
  m.soxrPeakDb = db(peak);
  return m;
}
Result render(const Audio& source, const Settings& s) {
  validate(source);
  if (!std::isfinite(s.targetLufs) || s.targetLufs < -70 || s.targetLufs > 0 ||
      !std::isfinite(s.ceilingDb) || s.ceilingDb < -24 || s.ceilingDb > 0 ||
      !std::isfinite(s.minimumGainDb) || !std::isfinite(s.maximumGainDb) ||
      s.minimumGainDb < -60 || s.maximumGainDb > 60 || s.minimumGainDb > s.maximumGainDb ||
      !std::isfinite(s.toleranceLu) || s.toleranceLu <= 0 || s.toleranceLu > .1 ||
      s.maximumPasses < 1 || s.maximumPasses > 64)
    throw std::invalid_argument("Invalid targeting settings");
  Result best;
  best.source = measure(source);
  best.audio = source;
  best.output = best.source;
  const bool silence = std::all_of(source.samples.begin(), source.samples.end(), [](float x) { return x == 0; });
  if (silence) { best.status = Status::silent; return best; }
  if (!std::isfinite(best.source.integratedLufs)) { best.status = Status::unmeasurable; return best; }
  best.status = Status::not_converged;
  best.errorLu = INFINITY;
  double low = s.minimumGainDb, high = s.maximumGainDb;
  double gain = std::clamp(s.targetLufs - best.source.integratedLufs, low, high);
  const auto frames = source.samples.size()/source.channels;
  gainpilot::dsp::TruePeakLimiter limiter;
  limiter.prepare(source.sampleRate, source.channels, 0.035375);
  limiter.setCeilingDb(static_cast<float>(s.ceilingDb));
  const auto delay = limiter.latencySamples();
  double previousGain = NAN, previousError = NAN;
  for (unsigned pass = 1; pass <= s.maximumPasses; ++pass) {
    limiter.reset();
    Audio out{source.sampleRate, source.channels, std::vector<float>(source.samples.size())};
    const float linear = static_cast<float>(std::pow(10., gain/20));
    std::array<float, 2> input{}, output{};
    double maximumAttenuation = 0;
    // Feed zeros to drain the delayed last sample, then discard the initial delay.
    for (std::size_t n = 0; n < frames + delay; ++n) {
      for (std::size_t c = 0; c < source.channels; ++c) input[c] = n < frames ? source.samples[n*source.channels+c] : 0;
      limiter.processFrame(input.data(), output.data(), linear);
      if (n >= delay) for (std::size_t c = 0; c < source.channels; ++c) {
        const auto index = (n-delay)*source.channels+c;
        out.samples[index] = output[c];
        const double expected = std::abs(double(source.samples[index])*linear);
        if (expected > 1e-9 && std::abs(output[c]) > 1e-15)
          maximumAttenuation = std::max(maximumAttenuation, db(expected/std::abs(output[c])));
      }
    }
    const auto measurement = measure(out);
    const auto error = measurement.integratedLufs - s.targetLufs;
    const bool peakSafe = measurement.eburPeakDb <= s.ceilingDb && measurement.soxrPeakDb <= s.ceilingDb;
    if (peakSafe && std::isfinite(error) && std::abs(error) < std::abs(best.errorLu)) {
      best.audio = std::move(out);
      best.output = measurement;
      best.gainDb = gain;
      best.errorLu = error;
      best.maximumAttenuationDb = maximumAttenuation;
    }
    best.passes = pass;
    best.latencySamples = delay;
    if (peakSafe && std::abs(error) <= s.toleranceLu) { best.status = Status::achieved; return best; }
    if (!peakSafe) high = gain; // Only upstream correction; never turn up limited output.
    else if (error < 0) low = gain;
    else high = gain;
    if (high-low < .001) { best.status = Status::unattainable; break; }
    double correction = gain - error;
    if (peakSafe && std::isfinite(previousError) && std::abs(gain-previousGain) > 1e-6) {
      const double slope = (error-previousError)/(gain-previousGain);
      if (slope > 1e-5) correction = gain - error/slope;
    }
    previousGain = gain;
    previousError = error;
    double next = std::clamp(correction, low, high);
    if (!std::isfinite(next) || std::abs(next-gain) < .001) next = (low+high)*.5;
    gain = next;
  }
  // A ceiling failure in every pass must never expose an unverified render as success.
  if (!std::isfinite(best.errorLu)) throw std::runtime_error("No measurable ceiling-safe render found");
  return best;
}
const char* statusName(Status s) {
  switch(s) { case Status::achieved: return "achieved"; case Status::unattainable: return "unattainable";
    case Status::not_converged: return "not_converged"; case Status::silent: return "silent"; case Status::unmeasurable: return "unmeasurable"; }
  return "unmeasurable";
}
}
