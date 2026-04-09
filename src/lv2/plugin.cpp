#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

#include <lv2/core/lv2.h>
#include <lv2/core/lv2_util.h>
#include <lv2/atom/atom.h>
#include <lv2/state/state.h>
#include <lv2/urid/urid.h>

#include "gainpilot/dsp/processor.hpp"
#include "gainpilot/parameters.hpp"
#include "gainpilot/state.hpp"

namespace {

using gainpilot::ParamId;
using gainpilot::ParameterState;
using gainpilot::dsp::GainPilotProcessor;
using gainpilot::dsp::ProcessBuffer;

constexpr char kStateBlobUri[] = "https://gainpilot.dev/state/blob";

enum PortIndex : std::uint32_t {
  kAudioInL = 0,
  kAudioInR = 1,
  kAudioOutL = 2,
  kAudioOutR = 3,
  kTargetLevel = 4,
  kTruePeak = 5,
  kMaxGain = 6,
  kInputTrim = 7,
  kProgramMode = 8,
  kFreezeLevel = 9,
  kInputLevel = 10,
  kCorrectionHigh = 11,
  kCorrectionLow = 12,
  kCorrMixMode = 13,
  kMeterMode = 14,
  kMeterReset = 15,
  kMeterValue = 16,
  kInputIntegratedValue = 17,
  kOutputIntegratedValue = 18,
  kOutputShortTermValue = 19,
  kGainReductionValue = 20,
  kLatency = 21
};

constexpr std::uint32_t kNumAudioPorts = GAINPILOT_LV2_CHANNELS * 2;

class PluginInstance {
public:
  explicit PluginInstance(double sampleRate, const LV2_Feature* const* features)
      : sampleRate_(sampleRate),
        audioInputs_(GAINPILOT_LV2_CHANNELS, nullptr),
        audioOutputs_(GAINPILOT_LV2_CHANNELS, nullptr) {
    if (features != nullptr) {
      map_ = static_cast<LV2_URID_Map*>(lv2_features_data(features, LV2_URID__map));
      mapUris();
    }
    processor_.prepare(sampleRate_, GAINPILOT_LV2_CHANNELS, 4096);
  }

  void connectPort(std::uint32_t port, void* data) {
    if (port < GAINPILOT_LV2_CHANNELS) {
      audioInputs_[port] = static_cast<const float*>(data);
      return;
    }

    if (port < kNumAudioPorts) {
      audioOutputs_[port - GAINPILOT_LV2_CHANNELS] = static_cast<float*>(data);
      return;
    }

    controlPorts_[port - kNumAudioPorts] = static_cast<float*>(data);
  }

  void activate() {
    processor_.reset();
  }

  LV2_State_Status save(LV2_State_Store_Function store,
                        LV2_State_Handle handle,
                        const LV2_Feature* const* features) const {
    if (store == nullptr) {
      return LV2_STATE_ERR_UNKNOWN;
    }

    if (!ensureMapped(features)) {
      return LV2_STATE_ERR_NO_FEATURE;
    }

    const auto bytes = gainpilot::serializeState(snapshotStateForSave());
    return store(handle,
                 uris_.stateBlob,
                 bytes.data(),
                 bytes.size(),
                 uris_.atomChunk,
                 LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);
  }

  LV2_State_Status restore(LV2_State_Retrieve_Function retrieve,
                           LV2_State_Handle handle,
                           const LV2_Feature* const* features) {
    if (retrieve == nullptr) {
      return LV2_STATE_ERR_UNKNOWN;
    }

    if (!ensureMapped(features)) {
      return LV2_STATE_ERR_NO_FEATURE;
    }

    size_t size = 0;
    uint32_t type = 0;
    uint32_t flags = 0;
    const void* value = retrieve(handle, uris_.stateBlob, &size, &type, &flags);
    if (value == nullptr) {
      return LV2_STATE_ERR_NO_PROPERTY;
    }
    if (size == 0 || type != uris_.atomChunk) {
      return LV2_STATE_ERR_BAD_TYPE;
    }

    const auto* begin = static_cast<const std::byte*>(value);
    const auto restored = gainpilot::deserializeState(std::span<const std::byte>(begin, size));
    if (!restored) {
      return LV2_STATE_ERR_BAD_TYPE;
    }

    parameters_ = *restored;
    parameters_.set(ParamId::meterReset, 0.0f);
    parameters_.set(ParamId::meterValue, -70.0f);
    parameters_.set(ParamId::inputIntegratedValue, -70.0f);
    parameters_.set(ParamId::outputIntegratedValue, -70.0f);
    parameters_.set(ParamId::outputShortTermValue, -70.0f);
    parameters_.set(ParamId::gainReductionValue, 0.0f);
    processor_.setParameters(parameters_);
    return LV2_STATE_SUCCESS;
  }

