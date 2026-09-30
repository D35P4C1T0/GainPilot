#include "gainpilot/offline/render.hpp"
#include <sndfile.h>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>

namespace {
double number(const char* text) {
  std::size_t used = 0;
  double value = std::stod(text, &used);
  if (text[used] != '\0') throw std::invalid_argument("Invalid numeric argument");
  return value;
}
struct OutputGuard {
  const char* path;
  bool owned{false};
  bool complete{false};
  ~OutputGuard() { if (owned && !complete) std::remove(path); }
};
}
int main(int argc, char** argv) {
  if (argc < 3 || argc > 7) {
    std::cerr << "Usage: gainpilot_offline_render input.wav output.wav [target-LUFS=-23] [ceiling-dBTP=-1] [min-gain=-24] [max-gain=24]\n";
    return 1;
  }
  try {
    SF_INFO info{};
    auto closer = [](SNDFILE* f) { sf_close(f); };
    std::unique_ptr<SNDFILE, decltype(closer)> file(sf_open(argv[1], SFM_READ, &info), closer);
    if (!file) throw std::runtime_error(sf_strerror(nullptr));
    if ((info.format & SF_FORMAT_TYPEMASK) != SF_FORMAT_WAV && (info.format & SF_FORMAT_TYPEMASK) != SF_FORMAT_WAVEX)
      throw std::runtime_error("Only WAV input is supported");
    if (info.channels < 1 || info.channels > 2 || info.frames < 1 ||
        static_cast<unsigned long long>(info.frames) > std::numeric_limits<std::size_t>::max()/info.channels)
      throw std::runtime_error("Invalid source dimensions");
    gainpilot::offline::Audio source{static_cast<unsigned long>(info.samplerate),
      static_cast<std::size_t>(info.channels), std::vector<float>(static_cast<std::size_t>(info.frames)*info.channels)};
    if (sf_readf_float(file.get(), source.samples.data(), info.frames) != info.frames)
      throw std::runtime_error("Could not read complete source");
    file.reset();
    gainpilot::offline::Settings settings;
    if (argc > 3) settings.targetLufs = number(argv[3]);
    if (argc > 4) settings.ceilingDb = number(argv[4]);
    if (argc > 5) settings.minimumGainDb = number(argv[5]);
    if (argc > 6) settings.maximumGainDb = number(argv[6]);
    if (std::filesystem::exists(argv[2])) throw std::runtime_error("Output exists; refusing overwrite");
    auto result = gainpilot::offline::render(source, settings);
    std::cout << "status=" << gainpilot::offline::statusName(result.status)
      << " source_LUFS=" << result.source.integratedLufs
      << " output_LUFS=" << result.output.integratedLufs << " error_LU=" << result.errorLu
      << " ebur128_dBTP=" << result.output.eburPeakDb << " soxr16_dBTP=" << result.output.soxrPeakDb
      << " gain_dB=" << result.gainDb << " passes=" << result.passes
      << " latency_trimmed=" << result.latencySamples
      << " max_attenuation_dB=" << result.maximumAttenuationDb << '\n';
    // Nonconverged results are diagnostics only; no deliverable is written.
    if (result.status != gainpilot::offline::Status::achieved && result.status != gainpilot::offline::Status::silent)
      return 2;
    // Exclusive creation protects the source and avoids a check/create overwrite race.
    OutputGuard guard{argv[2]};
    std::unique_ptr<FILE, decltype(&std::fclose)> output(std::fopen(argv[2], "wbx"), &std::fclose);
    if (!output) throw std::runtime_error("Could not exclusively create output");
    guard.owned = true;
    SF_INFO outInfo = info;
    outInfo.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
    std::unique_ptr<SNDFILE, decltype(closer)> sink(sf_open_fd(fileno(output.get()), SFM_WRITE, &outInfo, SF_FALSE), closer);
    if (!sink) throw std::runtime_error("Could not create float WAV");
    if (sf_writef_float(sink.get(), result.audio.samples.data(), info.frames) != info.frames)
      throw std::runtime_error("Incomplete WAV write");
    if (sf_close(sink.release())) throw std::runtime_error("WAV close failed");
    if (std::fclose(output.release())) throw std::runtime_error("Output close failed");
    guard.complete = true;
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
