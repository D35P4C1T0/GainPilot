#include "gainpilot/dsp/processor.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <numbers>
#include <string>
#include <thread>
#include <vector>
using Clock = std::chrono::steady_clock;
using namespace gainpilot;
using namespace gainpilot::dsp;
namespace {
template<class T> std::size_t meterBytes(const T& component) {
  if constexpr(requires {component.meterStorageBytes();}) return component.meterStorageBytes();
  else if constexpr(requires {component.storageBytes();}) return component.storageBytes();
  else return 0; // Older branches do not expose storage accounting.
}
struct Instance {
  GainPilotProcessor processor;
  TruePeakLimiter limiter;
  LoudnessMeter meter;
};
void run(const std::string& component, const std::string& scenario, double rate,
         std::size_t channels, std::size_t block, std::size_t count, bool quick) {
  const std::size_t callbacks = quick ? 40 : static_cast<std::size_t>(std::ceil(rate * 3.0 / block));
  const std::size_t warmup = static_cast<std::size_t>(std::ceil(rate * .5 / block));
  const std::size_t repeats = quick ? 2 : 3;
  std::vector<double> timings(callbacks * repeats);
  std::vector<std::unique_ptr<Instance>> instances;
  double preparationUs = 0;
  for (std::size_t i = 0; i < count; ++i) {
    auto instance = std::make_unique<Instance>();
    auto start = Clock::now();
    if(component == "processor") instance->processor.prepare(rate, channels, block);
    if(component == "limiter") instance->limiter.prepare(rate, channels);
    if(component == "meter") instance->meter.prepare(rate, channels);
    preparationUs += std::chrono::duration<double, std::micro>(Clock::now()-start).count();
    instances.push_back(std::move(instance));
  }
  // All input generation, output and timing storage allocation is outside callbacks.
  std::vector<std::vector<float>> input(channels, std::vector<float>(block * 64));
  std::vector<float> zeros(block, 0.f);
  std::vector<std::vector<float>> output(channels, std::vector<float>(block));
  std::vector<const float*> ins(channels);
  std::vector<float*> outs(channels);
  for(std::size_t c=0;c<channels;++c) {
    outs[c]=output[c].data();
    for(std::size_t n=0;n<input[c].size();++n) {
      const double phase = 2 * std::numbers::pi * (997 + 137*c) * n / rate;
      const float amplitude = scenario=="limiting" ? 2.f : .2f;
      input[c][n] = amplitude * static_cast<float>(std::sin(phase));
      if(scenario=="bursts" && n % 2048 < 8) input[c][n] = 2.f;
    }
  }
  ParameterState state;
  double checksum = 0;
  auto callback = [&](std::size_t index, bool measured) {
    const bool silent = scenario=="silence-tail" && measured;
    for(std::size_t c=0;c<channels;++c) ins[c]=silent ? zeros.data() : input[c].data()+(index%64)*block;
    float frame[2]{}, result[2]{};
    for(auto& instance:instances) {
      if(scenario=="reset") {
        if(component=="processor") instance->processor.requestMeterReset();
        if(component=="limiter") instance->limiter.reset();
        if(component=="meter") instance->meter.reset();
      }
      if(scenario=="switch" && component=="processor") {
        state.set(ParamId::channelMode, static_cast<float>(index%2));
        instance->processor.setParameters(state);
      }
      if(component=="processor") {
        instance->processor.process({ins.data(),outs.data(),channels,block});
        checksum += output[0].back();
      } else {
        for(std::size_t n=0;n<block;++n) {
          for(std::size_t c=0;c<channels;++c) frame[c]=silent ? 0.f : ins[c][n];
          if(component=="limiter") instance->limiter.processFrame(frame,result,1.f);
          else (void)instance->meter.processFrame(frame);
        }
        checksum += component=="limiter" ? result[0] : instance->meter.integratedLufs();
      }
    }
  };
  for(std::size_t r=0;r<repeats;++r) {
    for(std::size_t n=0;n<warmup;++n) callback(n,false);
    // Warm up with audio on every repeat, then feed zeros for silence tails.
    for(std::size_t n=0;n<callbacks;++n) {
      auto start=Clock::now(); callback(n,true);
      timings[r*callbacks+n]=std::chrono::duration<double,std::micro>(Clock::now()-start).count();
    }
  }
  const std::size_t storageBytes = component=="processor" ? meterBytes(instances[0]->processor)
    : component=="meter" ? meterBytes(instances[0]->meter) : 0;
  double resetUs=0;
  for(auto& instance:instances) {
    auto start=Clock::now();
    if(component=="processor") instance->processor.reset();
    if(component=="limiter") instance->limiter.reset();
    if(component=="meter") instance->meter.reset();
    resetUs+=std::chrono::duration<double,std::micro>(Clock::now()-start).count();
  }
  const double deadline=block/rate*1e6;
  std::size_t misses=0; double sum=0;
  for(double t:timings) {sum+=t;if(t>deadline)++misses;}
  std::sort(timings.begin(),timings.end());
  auto percentile=[&](double p){return timings[static_cast<std::size_t>(p*(timings.size()-1))];};
  std::cout<<component<<','<<scenario<<','<<rate<<','<<channels<<','<<block<<','<<count<<','<<timings.size()<<','
    <<percentile(.5)<<','<<percentile(.95)<<','<<percentile(.99)<<','<<timings.back()<<','<<misses<<','
    <<100*sum/(timings.size()*deadline)<<','<<preparationUs/count<<','<<resetUs/count<<','<<storageBytes<<','<<checksum<<'\n';
}
}
int main(int argc,char** argv) {
  bool quick=false;
  for(int i=1;i<argc;++i) {
    if(std::string(argv[i])=="--quick") quick=true;
    else {std::cerr<<"Usage: gainpilot_benchmark [--quick]\n";return 2;}
  }
  std::cout<<"# compiler="<<GAINPILOT_COMPILER<<" config="<<GAINPILOT_BUILD_TYPE<<" arch="<<GAINPILOT_ARCH
    <<" hardware_threads="<<std::thread::hardware_concurrency()<<" quick="<<quick<<'\n';
  std::cout<<"# Correctness/reference and allocation checks are separate; run ctest and retain configure log for skipped checks.\n";
  std::cout<<"component,scenario,rate,channels,block,instances,callbacks,median_us,p95_us,p99_us,max_us,deadline_misses,cpu_percent,prepare_us_per_instance,reset_us_per_instance,meter_storage_bytes_per_instance,checksum\n";
  for(double rate:{44100.,48000.,96000.}) for(std::size_t channels:{1u,2u})
    for(std::size_t block:{32u,256u,1024u}) for(std::size_t count:{1u,4u})
      for(const auto& component:{"processor","limiter","meter"})
        for(const auto& scenario:{"ordinary","bursts","limiting","silence-tail","reset","switch"}) {
          if(quick && (rate!=48000 || block!=256 || count!=1)) continue;
          run(component,scenario,rate,channels,block,count,quick);
        }
}
