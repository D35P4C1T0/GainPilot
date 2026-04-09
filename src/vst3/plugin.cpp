#include "plugin.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cmath>

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/gui/iplugview.h"
#include "public.sdk/source/vst/vstaudioprocessoralgo.h"

#include "common.hpp"

namespace gainpilot::vst3 {

namespace {

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
}

template <std::size_t ChannelCount>
Steinberg::tresult PLUGIN_API GainPilotPlugin<ChannelCount>::initialize(Steinberg::FUnknown* context) {
  const auto result = AudioEffect::initialize(context);
  if (result != Steinberg::kResultOk) {
    return result;
  }

  this->addAudioInput(inputBusName<ChannelCount>(), speakerArrangementFor<ChannelCount>());
  this->addAudioOutput(outputBusName<ChannelCount>(), speakerArrangementFor<ChannelCount>());

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
void GainPilotPlugin<ChannelCount>::pushOutputParameter(Steinberg::Vst::IParameterChanges* changes,
                                                        ParamId id,
                                                        Steinberg::Vst::ParamValue plainValue,
                                                        std::size_t cacheIndex) {
  const auto normalized = plainToNormalized(id, static_cast<float>(plainValue));
  if (std::abs(normalized - lastRuntimeOutputNormalized_[cacheIndex]) < 1.0e-6) {
    return;
  }

  lastRuntimeOutputNormalized_[cacheIndex] = normalized;
  Steinberg::int32 queueIndex = 0;
  if (auto* queue = changes->addParameterData(toVstParamId(id), queueIndex)) {
    Steinberg::int32 pointIndex = 0;
    queue->addPoint(0, normalized, pointIndex);
  }
}

template <std::size_t ChannelCount>
void GainPilotPlugin<ChannelCount>::pushMeterOutput(Steinberg::Vst::IParameterChanges* changes) {
  if (changes == nullptr) {
    return;
  }

  pushOutputParameter(changes, ParamId::meterValue, processor_.currentMeterValue(), 0);
  pushOutputParameter(changes, ParamId::inputIntegratedValue, processor_.currentInputIntegratedLufs(), 1);
  pushOutputParameter(changes, ParamId::outputIntegratedValue, processor_.currentOutputIntegratedLufs(), 2);
  pushOutputParameter(changes, ParamId::outputShortTermValue, processor_.currentOutputShortTermLufs(), 3);
  pushOutputParameter(changes, ParamId::gainReductionValue, processor_.currentGainReductionDb(), 4);
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
  }

  lastProjectTimeSamples_ = currentProjectTime;
  return needsReset;
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
  const auto transportReset = resetForTransportDiscontinuity(data);
  static_cast<void>(transportReset);

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
  parameterState_.set(ParamId::inputIntegratedValue, -70.0f);
  parameterState_.set(ParamId::outputIntegratedValue, -70.0f);
  parameterState_.set(ParamId::outputShortTermValue, -70.0f);
  parameterState_.set(ParamId::gainReductionValue, 0.0f);
  processor_.setParameters(parameterState_);
  lastProjectTimeSamples_.reset();
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
