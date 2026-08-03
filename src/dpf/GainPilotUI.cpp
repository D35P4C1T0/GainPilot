#include "DistrhoUI.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>

#include "gainpilot/parameters.hpp"

START_NAMESPACE_DISTRHO

namespace {

using gainpilot::ParamId;

constexpr float kDesignWidth = 840.0f;
constexpr float kDesignHeight = 560.0f;
constexpr std::array<ParamId, 4> kSliderParameters{
    ParamId::targetLevel,
    ParamId::inputTrim,
    ParamId::truePeak,
    ParamId::maxGain,
};

constexpr std::uint32_t paramIndex(const ParamId id) noexcept
{
    return static_cast<std::uint32_t>(id);
}

struct Bounds
{
    float x;
    float y;
    float width;
    float height;

    bool contains(const float px, const float py) const noexcept
    {
        return px >= x && py >= y && px <= x + width && py <= y + height;
    }
};

Bounds sliderBounds(const ParamId id) noexcept
{
    switch (id)
    {
    case ParamId::targetLevel:
        return {52.0f, 224.0f, 224.0f, 28.0f};
    case ParamId::inputTrim:
        return {364.0f, 414.0f, 416.0f, 24.0f};
    case ParamId::truePeak:
        return {364.0f, 462.0f, 416.0f, 24.0f};
    case ParamId::maxGain:
        return {364.0f, 510.0f, 416.0f, 24.0f};
    default:
        return {};
    }
}

const char* shortLabel(const ParamId id) noexcept
{
    switch (id)
    {
    case ParamId::targetLevel:
        return "TARGET LEVEL";
    case ParamId::inputTrim:
        return "INPUT TRIM";
    case ParamId::truePeak:
        return "TRUE-PEAK CEILING";
    case ParamId::maxGain:
        return "MAX GAIN";
    default:
        return "";
    }
}

} // namespace

class GainPilotDPFUI final : public UI
{
public:
    GainPilotDPFUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        for (const gainpilot::ParameterSpec& spec : gainpilot::kParameterSpecs)
            values_[paramIndex(spec.id)] = spec.defaultValue;

        if constexpr (DISTRHO_PLUGIN_NUM_INPUTS == 1)
            values_[paramIndex(ParamId::channelMode)] =
                static_cast<float>(gainpilot::ChannelMode::mono);

        loadSharedResources();
        setGeometryConstraints(630, 420, true);
    }

