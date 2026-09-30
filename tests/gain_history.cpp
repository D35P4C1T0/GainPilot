#include "gainpilot/ui/gain_history.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

static void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

int main() try {
  gainpilot::ui::GainHistory history;
  // Frequent unchanged notifications cannot stop or accelerate history.
  for (int tick = 0; tick <= 6000; ++tick)
    history.sample(tick / 100.0, tick < 3000 ? 0.0f : 6.0f);
  require(history.size() == history.capacity, "Constant gain did not fill history");
  require(history.at(0).time < 0.02 && history.at(179).time > 59.99,
          "180 samples do not span 60 seconds");
  std::size_t step = 0;
  while (step < history.size() && history.at(step).gain == 0.0f) ++step;
  require(step < history.size() && history.at(step).time >= 30.0 &&
              history.at(step).time < 30.0 + history.interval + .01,
          "Gain step is not positioned by elapsed time");
  require(history.sample(120.0, -3.0f), "Delayed idle did not resume");
  require(history.at(179).time == 120.0 && history.at(178).time <= 60.0,
          "Missed ticks were backfilled");
  require(!history.sample(120.01, 4.0f), "Notifications accelerated cadence");
  require(history.sample(120.4, 99.0f) && history.at(179).gain == 15.0f,
          "Display gain not clamped");
  gainpilot::ui::GainHistory uptime;
  for (int tick = 0; tick <= 6000; ++tick)
    uptime.sample(1234567.0 + tick / 100.0, 0.0f);
  require(uptime.size() == uptime.capacity &&
              uptime.at(179).time - uptime.at(0).time > 59.99,
          "Long-running editor clock lost a boundary sample");
  gainpilot::ui::GainHistory reopened;
  require(reopened.size() == 0 && reopened.sample(200.0, 0.0f) &&
              reopened.size() == 1, "New editor retained old history");
  std::cout << "Gain history wall clock, steps, gaps and reopen passed\n";
  return 0;
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n';
  return 1;
}