  void run(std::uint32_t frames) {
    for (std::size_t index = 0; index < inputControlMap.size(); ++index) {
      if (controlPorts_[index] == nullptr) {
        continue;
      }
      parameters_.set(inputControlMap[index], *controlPorts_[index]);
    }

    processor_.setParameters(parameters_);

    ProcessBuffer buffer{
        .inputs = audioInputs_.data(),
        .outputs = audioOutputs_.data(),
        .channels = GAINPILOT_LV2_CHANNELS,
        .frames = frames,
    };

    processor_.process(buffer);

    if (controlPorts_[kControlMeterValue] != nullptr) {
      *controlPorts_[kControlMeterValue] = processor_.currentMeterValue();
    }
    if (controlPorts_[kControlInputIntegratedValue] != nullptr) {
      *controlPorts_[kControlInputIntegratedValue] = processor_.currentInputIntegratedLufs();
    }
    if (controlPorts_[kControlOutputIntegratedValue] != nullptr) {
      *controlPorts_[kControlOutputIntegratedValue] = processor_.currentOutputIntegratedLufs();
    }
    if (controlPorts_[kControlOutputShortTermValue] != nullptr) {
      *controlPorts_[kControlOutputShortTermValue] = processor_.currentOutputShortTermLufs();
    }
    if (controlPorts_[kControlGainReductionValue] != nullptr) {
      *controlPorts_[kControlGainReductionValue] = processor_.currentGainReductionDb();
    }
    if (controlPorts_[kControlLatency] != nullptr) {
      *controlPorts_[kControlLatency] = processor_.currentLatencySamples();
    }
  }

private:
  static constexpr std::size_t kControlMeterValue = 12;
  static constexpr std::size_t kControlInputIntegratedValue = 13;
  static constexpr std::size_t kControlOutputIntegratedValue = 14;
  static constexpr std::size_t kControlOutputShortTermValue = 15;
  static constexpr std::size_t kControlGainReductionValue = 16;
  static constexpr std::size_t kControlLatency = 17;
  static constexpr std::array inputControlMap{
      ParamId::targetLevel,
      ParamId::truePeak,
      ParamId::maxGain,
      ParamId::inputTrim,
      ParamId::programMode,
      ParamId::freezeLevel,
      ParamId::inputLevel,
      ParamId::correctionHigh,
      ParamId::correctionLow,
      ParamId::corrMixMode,
      ParamId::meterMode,
      ParamId::meterReset,
  };
  struct Uris {
    LV2_URID stateBlob{0};
    LV2_URID atomChunk{0};
  };

  [[nodiscard]] ParameterState snapshotStateForSave() const {
    ParameterState snapshot = parameters_;
    for (std::size_t index = 0; index < inputControlMap.size(); ++index) {
      if (controlPorts_[index] != nullptr) {
        snapshot.set(inputControlMap[index], *controlPorts_[index]);
      }
    }
    snapshot.set(ParamId::meterReset, 0.0f);
    snapshot.set(ParamId::meterValue, -70.0f);
    snapshot.set(ParamId::inputIntegratedValue, -70.0f);
    snapshot.set(ParamId::outputIntegratedValue, -70.0f);
    snapshot.set(ParamId::outputShortTermValue, -70.0f);
    snapshot.set(ParamId::gainReductionValue, 0.0f);
    return snapshot;
  }

  [[nodiscard]] bool ensureMapped(const LV2_Feature* const* features) const {
    if (map_ != nullptr && uris_.stateBlob != 0 && uris_.atomChunk != 0) {
      return true;
    }

    if (features != nullptr && map_ == nullptr) {
      map_ = static_cast<LV2_URID_Map*>(lv2_features_data(features, LV2_URID__map));
      mapUris();
    }

    return map_ != nullptr && uris_.stateBlob != 0 && uris_.atomChunk != 0;
  }

  void mapUris() const {
    if (map_ == nullptr) {
      return;
    }
    uris_.stateBlob = map_->map(map_->handle, kStateBlobUri);
    uris_.atomChunk = map_->map(map_->handle, LV2_ATOM__Chunk);
  }

  double sampleRate_{};
  ParameterState parameters_{};
  GainPilotProcessor processor_{};
  std::vector<const float*> audioInputs_{};
  std::vector<float*> audioOutputs_{};
  mutable LV2_URID_Map* map_{nullptr};
  mutable Uris uris_{};
  std::array<float*, 18> controlPorts_{};
};

LV2_State_Status saveState(LV2_Handle instance,
                           LV2_State_Store_Function store,
                           LV2_State_Handle handle,
                           uint32_t,
                           const LV2_Feature* const* features) {
  return static_cast<const PluginInstance*>(instance)->save(store, handle, features);
}

LV2_State_Status restoreState(LV2_Handle instance,
                              LV2_State_Retrieve_Function retrieve,
                              LV2_State_Handle handle,
                              uint32_t,
                              const LV2_Feature* const* features) {
  return static_cast<PluginInstance*>(instance)->restore(retrieve, handle, features);
}

constexpr LV2_State_Interface kStateInterface{
    saveState,
    restoreState,
};

LV2_Handle instantiate(const LV2_Descriptor*, double rate, const char*, const LV2_Feature* const* features) {
  return new PluginInstance(rate, features);
}

void connectPort(LV2_Handle instance, std::uint32_t port, void* data) {
  static_cast<PluginInstance*>(instance)->connectPort(port, data);
}

void activate(LV2_Handle instance) {
  static_cast<PluginInstance*>(instance)->activate();
}

void run(LV2_Handle instance, std::uint32_t sampleCount) {
  static_cast<PluginInstance*>(instance)->run(sampleCount);
}

void deactivate(LV2_Handle) {}

void cleanup(LV2_Handle instance) {
  delete static_cast<PluginInstance*>(instance);
}

const void* extensionData(const char* uri) {
  if (uri != nullptr && std::strcmp(uri, LV2_STATE__interface) == 0) {
    return &kStateInterface;
  }
  return nullptr;
}

constexpr LV2_Descriptor kDescriptor{
    GAINPILOT_LV2_URI,
    instantiate,
    connectPort,
    activate,
    run,
    deactivate,
    cleanup,
    extensionData,
};

}  // namespace

LV2_SYMBOL_EXPORT
const LV2_Descriptor* lv2_descriptor(std::uint32_t index) {
  return index == 0 ? &kDescriptor : nullptr;
}