protected:
    void parameterChanged(const std::uint32_t index, const float value) override
    {
        if (index >= gainpilot::kNumParameters)
            return;

        if (values_[index] == value)
            return;

        values_[index] = value;
        if (index == paramIndex(ParamId::appliedGainValue))
        {
            history_[historyWrite_] = std::clamp(value, -12.0f, 12.0f);
            historyWrite_ = (historyWrite_ + 1) % history_.size();
            historySize_ = std::min(historySize_ + 1, history_.size());
        }
        repaint();
    }

    void stateChanged(const char*, const char*) override {}

    void onNanoDisplay() override
    {
        const float sx = static_cast<float>(getWidth()) / kDesignWidth;
        const float sy = static_cast<float>(getHeight()) / kDesignHeight;
        save();
        scale(sx, sy);

        beginPath();
        rect(0.0f, 0.0f, kDesignWidth, kDesignHeight);
        fillColor(11, 13, 14);
        fill();

        drawHeader();
        drawTargetCard();
        drawResponseCard();

        restore();
    }

    bool onMouse(const MouseEvent& event) override
    {
        if (event.button != 1)
            return false;

        const float x = static_cast<float>(event.pos.getX()) * kDesignWidth /
                        static_cast<float>(getWidth());
        const float y = static_cast<float>(event.pos.getY()) * kDesignHeight /
                        static_cast<float>(getHeight());

        if (!event.press)
        {
            if (activeSlider_ != ParamId::count)
            {
                editParameter(paramIndex(activeSlider_), false);
                activeSlider_ = ParamId::count;
                return true;
            }
            if (resetPressed_)
            {
                setParameterValue(paramIndex(ParamId::meterReset), 0.0f);
                editParameter(paramIndex(ParamId::meterReset), false);
                resetPressed_ = false;
                repaint();
                return true;
            }
            return false;
        }

        for (const ParamId id : kSliderParameters)
        {
            const Bounds bounds = sliderBounds(id);
            if (!bounds.contains(x, y))
                continue;

            activeSlider_ = id;
            editParameter(paramIndex(id), true);
            updateSliderFromX(id, x);
            return true;
        }

        if (Bounds{352.0f, 118.0f, 84.0f, 32.0f}.contains(x, y))
            return setDiscreteParameter(ParamId::programMode, 0.0f);
        if (Bounds{440.0f, 118.0f, 84.0f, 32.0f}.contains(x, y))
            return setDiscreteParameter(ParamId::programMode, 1.0f);
        if (Bounds{588.0f, 118.0f, 88.0f, 32.0f}.contains(x, y))
            return setDiscreteParameter(ParamId::channelMode, 0.0f);
        if (Bounds{680.0f, 118.0f, 88.0f, 32.0f}.contains(x, y))
            return setDiscreteParameter(ParamId::channelMode, 1.0f);

        if (Bounds{54.0f, 474.0f, 220.0f, 42.0f}.contains(x, y))
        {
            resetPressed_ = true;
            editParameter(paramIndex(ParamId::meterReset), true);
            setParameterValue(paramIndex(ParamId::meterReset), 1.0f);
            repaint();
            return true;
        }

        return false;
    }

    bool onMotion(const MotionEvent& event) override
    {
        if (activeSlider_ == ParamId::count)
            return false;

        const float x = static_cast<float>(event.pos.getX()) * kDesignWidth /
                        static_cast<float>(getWidth());
        updateSliderFromX(activeSlider_, x);
        return true;
    }

    bool onScroll(const ScrollEvent& event) override
    {
        const float x = static_cast<float>(event.pos.getX()) * kDesignWidth /
                        static_cast<float>(getWidth());
        const float y = static_cast<float>(event.pos.getY()) * kDesignHeight /
                        static_cast<float>(getHeight());

        for (const ParamId id : kSliderParameters)
        {
            if (!sliderBounds(id).contains(x, y))
                continue;

            const gainpilot::ParameterSpec& spec = gainpilot::parameterSpec(id);
            const float step = (spec.maxValue - spec.minValue) / 100.0f;
            const float next = gainpilot::clampToSpec(
                id,
                values_[paramIndex(id)] + static_cast<float>(event.delta.getY()) * step);
            editParameter(paramIndex(id), true);
            values_[paramIndex(id)] = next;
            setParameterValue(paramIndex(id), next);
            editParameter(paramIndex(id), false);
            repaint();
            return true;
        }
        return false;
    }

