#include "gainpilot/offline/render.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <fstream>
#include <cstdint>
#include <stdexcept>
#include <vector>
using namespace gainpilot::offline;
namespace {
void require(bool b, const char* why) { if (!b) throw std::runtime_error(why); }
Audio fixture(unsigned long rate, std::size_t channels, int kind) {
  Audio a{rate, channels, std::vector<float>(rate*3*channels)};
  constexpr double pi = 3.14159265358979323846;
  for (std::size_t n = 0; n < rate*3; ++n) {
    double t = double(n)/rate;
    double value = .06 * std::sin(2*pi*997*t);
    if (kind == 1) { // pauses and speech-shaped modulation
      value *= std::fmod(t, 1) < .7 ? .2 + .8*std::pow(.5+.5*std::sin(2*pi*3.7*t),2) : 0;
    } else if (kind == 2) { // clipped multitone, limiting required
      value = std::clamp(.8*std::sin(2*pi*997*t)+.4*std::sin(2*pi*1499*t), -.5, .5);
    } else if (kind == 3) { // percussive high-crest pulses
      value = .6*std::exp(-std::fmod(t,.2)*70)*std::sin(2*pi*8000*t);
    }
    for (std::size_t c = 0; c < channels; ++c) a.samples[n*channels+c] = float(value*(c ? .8 : 1));
  }
  return a;
}
}
int main(int argc, char** argv) {
  try {
    for (auto rate : {44100ul, 48000ul, 96000ul}) for (auto channels : {1u,2u}) {
      for (int kind = 0; kind < 4; ++kind) {
        const auto source = fixture(rate,channels,kind);
        Settings s;
        s.targetLufs = kind == 2 ? -10 : (kind == 3 ? -15.8 : -23);
        if (kind == 3) s.toleranceLu = .01;
        auto r = render(source,s);
        std::cout << "rate=" << rate << " channels=" << channels << " fixture=" << kind
          << " status=" << statusName(r.status) << " error=" << r.errorLu
          << " peak=" << r.output.eburPeakDb << " soxr=" << r.output.soxrPeakDb
          << " passes=" << r.passes << '\n';
        require(r.status == Status::achieved,"Fixture failed target");
        require(std::abs(r.errorLu) <= .1,"Target tolerance");
        require(r.output.eburPeakDb <= s.ceilingDb && r.output.soxrPeakDb <= s.ceilingDb,"Ceiling");
        require(r.audio.samples.size() == source.samples.size(),"Duration");
        require(r.passes <= s.maximumPasses,"Bounded passes");
        if (rate == 48000 && channels == 1 && kind == 0) {
          require(render(source,s).audio.samples == r.audio.samples,"Determinism");
          require(r.maximumAttenuationDb < .001,"Unnecessary limiting or latency error");
          for (std::size_t i = 0; i < source.samples.size(); ++i)
            require(std::abs(r.audio.samples[i] - source.samples[i]*std::pow(10.,r.gainDb/20)) < 1e-6,"Latency alignment");
        }
      }
    }
    auto iterative = fixture(44100,1,3);
    Settings limitedBudget;
    limitedBudget.targetLufs = -15.8;
    limitedBudget.toleranceLu = .01;
    limitedBudget.maximumPasses = 1;
    require(render(iterative,limitedBudget).status == Status::not_converged,"Iteration exhaustion distinguished");
    auto shortClip = fixture(48000,1,0);
    shortClip.samples.resize(24000);
    shortClip.samples.front() = .4f;
    shortClip.samples.back() = -.4f;
    require(render(shortClip).status == Status::achieved,"Short clip and endpoint transients");
    auto source = fixture(48000,1,0);
    Settings impossible;
    impossible.targetLufs = -3;
    impossible.maximumGainDb = 0;
    auto r = render(source,impossible);
    require(r.status == Status::unattainable && r.errorLu < -.1,"Gain-limited target reported");
    impossible.maximumGainDb = 24;
    impossible.targetLufs = 0;
    require(render(source,impossible).status == Status::unattainable,"Ceiling-limited target reported");
    source.samples.assign(source.samples.size(),0);
    require(render(source).status == Status::silent,"Silence");
    source.samples.resize(100);
    source.samples[0] = .1;
    require(render(source).status == Status::unmeasurable,"Short input");
    source.samples[0] = INFINITY;
    bool caught = false;
    try { (void)render(source); } catch (const std::invalid_argument&) { caught = true; }
    require(caught,"Invalid sample rejection");
    if (argc == 2) {
      std::ifstream wav(argv[1], std::ios::binary);
      std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(wav)), {});
      require(bytes.size() == 128044, "Speech fixture size");
      Audio speech{8000,1,std::vector<float>(64000)};
      for (std::size_t n = 0; n < speech.samples.size(); ++n) {
        unsigned word = bytes[44+2*n] | (unsigned(bytes[45+2*n]) << 8);
        speech.samples[n] = float(word >= 32768 ? int(word)-65536 : int(word))/32768;
      }
      auto real = render(speech);
      require(real.status == Status::achieved && std::abs(real.errorLu) <= .1, "Real speech target");
      require(real.output.eburPeakDb <= -1 && real.output.soxrPeakDb <= -1, "Real speech ceiling");
      std::cout << "Open Speech Repository error=" << real.errorLu << " peak=" << real.output.eburPeakDb << " soxr=" << real.output.soxrPeakDb << '\n';
    }
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
