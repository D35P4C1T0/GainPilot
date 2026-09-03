#include "DistrhoPlugin.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <optional>
#include <string>
#include <vector>

#include "gainpilot/dsp/processor.hpp"
#include "gainpilot/parameters.hpp"
#include "gainpilot/state.hpp"
#include "gainpilot/version.hpp"

START_NAMESPACE_DISTRHO

namespace {

using gainpilot::ParamId;
using gainpilot::ParameterState;

constexpr bool isMonoBuild = DISTRHO_PLUGIN_NUM_INPUTS == 1;
constexpr char kStateKey[] = "blob";

const char* parameterUnit(const ParamId id) noexcept
{
    switch (id)
    {
    case ParamId::targetLevel:
    case ParamId::freezeLevel:
    case ParamId::inputLevel:
    case ParamId::meterValue:
    case ParamId::inputIntegratedValue:
    case ParamId::outputIntegratedValue:
    case ParamId::outputShortTermValue:
        return "LUFS";
    case ParamId::truePeak:
    case ParamId::maxGain:
    case ParamId::maxCut:
    case ParamId::inputTrim:
    case ParamId::gainReductionValue:
    case ParamId::appliedGainValue:
        return "dB";
    case ParamId::correctionHigh:
    case ParamId::correctionLow:
        return "%";
    default:
        return "";
    }
}

void setEnumeration(Parameter& parameter,
                    const std::initializer_list<ParameterEnumerationValue> values)
{
    parameter.enumValues.count = static_cast<std::uint8_t>(values.size());
    parameter.enumValues.restrictedMode = true;
    parameter.enumValues.values = new ParameterEnumerationValue[values.size()];
    std::copy(values.begin(), values.end(), parameter.enumValues.values);
}

std::string encodeState(const ParameterState& state)
{
    static constexpr char digits[] = "0123456789abcdef";
    const std::vector<std::byte> bytes = gainpilot::serializeState(state);
    std::string encoded;
    encoded.resize(bytes.size() * 2);

    for (std::size_t i = 0; i < bytes.size(); ++i)
    {
        const auto value = std::to_integer<unsigned int>(bytes[i]);
        encoded[i * 2] = digits[value >> 4];
        encoded[i * 2 + 1] = digits[value & 0x0f];
    }
    return encoded;
}

int hexDigit(const char value) noexcept
{
    if (value >= '0' && value <= '9')
        return value - '0';
    if (value >= 'a' && value <= 'f')
        return value - 'a' + 10;
    if (value >= 'A' && value <= 'F')
        return value - 'A' + 10;
    return -1;
}

std::optional<ParameterState> decodeState(const char* const encoded)
{
    if (encoded == nullptr)
        return std::nullopt;

    const std::size_t length = std::strlen(encoded);
    if (length == 0 || length % 2 != 0)
        return std::nullopt;

    std::vector<std::byte> bytes(length / 2);
    for (std::size_t i = 0; i < bytes.size(); ++i)
    {
        const int high = hexDigit(encoded[i * 2]);
        const int low = hexDigit(encoded[i * 2 + 1]);
        if (high < 0 || low < 0)
            return std::nullopt;
        bytes[i] = static_cast<std::byte>((high << 4) | low);
    }

    return gainpilot::deserializeState(bytes);
}

} // namespace

class GainPilotDPFPlugin final : public Plugin
{
public:
    GainPilotDPFPlugin()
        : Plugin(static_cast<std::uint32_t>(gainpilot::kNumParameters), 0, 1)
    {
        if constexpr (isMonoBuild)
            parameters_.set(ParamId::channelMode,
                            static_cast<float>(gainpilot::ChannelMode::mono));
        prepareProcessor(getSampleRate(), getBufferSize());
    }

protected:
    const char* getDescription() const override
    {
        return "Adaptive BS.1770 / EBU-R128 loudness leveling with true-peak protection.";
    }

    const char* getMaker() const override
    {
        return "GainPilot contributors";
    }

    const char* getHomePage() const override
    {
        return "https://gainpilot.dev";
    }

    const char* getLicense() const override
    {
        return "MIT";
    }

    std::uint32_t getVersion() const override
    {
        return d_version(gainpilot::kVersionMajor,
                         gainpilot::kVersionMinor,
                         gainpilot::kVersionPatch);
    }

    void initAudioPort(const bool input, const std::uint32_t index, AudioPort& port) override
    {
        port.groupId = isMonoBuild ? kPortGroupMono : kPortGroupStereo;
        Plugin::initAudioPort(input, index, port);
    }

