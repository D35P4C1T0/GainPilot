#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "public.sdk/source/vst/vstaudioeffect.h"

#include "gainpilot/dsp/processor.hpp"
#include "gainpilot/state.hpp"

namespace gainpilot::vst3 {

template <std::size_t ChannelCount>
class GainPilotPlugin final : public Steinberg::Vst::AudioEffect {
public:
  GainPilotPlugin();

  static Steinberg::FUnknown* createInstance(void*) { return static_cast<Steinberg::Vst::IAudioProcessor*>(new GainPilotPlugin); }

  Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API terminate() SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& newSetup) SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API setBusArrangements(Steinberg::Vst::SpeakerArrangement* inputs,
                                                   Steinberg::int32 numIns,
                                                   Steinberg::Vst::SpeakerArrangement* outputs,
                                                   Steinberg::int32 numOuts) SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32 symbolicSampleSize) SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) SMTG_OVERRIDE;
  Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) SMTG_OVERRIDE;
  Steinberg::uint32 PLUGIN_API getLatencySamples() SMTG_OVERRIDE;

private:
  void applyInputParameterChanges(Steinberg::Vst::IParameterChanges* changes);
  void pushMeterOutput(Steinberg::Vst::IParameterChanges* changes);
  void ensureScratchCapacity(std::size_t samples);
  [[nodiscard]] bool resetForTransportDiscontinuity(const Steinberg::Vst::ProcessData& data);
  void applyNormalizedParameter(Steinberg::Vst::ParamID tag, Steinberg::Vst::ParamValue value);

  template <typename SampleType>
  void processDoublePrecision(SampleType** input, SampleType** output, Steinberg::int32 numSamples);

  gainpilot::ParameterState parameterState_{};
  gainpilot::dsp::GainPilotProcessor processor_{};
  Steinberg::uint32 latencySamples_{0};
  Steinberg::Vst::ParamValue lastMeterNormalized_{0.0};
  std::optional<Steinberg::int64> lastProjectTimeSamples_{};
  std::array<std::vector<float>, ChannelCount> tempInputs_{};
  std::array<std::vector<float>, ChannelCount> tempOutputs_{};
  std::array<const float*, ChannelCount> tempInputPtrs_{};
  std::array<float*, ChannelCount> tempOutputPtrs_{};
};

extern template class GainPilotPlugin<1>;
extern template class GainPilotPlugin<2>;

}  // namespace gainpilot::vst3