private:
    void drawHeader()
    {
        drawText(28.0f, 27.0f, 25.0f, "GainPilot", 199, 204, 206, ALIGN_LEFT | ALIGN_MIDDLE);
        drawText(28.0f,
                 53.0f,
                 13.0f,
                 "Adaptive LUFS leveling  /  linked true-peak protection",
                 122,
                 128,
                 130,
                 ALIGN_LEFT | ALIGN_MIDDLE);
        drawText(812.0f,
                 39.0f,
                 13.0f,
                 "BS.1770 / EBU R128",
                 46,
                 230,
                 214,
                 ALIGN_RIGHT | ALIGN_MIDDLE);
    }

    void drawTargetCard()
    {
        drawPanel(24.0f, 82.0f, 280.0f, 454.0f);
        drawText(164.0f, 119.0f, 12.0f, "TARGET LUFS", 122, 128, 130, ALIGN_CENTER | ALIGN_MIDDLE);

        char textBuffer[64]{};
        std::snprintf(textBuffer,
                      sizeof(textBuffer),
                      "%.1f",
                      values_[paramIndex(ParamId::targetLevel)]);
        drawText(164.0f, 174.0f, 58.0f, textBuffer, 46, 230, 214, ALIGN_CENTER | ALIGN_MIDDLE);
        drawSlider(ParamId::targetLevel);

        drawReadout(54.0f, 291.0f, "Input", ParamId::inputIntegratedValue, "LUFS-I");
        drawReadout(54.0f, 329.0f, "Output", ParamId::outputIntegratedValue, "LUFS-I");
        drawReadout(54.0f, 367.0f, "Short-term", ParamId::outputShortTermValue, "LUFS");
        drawReadout(54.0f, 405.0f, "Gain reduction", ParamId::gainReductionValue, "dB");
        drawReadout(54.0f, 443.0f, "Applied gain", ParamId::appliedGainValue, "dB");

        beginPath();
        roundedRect(54.0f, 474.0f, 220.0f, 42.0f, 7.0f);
        fillColor(resetPressed_ ? 46 : 27, resetPressed_ ? 230 : 43, resetPressed_ ? 214 : 44);
        fill();
        strokeWidth(1.0f);
        strokeColor(46, 230, 214);
        stroke();
        drawText(164.0f,
                 495.0f,
                 13.0f,
                 "RESET / RELEARN",
                 resetPressed_ ? 11 : 199,
                 resetPressed_ ? 13 : 204,
                 resetPressed_ ? 14 : 206,
                 ALIGN_CENTER | ALIGN_MIDDLE);
    }

    void drawResponseCard()
    {
        drawPanel(322.0f, 82.0f, 494.0f, 454.0f);
        drawText(350.0f, 100.0f, 11.0f, "PROGRAM", 122, 128, 130, ALIGN_LEFT | ALIGN_MIDDLE);
        drawText(586.0f, 100.0f, 11.0f, "CHANNEL", 122, 128, 130, ALIGN_LEFT | ALIGN_MIDDLE);
        drawModeButton({352.0f, 118.0f, 84.0f, 32.0f},
                       "AUTO",
                       values_[paramIndex(ParamId::programMode)] < 0.5f);
        drawModeButton({440.0f, 118.0f, 84.0f, 32.0f},
                       "SPEECH",
                       values_[paramIndex(ParamId::programMode)] >= 0.5f);
        drawModeButton({588.0f, 118.0f, 88.0f, 32.0f},
                       "STEREO",
                       values_[paramIndex(ParamId::channelMode)] < 0.5f);
        drawModeButton({680.0f, 118.0f, 88.0f, 32.0f},
                       "MONO",
                       values_[paramIndex(ParamId::channelMode)] >= 0.5f);

        drawHistory();
        drawSlider(ParamId::inputTrim);
        drawSlider(ParamId::truePeak);
        drawSlider(ParamId::maxGain);
    }

    void drawPanel(const float x, const float y, const float width, const float height)
    {
        beginPath();
        roundedRect(x, y, width, height, 10.0f);
        fillColor(19, 21, 22);
        fill();
        strokeWidth(1.0f);
        strokeColor(39, 43, 44);
        stroke();
    }

    void drawModeButton(const Bounds& bounds, const char* const label, const bool selected)
    {
        beginPath();
        roundedRect(bounds.x, bounds.y, bounds.width, bounds.height, 5.0f);
        fillColor(selected ? 46 : 27, selected ? 230 : 43, selected ? 214 : 44);
        fill();
        drawText(bounds.x + bounds.width * 0.5f,
                 bounds.y + bounds.height * 0.5f,
                 11.0f,
                 label,
                 selected ? 11 : 199,
                 selected ? 13 : 204,
                 selected ? 14 : 206,
                 ALIGN_CENTER | ALIGN_MIDDLE);
    }

    void drawSlider(const ParamId id)
    {
        const Bounds bounds = sliderBounds(id);
        const gainpilot::ParameterSpec& spec = gainpilot::parameterSpec(id);
        const float value = values_[paramIndex(id)];
        const float normalized = std::clamp((value - spec.minValue) /
                                                (spec.maxValue - spec.minValue),
                                            0.0f,
                                            1.0f);

        char valueBuffer[48]{};
        if (id == ParamId::targetLevel)
            std::snprintf(valueBuffer, sizeof(valueBuffer), "%.1f LUFS", value);
        else
            std::snprintf(valueBuffer, sizeof(valueBuffer), "%+.1f dB", value);
        drawText(bounds.x,
                 bounds.y - 14.0f,
                 11.0f,
                 shortLabel(id),
                 122,
                 128,
                 130,
                 ALIGN_LEFT | ALIGN_MIDDLE);
        drawText(bounds.x + bounds.width,
                 bounds.y - 14.0f,
                 12.0f,
                 valueBuffer,
                 199,
                 204,
                 206,
                 ALIGN_RIGHT | ALIGN_MIDDLE);

        const float trackY = bounds.y + bounds.height * 0.5f - 3.0f;
        beginPath();
        roundedRect(bounds.x, trackY, bounds.width, 6.0f, 3.0f);
        fillColor(39, 43, 44);
        fill();
        beginPath();
        roundedRect(bounds.x, trackY, bounds.width * normalized, 6.0f, 3.0f);
        fillColor(46, 230, 214);
        fill();
        beginPath();
        circle(bounds.x + bounds.width * normalized, trackY + 3.0f, 7.0f);
        fillColor(199, 204, 206);
        fill();
    }

    void drawReadout(const float x,
                     const float y,
                     const char* const label,
                     const ParamId id,
                     const char* const unit)
    {
        char valueBuffer[64]{};
        std::snprintf(valueBuffer,
                      sizeof(valueBuffer),
                      "%+.1f %s",
                      values_[paramIndex(id)],
                      unit);
        drawText(x, y, 12.0f, label, 122, 128, 130, ALIGN_LEFT | ALIGN_MIDDLE);
        drawText(274.0f, y, 13.0f, valueBuffer, 199, 204, 206, ALIGN_RIGHT | ALIGN_MIDDLE);
    }

    void drawHistory()
    {
        constexpr Bounds graph{350.0f, 176.0f, 438.0f, 190.0f};
        beginPath();
        roundedRect(graph.x, graph.y, graph.width, graph.height, 7.0f);
        fillColor(11, 13, 14);
        fill();

        drawText(graph.x,
                 graph.y - 12.0f,
                 11.0f,
                 "APPLIED GAIN HISTORY",
                 122,
                 128,
                 130,
                 ALIGN_LEFT | ALIGN_MIDDLE);
        char currentBuffer[48]{};
        std::snprintf(currentBuffer,
                      sizeof(currentBuffer),
                      "%+.1f dB",
                      values_[paramIndex(ParamId::appliedGainValue)]);
        drawText(graph.x + graph.width,
                 graph.y - 12.0f,
                 13.0f,
                 currentBuffer,
                 46,
                 230,
                 214,
                 ALIGN_RIGHT | ALIGN_MIDDLE);

        for (int gain = -12; gain <= 12; gain += 6)
        {
            const float y = graph.y + (12.0f - static_cast<float>(gain)) / 24.0f * graph.height;
            beginPath();
            moveTo(graph.x, y);
            lineTo(graph.x + graph.width, y);
            strokeWidth(gain == 0 ? 1.5f : 1.0f);
            strokeColor(gain == 0 ? 70 : 39, gain == 0 ? 76 : 43, gain == 0 ? 78 : 44);
            stroke();
        }

        if (historySize_ < 2)
            return;

        beginPath();
        for (std::size_t i = 0; i < historySize_; ++i)
        {
            const std::size_t offset = (historyWrite_ + history_.size() - historySize_ + i) % history_.size();
            const float x = graph.x + static_cast<float>(i) /
                                          static_cast<float>(historySize_ - 1) * graph.width;
            const float y = graph.y + (12.0f - history_[offset]) / 24.0f * graph.height;
            if (i == 0)
                moveTo(x, y);
            else
                lineTo(x, y);
        }
        strokeWidth(2.0f);
        strokeColor(46, 230, 214);
        stroke();
    }

    void drawText(const float x,
                  const float y,
                  const float size,
                  const char* const value,
                  const int red,
                  const int green,
                  const int blue,
                  const int alignment)
    {
        fontSize(size);
        textAlign(alignment);
        fillColor(red, green, blue);
        text(x, y, value, nullptr);
    }

    bool setDiscreteParameter(const ParamId id, const float value)
    {
        if constexpr (DISTRHO_PLUGIN_NUM_INPUTS == 1)
        {
            if (id == ParamId::channelMode && value < 0.5f)
                return true;
        }

        values_[paramIndex(id)] = value;
        editParameter(paramIndex(id), true);
        setParameterValue(paramIndex(id), value);
        editParameter(paramIndex(id), false);
        repaint();
        return true;
    }

    void updateSliderFromX(const ParamId id, const float x)
    {
        const Bounds bounds = sliderBounds(id);
        const gainpilot::ParameterSpec& spec = gainpilot::parameterSpec(id);
        const float normalized = std::clamp((x - bounds.x) / bounds.width, 0.0f, 1.0f);
        const float value = spec.minValue + normalized * (spec.maxValue - spec.minValue);
        values_[paramIndex(id)] = value;
        setParameterValue(paramIndex(id), value);
        repaint();
    }

    std::array<float, gainpilot::kNumParameters> values_{};
    std::array<float, 180> history_{};
    std::size_t historyWrite_{0};
    std::size_t historySize_{0};
    ParamId activeSlider_{ParamId::count};
    bool resetPressed_{false};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GainPilotDPFUI)
};

UI* createUI()
{
    return new GainPilotDPFUI();
}

END_NAMESPACE_DISTRHO