    void initParameter(const std::uint32_t index, Parameter& parameter) override
    {
        if (index >= gainpilot::kNumParameters)
            return;

        const auto id = static_cast<ParamId>(index);
        const gainpilot::ParameterSpec& spec = gainpilot::parameterSpec(id);
        parameter.name = spec.name.data();
        parameter.symbol = spec.key.data();
        parameter.unit = parameterUnit(id);
        parameter.ranges.min = spec.minValue;
        parameter.ranges.max = spec.maxValue;
        parameter.ranges.def = spec.defaultValue;
        parameter.hints = spec.automatable ? kParameterIsAutomatable : 0;

        if (spec.outputOnly)
            parameter.hints = kParameterIsOutput;

        switch (id)
        {
        case ParamId::meterReset:
            parameter.hints = kParameterIsAutomatable | kParameterIsTrigger |
                              kParameterIsHidden;
            break;
        case ParamId::programMode:
            parameter.hints |= kParameterIsInteger;
            setEnumeration(parameter, {{0.0f, "Automatic"}, {1.0f, "Speech"}});
            break;
        case ParamId::channelMode:
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.def = isMonoBuild ? 1.0f : spec.defaultValue;
            setEnumeration(parameter, {{0.0f, "Stereo"}, {1.0f, "Mono"}});
            break;
        case ParamId::corrMixMode:
            parameter.hints |= kParameterIsInteger | kParameterIsHidden;
            setEnumeration(parameter,
                           {{0.0f, "Linear / Linear"},
                            {1.0f, "Linear / Log"},
                            {2.0f, "Log / Linear"},
                            {3.0f, "Log / Log"}});
            break;
        case ParamId::meterMode:
            parameter.hints |= kParameterIsInteger | kParameterIsHidden;
            setEnumeration(parameter,
                           {{0.0f, "Momentary"},
                            {1.0f, "Short-Term"},
                            {2.0f, "Integrated"}});
            break;
        case ParamId::freezeLevel:
        case ParamId::inputLevel:
        case ParamId::correctionHigh:
        case ParamId::correctionLow:
            parameter.hints |= kParameterIsHidden;
            break;
        default:
            break;
        }
    }

    void initState(const std::uint32_t index, State& state) override
    {
        if (index != 0)
            return;

        const std::string defaultState = encodeState(parameters_);
        state.key = kStateKey;
        state.defaultValue = defaultState.c_str();
        state.label = "GainPilot state";
        state.description = "Versioned cross-format GainPilot parameter state";
        state.hints = kStateIsHostReadable | kStateIsOnlyForDSP;
    }

    float getParameterValue(const std::uint32_t index) const override
    {
        if (index >= gainpilot::kNumParameters)
            return 0.0f;

        switch (static_cast<ParamId>(index))
        {
        case ParamId::meterValue:
            return processor_.currentMeterValue();
        case ParamId::inputIntegratedValue:
            return processor_.currentInputIntegratedLufs();
        case ParamId::outputIntegratedValue:
            return processor_.currentOutputIntegratedLufs();
        case ParamId::outputShortTermValue:
            return processor_.currentOutputShortTermLufs();
        case ParamId::gainReductionValue:
            return processor_.currentGainReductionDb();
        case ParamId::appliedGainValue:
            return processor_.currentAppliedGainDb();
        default:
            return parameters_.get(static_cast<ParamId>(index));
        }
    }

    void setParameterValue(const std::uint32_t index, const float value) override
    {
        if (index >= gainpilot::kNumParameters)
            return;

        const auto id = static_cast<ParamId>(index);
        if (gainpilot::parameterSpec(id).outputOnly)
            return;

        if constexpr (isMonoBuild)
        {
            if (id == ParamId::channelMode)
            {
                parameters_.set(id, static_cast<float>(gainpilot::ChannelMode::mono));
                return;
            }
        }
        parameters_.set(id, value);
    }

    String getState(const char* const key) const override
    {
        if (key == nullptr || std::strcmp(key, kStateKey) != 0)
            return String();
        const std::string encoded = encodeState(parameters_);
        return String(encoded.c_str());
    }

    void setState(const char* const key, const char* const value) override
    {
        if (key == nullptr || std::strcmp(key, kStateKey) != 0)
            return;

        const std::optional<ParameterState> restored = decodeState(value);
        if (!restored)
            return;

        parameters_ = *restored;
        parameters_.set(ParamId::meterReset, 0.0f);
        if constexpr (isMonoBuild)
            parameters_.set(ParamId::channelMode,
                            static_cast<float>(gainpilot::ChannelMode::mono));
        processor_.setParameters(parameters_);
    }

    void activate() override
    {
        processor_.reset();
        processor_.setParameters(parameters_);
        lastTransportFrame_.reset();
        setLatency(static_cast<std::uint32_t>(processor_.latencySamples()));
    }

    void run(const float** const inputs, float** const outputs, const std::uint32_t frames) override
    {
        const TimePosition& position = getTimePosition();
        if (lastTransportFrame_ && position.frame < *lastTransportFrame_)
        {
            processor_.reset();
            processor_.setParameters(parameters_);
        }
        lastTransportFrame_ = position.frame;

        processor_.setParameters(parameters_);
        processor_.process({
            .inputs = inputs,
            .outputs = outputs,
            .channels = DISTRHO_PLUGIN_NUM_INPUTS,
            .frames = frames,
        });

    }

    void bufferSizeChanged(const std::uint32_t newBufferSize) override
    {
        prepareProcessor(getSampleRate(), newBufferSize);
    }

    void sampleRateChanged(const double newSampleRate) override
    {
        prepareProcessor(newSampleRate, getBufferSize());
    }

private:
    void prepareProcessor(const double sampleRate, const std::uint32_t bufferSize)
    {
        processor_.prepare(sampleRate,
                           DISTRHO_PLUGIN_NUM_INPUTS,
                           std::max<std::uint32_t>(bufferSize, 1));
        processor_.setParameters(parameters_);
        setLatency(static_cast<std::uint32_t>(processor_.latencySamples()));
        lastTransportFrame_.reset();
    }

    ParameterState parameters_{};
    gainpilot::dsp::GainPilotProcessor processor_{};
    std::optional<std::uint64_t> lastTransportFrame_{};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GainPilotDPFPlugin)
};

Plugin* createPlugin()
{
    return new GainPilotDPFPlugin();
}

END_NAMESPACE_DISTRHO
