#include "plugin.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <iomanip>

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/gui/iplugview.h"
#include "public.sdk/source/vst/vstaudioprocessoralgo.h"

#include "common.hpp"

namespace gainpilot::vst3 {

namespace {

constexpr std::uint32_t kTraceBlockBudget = 32768;

template <std::size_t ChannelCount>
constexpr Steinberg::Vst::SpeakerArrangement speakerArrangementFor() {
  if constexpr (ChannelCount == 1) {
    return Steinberg::Vst::SpeakerArr::kMono;
  }
  return Steinberg::Vst::SpeakerArr::kStereo;
}

template <std::size_t ChannelCount>
constexpr const Steinberg::Vst::TChar* inputBusName() {
  if constexpr (ChannelCount == 1) {
    return STR16("Mono In");
  }
  return STR16("Stereo In");
}

template <std::size_t ChannelCount>
constexpr const Steinberg::Vst::TChar* outputBusName() {
  if constexpr (ChannelCount == 1) {
    return STR16("Mono Out");
  }
  return STR16("Stereo Out");
}

std::vector<std::byte> readStateBytes(Steinberg::IBStream* stream) {
  std::vector<std::byte> bytes(sizeof(std::uint32_t) * 2 + 4 + sizeof(float) * kStateParamIds.size());
  Steinberg::int32 bytesRead = 0;
  if (stream->read(bytes.data(), static_cast<Steinberg::int32>(bytes.size()), &bytesRead) != Steinberg::kResultTrue) {
    return {};
  }
  if (bytesRead != static_cast<Steinberg::int32>(bytes.size())) {
    return {};
  }
  return bytes;
}

}  // namespace

template <std::size_t ChannelCount>
GainPilotPlugin<ChannelCount>::GainPilotPlugin() {
  setControllerClass(kControllerCid);
  traceLog_.open("/tmp/gainpilot_vst3_trace.log", std::ios::out | std::ios::app);
}

template <std::size_t ChannelCount>
Steinberg::tresult PLUGIN_API GainPilotPlugin<ChannelCount>::initialize(Steinberg::FUnknown* context) {
  const auto result = AudioEffect::initialize(context);
  if (result != Steinberg::kResultOk) {
    return result;
  }

  this->addAudioInput(inputBusName<ChannelCount>(), speakerArrangementFor<ChannelCount>());
  this->addAudioOutput(outputBusName<ChannelCount>(), speakerArrangementFor<ChannelCount>());
  traceMessage("initialize");

  return Steinberg::kResultOk;
}

template <std::size_t ChannelCount>
Steinberg::tresult PLUGIN_API GainPilotPlugin<ChannelCount>::terminate() {
  return AudioEffect::terminate();
}

template <std::size_t ChannelCount>
Steinberg::tresult PLUGIN_API GainPilotPlugin<ChannelCount>::setActive(Steinberg::TBool state) {
  if (state) {
    processor_.reset();
    lastProjectTimeSamples_.reset();
    traceBlocksRemaining_ = kTraceBlockBudget;
    traceMessage("setActive(true): processor reset");
  }
  return Steinberg::kResultOk;
}

template <std::size_t ChannelCount>
Steinberg::tresult PLUGIN_API GainPilotPlugin<ChannelCount>::setupProcessing(Steinberg::Vst::ProcessSetup& newSetup) {
  const auto result = AudioEffect::setupProcessing(newSetup);
  if (result != Steinberg::kResultOk) {
    return result;
  }

  processor_.prepare(newSetup.sampleRate, ChannelCount, static_cast<std::size_t>(newSetup.maxSamplesPerBlock));
  processor_.setParameters(parameterState_);
  latencySamples_ = static_cast<Steinberg::uint32>(processor_.latencySamples());
  ensureScratchCapacity(static_cast<std::size_t>(newSetup.maxSamplesPerBlock));
  traceBlocksRemaining_ = kTraceBlockBudget;
  traceMessage(newSetup.processMode == Steinberg::Vst::kOffline ? "setupProcessing: offline" : "setupProcessing: realtime/prefetch");
  return Steinberg::kResultOk;
}

template <std::size_t ChannelCount>
Steinberg::tresult PLUGIN_API GainPilotPlugin<ChannelCount>::setBusArrangements(Steinberg::Vst::SpeakerArrangement* inputs,
                                                                                 Steinberg::int32 numIns,
                                                                                 Steinberg::Vst::SpeakerArrangement* outputs,
                                                                                 Steinberg::int32 numOuts) {
  if (numIns != 1 || numOuts != 1) {
    return Steinberg::kResultFalse;
  }
  if (Steinberg::Vst::SpeakerArr::getChannelCount(inputs[0]) != ChannelCount ||
      Steinberg::Vst::SpeakerArr::getChannelCount(outputs[0]) != ChannelCount) {
    return Steinberg::kResultFalse;
  }

  if (auto* inputBus = FCast<Steinberg::Vst::AudioBus>(this->audioInputs.at(0))) {
    inputBus->setArrangement(inputs[0]);
    inputBus->setName(inputBusName<ChannelCount>());
  }
  if (auto* outputBus = FCast<Steinberg::Vst::AudioBus>(this->audioOutputs.at(0))) {
    outputBus->setArrangement(outputs[0]);
    outputBus->setName(outputBusName<ChannelCount>());
  }
  return Steinberg::kResultTrue;
}

template <std::size_t ChannelCount>
Steinberg::tresult PLUGIN_API GainPilotPlugin<ChannelCount>::canProcessSampleSize(Steinberg::int32 symbolicSampleSize) {
  return symbolicSampleSize == Steinberg::Vst::kSample32 || symbolicSampleSize == Steinberg::Vst::kSample64
             ? Steinberg::kResultTrue
             : Steinberg::kResultFalse;
}

template <std::size_t ChannelCount>
void GainPilotPlugin<ChannelCount>::applyNormalizedParameter(Steinberg::Vst::ParamID tag,
                                                             Steinberg::Vst::ParamValue value) {
  if (static_cast<ParamId>(tag) == ParamId::inputLevel && value >= 1.0) {
    value = std::nextafter(1.0, 0.0);
  }

  const auto id = static_cast<ParamId>(tag);
  if (id < ParamId::count && isAutomatableStateParam(id)) {
    parameterState_.setNormalized(id, static_cast<float>(value));
    traceBlocksRemaining_ = kTraceBlockBudget;
  }
}

template <std::size_t ChannelCount>
Steinberg::uint32 PLUGIN_API GainPilotPlugin<ChannelCount>::getLatencySamples() {
  return latencySamples_;
}

template <std::size_t ChannelCount>
void GainPilotPlugin<ChannelCount>::applyInputParameterChanges(Steinberg::Vst::IParameterChanges* changes) {
  if (changes == nullptr) {
    return;
  }

  const auto numChanged = changes->getParameterCount();
  for (Steinberg::int32 index = 0; index < numChanged; ++index) {
    auto* queue = changes->getParameterData(index);
    if (queue == nullptr || queue->getPointCount() == 0) {
      continue;
    }

    Steinberg::Vst::ParamValue value = 0.0;
    Steinberg::int32 sampleOffset = 0;
    if (queue->getPoint(queue->getPointCount() - 1, sampleOffset, value) != Steinberg::kResultTrue) {
      continue;
    }

    applyNormalizedParameter(queue->getParameterId(), value);
  }
}

template <std::size_t ChannelCount>
void GainPilotPlugin<ChannelCount>::pushMeterOutput(Steinberg::Vst::IParameterChanges* changes) {
  if (changes == nullptr) {
    return;
  }

  const auto currentMeterPlain = processor_.currentMeterValue();
  const auto normalized = plainToNormalized(ParamId::meterValue, currentMeterPlain);
  if (std::abs(normalized - lastMeterNormalized_) < 1.0e-6) {
    return;
  }

  lastMeterNormalized_ = normalized;
  Steinberg::int32 queueIndex = 0;
  if (auto* queue = changes->addParameterData(toVstParamId(ParamId::meterValue), queueIndex)) {
    Steinberg::int32 pointIndex = 0;
    queue->addPoint(0, normalized, pointIndex);
  }
}

template <std::size_t ChannelCount>
void GainPilotPlugin<ChannelCount>::ensureScratchCapacity(std::size_t samples) {
  for (std::size_t channel = 0; channel < ChannelCount; ++channel) {
    tempInputs_[channel].resize(samples);
    tempOutputs_[channel].resize(samples);
    tempInputPtrs_[channel] = tempInputs_[channel].data();
    tempOutputPtrs_[channel] = tempOutputs_[channel].data();
  }
}

template <std::size_t ChannelCount>
bool GainPilotPlugin<ChannelCount>::resetForTransportDiscontinuity(const Steinberg::Vst::ProcessData& data) {
  if (data.processContext == nullptr) {
    return false;
  }

  const auto currentProjectTime = data.processContext->projectTimeSamples;
  bool needsReset = false;
  if (!lastProjectTimeSamples_.has_value()) {
    needsReset = currentProjectTime == 0;
  } else {
    const auto previousProjectTime = *lastProjectTimeSamples_;
    if (currentProjectTime < previousProjectTime) {
      needsReset = true;
    } else if (currentProjectTime == 0 && previousProjectTime > 0) {
      needsReset = true;
    }
  }

  if (needsReset) {
    processor_.reset();
    processor_.setParameters(parameterState_);
    traceBlocksRemaining_ = kTraceBlockBudget;
  }

  lastProjectTimeSamples_ = currentProjectTime;
  return needsReset;
}

template <std::size_t ChannelCount>
void GainPilotPlugin<ChannelCount>::traceMessage(const char* message) {
  if (!traceLog_.is_open()) {
    return;
  }

  traceLog_ << "[msg] " << message << '\n';
  traceLog_.flush();
}

template <std::size_t ChannelCount>
void GainPilotPlugin<ChannelCount>::traceProcessState(const Steinberg::Vst::ProcessData& data, bool transportReset) {
  if (!traceLog_.is_open() || traceBlocksRemaining_ == 0) {
    return;
  }

  const auto projectTime = data.processContext != nullptr ? data.processContext->projectTimeSamples : -1;
  traceLog_ << std::fixed << std::setprecision(3)
            << "[blk] mode=" << data.processMode
            << " reset=" << (transportReset ? 1 : 0)
            << " proj=" << projectTime
            << " ns=" << data.numSamples
            << " target=" << parameterState_.get(ParamId::targetLevel)
            << " input=" << parameterState_.get(ParamId::inputLevel)
            << " tp=" << parameterState_.get(ParamId::truePeak)
            << " gain=" << processor_.currentAppliedGainDb()
            << " inS=" << processor_.currentInputShortTermLufs()
            << " inI=" << processor_.currentInputIntegratedLufs()
            << " outS=" << processor_.currentOutputShortTermLufs()
            << " outI=" << processor_.currentOutputIntegratedLufs()
            << '\n';
  traceLog_.flush();
  --traceBlocksRemaining_;
}

template <std::size_t ChannelCount>
template <typename SampleType>
void GainPilotPlugin<ChannelCount>::processDoublePrecision(SampleType** input,
                                                           SampleType** output,
                                                           Steinberg::int32 numSamples) {
  ensureScratchCapacity(static_cast<std::size_t>(numSamples));

  for (std::size_t channel = 0; channel < ChannelCount; ++channel) {
    for (Steinberg::int32 sample = 0; sample < numSamples; ++sample) {
      tempInputs_[channel][sample] = static_cast<float>(input[channel][sample]);
    }
  }

  const dsp::ProcessBuffer buffer{
      .inputs = tempInputPtrs_.data(),
      .outputs = tempOutputPtrs_.data(),
      .channels = ChannelCount,
      .frames = static_cast<std::size_t>(numSamples),
  };
  processor_.process(buffer);

  for (std::size_t channel = 0; channel < ChannelCount; ++channel) {
    for (Steinberg::int32 sample = 0; sample < numSamples; ++sample) {
      output[channel][sample] = static_cast<SampleType>(tempOutputs_[channel][sample]);
    }
  }
}

template <std::size_t ChannelCount>
Steinberg::tresult PLUGIN_API GainPilotPlugin<ChannelCount>::process(Steinberg::Vst::ProcessData& data) {
  if (data.numInputs == 0 || data.numOutputs == 0 || data.numSamples <= 0) {
    return Steinberg::kResultOk;
  }

  applyInputParameterChanges(data.inputParameterChanges);
  processor_.setParameters(parameterState_);
  processor_.setOfflineMode(data.processMode == Steinberg::Vst::kOffline);
  const bool transportReset = resetForTransportDiscontinuity(data);

  if (data.symbolicSampleSize == Steinberg::Vst::kSample32) {
    auto** input =
        reinterpret_cast<float**>(Steinberg::Vst::getChannelBuffersPointer(this->processSetup, data.inputs[0]));
    auto** output =
        reinterpret_cast<float**>(Steinberg::Vst::getChannelBuffersPointer(this->processSetup, data.outputs[0]));
    const dsp::ProcessBuffer buffer{
        .inputs = const_cast<const float* const*>(input),
        .outputs = output,
        .channels = ChannelCount,
        .frames = static_cast<std::size_t>(data.numSamples),
    };
    processor_.process(buffer);
  } else {
    auto** input =
        reinterpret_cast<double**>(Steinberg::Vst::getChannelBuffersPointer(this->processSetup, data.inputs[0]));
    auto** output =
        reinterpret_cast<double**>(Steinberg::Vst::getChannelBuffersPointer(this->processSetup, data.outputs[0]));
    processDoublePrecision(input, output, data.numSamples);
  }

  pushMeterOutput(data.outputParameterChanges);
  traceProcessState(data, transportReset);
  return Steinberg::kResultOk;
}

template <std::size_t ChannelCount>
Steinberg::tresult PLUGIN_API GainPilotPlugin<ChannelCount>::setState(Steinberg::IBStream* state) {
  if (state == nullptr) {
    return Steinberg::kResultFalse;
  }

  const auto bytes = readStateBytes(state);
  if (bytes.empty()) {
    return Steinberg::kResultFalse;
  }

  const auto restored = deserializeState(bytes);
  if (!restored) {
    return Steinberg::kResultFalse;
  }

  parameterState_ = *restored;
  parameterState_.set(ParamId::meterReset, 0.0f);
  parameterState_.set(ParamId::meterValue, -70.0f);
  processor_.setParameters(parameterState_);
  lastProjectTimeSamples_.reset();
  traceBlocksRemaining_ = kTraceBlockBudget;
  traceMessage("setState");
  return Steinberg::kResultOk;
}

template <std::size_t ChannelCount>
Steinberg::tresult PLUGIN_API GainPilotPlugin<ChannelCount>::getState(Steinberg::IBStream* state) {
  if (state == nullptr) {
    return Steinberg::kResultFalse;
  }

  const auto bytes = serializeState(parameterState_);
  Steinberg::int32 bytesWritten = 0;
  return state->write(const_cast<std::byte*>(bytes.data()),
                      static_cast<Steinberg::int32>(bytes.size()),
                      &bytesWritten) == Steinberg::kResultTrue &&
                 bytesWritten == static_cast<Steinberg::int32>(bytes.size())
             ? Steinberg::kResultOk
             : Steinberg::kResultFalse;
}

template class GainPilotPlugin<1>;
template class GainPilotPlugin<2>;

}  // namespace gainpilot::vst3
