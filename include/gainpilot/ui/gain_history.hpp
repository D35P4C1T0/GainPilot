#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace gainpilot::ui {

// Bounded editor-wall-time history. Parameter notifications only update the
// latest gain; the editor clock supplies samples. Delayed ticks are not backfilled
// because the gain during editor inactivity is unknown.
class GainHistory {
public:
  static constexpr std::size_t capacity = 180;
  static constexpr double duration = 60.0;
  static constexpr double interval = duration / (capacity - 1);
  struct Sample {
    double time;
    float gain;
  };

  bool sample(double now, float gain) noexcept {
    if (!std::isfinite(now) || !std::isfinite(gain))
      return false;
    if (size_ != 0 && now + 1.0e-9 < nextSample_)
      return false;
    samples_[write_] = {now, std::clamp(gain, -15.0f, 15.0f)};
    write_ = (write_ + 1) % capacity;
    size_ = std::min(size_ + 1, capacity);
    // Preserve the cadence's phase across ordinary jitter; on a missed tick,
    // advance straight to the next deadline with bounded work.
    if (size_ == 1)
      nextSample_ = now + interval;
    else
      nextSample_ +=
          std::max(1.0, std::floor((now + 1.0e-9 - nextSample_) / interval) + 1.0) * interval;
    return true;
  }

  std::size_t size() const noexcept { return size_; }
  const Sample& at(std::size_t index) const noexcept {
    return samples_[(write_ + capacity - size_ + index) % capacity];
  }

private:
  std::array<Sample, capacity> samples_{};
  std::size_t write_{0};
  std::size_t size_{0};
  double nextSample_{0.0};
};

} // namespace gainpilot::ui
