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
constexpr float kDesignHeight = 472.0f;
constexpr std::array<ParamId, 4> kSliderParameters{
    ParamId::targetLevel,
    ParamId::inputTrim,
    ParamId::truePeak,
    ParamId::maxGain,
};
constexpr std::array<float, 4> kUiScales{0.75f, 1.0f, 1.25f, 1.5f};
constexpr std::array<const char *, 4> kUiScaleLabels{"75%", "100%", "125%",
                                                     "150%"};

constexpr std::uint32_t paramIndex(const ParamId id) noexcept {
  return static_cast<std::uint32_t>(id);
}

struct Bounds {
  float x{};
  float y{};
  float width{};
  float height{};

  bool contains(const float px, const float py) const noexcept {
    return px >= x && py >= y && px <= x + width && py <= y + height;
  }
};

Bounds sliderBounds(const ParamId id) noexcept {
  switch (id) {
  case ParamId::targetLevel:
    return {67.0f, 207.0f, 218.0f, 25.0f};
  case ParamId::inputTrim:
    return {333.0f, 319.0f, 143.0f, 112.0f};
  case ParamId::truePeak:
    return {481.0f, 319.0f, 143.0f, 112.0f};
  case ParamId::maxGain:
    return {629.0f, 319.0f, 143.0f, 112.0f};
  default:
    return {};
  }
}

Bounds scaleButtonBounds(const std::size_t index) noexcept {
  return {645.0f + static_cast<float>(index) * 35.0f, 45.0f, 32.0f, 15.0f};
}

const char *shortLabel(const ParamId id) noexcept {
  switch (id) {
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

class GainPilotDPFUI final : public UI {
public:
  GainPilotDPFUI() : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT) {
    for (const gainpilot::ParameterSpec &spec : gainpilot::kParameterSpecs)
      values_[paramIndex(spec.id)] = spec.defaultValue;

    if constexpr (DISTRHO_PLUGIN_NUM_INPUTS == 1)
      values_[paramIndex(ParamId::channelMode)] =
          static_cast<float>(gainpilot::ChannelMode::mono);

    loadSharedResources();
    setGeometryConstraints(630, 354, true);
  }

protected:
  void parameterChanged(const std::uint32_t index, const float value) override {
    if (index >= gainpilot::kNumParameters || values_[index] == value)
      return;

    values_[index] = value;
    if (index == paramIndex(ParamId::appliedGainValue)) {
      history_[historyWrite_] = std::clamp(value, -15.0f, 15.0f);
      historyWrite_ = (historyWrite_ + 1) % history_.size();
      historySize_ = std::min(historySize_ + 1, history_.size());
    }
    repaint();
  }

  void stateChanged(const char *, const char *) override {}

  void onNanoDisplay() override {
    const float sx = static_cast<float>(getWidth()) / kDesignWidth;
    const float sy = static_cast<float>(getHeight()) / kDesignHeight;
    save();
    scale(sx, sy);

    drawChassis();
    drawHeader();
    drawTargetCard();
    drawResponseCard();

    restore();
  }

  bool onMouse(const MouseEvent &event) override {
    if (event.button != 1)
      return false;

    const float x = static_cast<float>(event.pos.getX()) * kDesignWidth /
                    static_cast<float>(getWidth());
    const float y = static_cast<float>(event.pos.getY()) * kDesignHeight /
                    static_cast<float>(getHeight());

    if (!event.press) {
      if (activeSlider_ != ParamId::count) {
        editParameter(paramIndex(activeSlider_), false);
        activeSlider_ = ParamId::count;
        return true;
      }
      if (resetPressed_) {
        setParameterValue(paramIndex(ParamId::meterReset), 0.0f);
        editParameter(paramIndex(ParamId::meterReset), false);
        resetPressed_ = false;
        repaint();
        return true;
      }
      return false;
    }

    for (const ParamId id : kSliderParameters) {
      const Bounds bounds = sliderBounds(id);
      if (!bounds.contains(x, y))
        continue;

      activeSlider_ = id;
      editParameter(paramIndex(id), true);
      if (id == ParamId::targetLevel) {
        updateSliderFromX(id, x);
      } else {
        dragStartY_ = y;
        dragStartValue_ = values_[paramIndex(id)];
      }
      return true;
    }

    for (std::size_t index = 0; index < kUiScales.size(); ++index) {
      if (scaleButtonBounds(index).contains(x, y))
        return setUiScale(index);
    }

    if (Bounds{342.0f, 96.0f, 82.0f, 33.0f}.contains(x, y))
      return setDiscreteParameter(ParamId::programMode, 0.0f);
    if (Bounds{429.0f, 96.0f, 85.0f, 33.0f}.contains(x, y))
      return setDiscreteParameter(ParamId::programMode, 1.0f);
    if (Bounds{585.0f, 96.0f, 86.0f, 33.0f}.contains(x, y))
      return setDiscreteParameter(ParamId::channelMode, 0.0f);
    if (Bounds{677.0f, 96.0f, 86.0f, 33.0f}.contains(x, y))
      return setDiscreteParameter(ParamId::channelMode, 1.0f);

    if (Bounds{67.0f, 384.0f, 218.0f, 38.0f}.contains(x, y)) {
      resetPressed_ = true;
      editParameter(paramIndex(ParamId::meterReset), true);
      setParameterValue(paramIndex(ParamId::meterReset), 1.0f);
      repaint();
      return true;
    }

    return false;
  }

  bool onMotion(const MotionEvent &event) override {
    if (activeSlider_ == ParamId::count)
      return false;

    const float x = static_cast<float>(event.pos.getX()) * kDesignWidth /
                    static_cast<float>(getWidth());
    if (activeSlider_ == ParamId::targetLevel) {
      updateSliderFromX(activeSlider_, x);
    } else {
      const float y = static_cast<float>(event.pos.getY()) * kDesignHeight /
                      static_cast<float>(getHeight());
      updateKnobFromY(activeSlider_, y);
    }
    return true;
  }

  bool onScroll(const ScrollEvent &event) override {
    const float x = static_cast<float>(event.pos.getX()) * kDesignWidth /
                    static_cast<float>(getWidth());
    const float y = static_cast<float>(event.pos.getY()) * kDesignHeight /
                    static_cast<float>(getHeight());

    for (const ParamId id : kSliderParameters) {
      if (!sliderBounds(id).contains(x, y))
        continue;

      const gainpilot::ParameterSpec &spec = gainpilot::parameterSpec(id);
      const float step = (spec.maxValue - spec.minValue) / 100.0f;
      const float next = gainpilot::clampToSpec(
          id, values_[paramIndex(id)] +
                  static_cast<float>(event.delta.getY()) * step);
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
  void drawChassis() {
    fillRect(0.0f, 0.0f, kDesignWidth, kDesignHeight, 6, 7, 7);

    // Layered edge creates thick, rounded, nickel-trimmed faceplate.
    fillRounded(2.0f, 2.0f, 836.0f, 466.0f, 22.0f, 18, 20, 20);
    strokeRounded(3.0f, 3.0f, 834.0f, 464.0f, 21.0f, 2.0f, 76, 79, 78);
    strokeRounded(7.0f, 7.0f, 826.0f, 456.0f, 18.0f, 1.0f, 174, 174, 166);
    strokeRounded(10.0f, 10.0f, 820.0f, 450.0f, 16.0f, 1.0f, 45, 47, 46);
    fillRounded(13.0f, 13.0f, 814.0f, 444.0f, 14.0f, 20, 23, 24);

    // Fine horizontal bands suggest brushed black steel without bitmap assets.
    for (int y = 15; y < 457; y += 3) {
      beginPath();
      moveTo(15.0f, static_cast<float>(y));
      lineTo(825.0f, static_cast<float>(y));
      strokeWidth(0.45f);
      const int shade = 24 + (y % 9 == 0 ? 4 : 0);
      strokeColor(shade, shade + 2, shade + 2);
      stroke();
    }

    drawScrew(28.0f, 29.0f);
    drawScrew(812.0f, 29.0f);
    drawScrew(28.0f, 443.0f);
    drawScrew(812.0f, 443.0f);
  }

  void drawHeader() {
    drawEmbossedText(57.0f, 33.0f, 30.0f, "GainPilot", 218, 218, 214,
                     ALIGN_LEFT | ALIGN_MIDDLE);
    drawText(58.0f, 56.0f, 11.5f,
             "Adaptive LUFS leveling  /  linked true-peak protection", 176, 178,
             176, ALIGN_LEFT | ALIGN_MIDDLE);
    drawText(783.0f, 25.0f, 10.5f, "BS.1770 / EBU R128", 30, 232, 231,
             ALIGN_RIGHT | ALIGN_MIDDLE);
    drawText(634.0f, 52.5f, 8.0f, "SCALE", 151, 153, 150,
             ALIGN_RIGHT | ALIGN_MIDDLE);
    for (std::size_t index = 0; index < kUiScales.size(); ++index)
      drawScaleButton(index);
  }

  void drawScaleButton(const std::size_t index) {
    const Bounds bounds = scaleButtonBounds(index);
    const bool selected = index == uiScaleIndex_;
    fillRounded(bounds.x, bounds.y, bounds.width, bounds.height, 3.0f,
                selected ? 10 : 20, selected ? 51 : 23, selected ? 53 : 23);
    strokeRounded(bounds.x, bounds.y, bounds.width, bounds.height, 3.0f, 0.8f,
                  selected ? 65 : 68, selected ? 211 : 68, selected ? 207 : 64);
    drawText(bounds.x + bounds.width * 0.5f, bounds.y + bounds.height * 0.5f,
             7.5f, kUiScaleLabels[index], selected ? 72 : 169,
             selected ? 232 : 170, selected ? 228 : 167,
             ALIGN_CENTER | ALIGN_MIDDLE);
  }

  void drawTargetCard() {
    drawPanel(45.0f, 68.0f, 262.0f, 374.0f, 14.0f);
    drawText(176.0f, 93.0f, 10.5f, "TARGET LUFS", 183, 183, 180,
             ALIGN_CENTER | ALIGN_MIDDLE);

    // Deep glass readout with stepped bezel.
    fillRounded(81.0f, 103.0f, 187.0f, 78.0f, 10.0f, 6, 7, 7);
    strokeRounded(81.0f, 103.0f, 187.0f, 78.0f, 10.0f, 1.4f, 109, 105, 94);
    strokeRounded(85.0f, 107.0f, 179.0f, 70.0f, 7.0f, 1.0f, 22, 24, 24);
    fillRounded(87.0f, 109.0f, 175.0f, 66.0f, 6.0f, 4, 14, 16);
    for (int y = 112; y < 174; y += 4)
      fillRect(89.0f, static_cast<float>(y), 171.0f, 1.0f, 5, 18, 20);

    char valueBuffer[64]{};
    std::snprintf(valueBuffer, sizeof(valueBuffer), "%.1f",
                  values_[paramIndex(ParamId::targetLevel)]);
    drawText(174.5f, 143.0f, 47.0f, valueBuffer, 43, 236, 235,
             ALIGN_CENTER | ALIGN_MIDDLE);

    drawSlider(ParamId::targetLevel);

    fillRounded(62.0f, 235.0f, 227.0f, 135.0f, 7.0f, 16, 18, 18);
    strokeRounded(62.0f, 235.0f, 227.0f, 135.0f, 7.0f, 1.0f, 67, 67, 63);
    drawReadout(75.0f, 251.0f, "Input", ParamId::inputIntegratedValue,
                "LUFS-I");
    drawReadout(75.0f, 278.0f, "Output", ParamId::outputIntegratedValue,
                "LUFS-I");
    drawReadout(75.0f, 305.0f, "Short-term", ParamId::outputShortTermValue,
                "LUFS");
    drawReadout(75.0f, 332.0f, "Gain reduction", ParamId::gainReductionValue,
                "dB");
    drawReadout(75.0f, 359.0f, "Applied gain", ParamId::appliedGainValue, "dB");

    for (int y = 263; y <= 344; y += 27) {
      beginPath();
      moveTo(63.0f, static_cast<float>(y));
      lineTo(288.0f, static_cast<float>(y));
      strokeWidth(0.7f);
      strokeColor(57, 58, 56);
      stroke();
    }

    drawResetButton();
  }

  void drawResponseCard() {
    drawPanel(315.0f, 67.0f, 476.0f, 376.0f, 13.0f);

    drawText(343.0f, 85.0f, 10.0f, "PROGRAM", 182, 183, 180,
             ALIGN_LEFT | ALIGN_MIDDLE);
    drawText(587.0f, 85.0f, 10.0f, "CHANNEL", 182, 183, 180,
             ALIGN_LEFT | ALIGN_MIDDLE);
    drawModeButton({342.0f, 96.0f, 82.0f, 33.0f}, "AUTO",
                   values_[paramIndex(ParamId::programMode)] < 0.5f);
    drawModeButton({429.0f, 96.0f, 85.0f, 33.0f}, "SPEECH",
                   values_[paramIndex(ParamId::programMode)] >= 0.5f);
    drawModeButton({585.0f, 96.0f, 86.0f, 33.0f}, "STEREO",
                   values_[paramIndex(ParamId::channelMode)] < 0.5f);
    drawModeButton({677.0f, 96.0f, 86.0f, 33.0f}, "MONO",
                   values_[paramIndex(ParamId::channelMode)] >= 0.5f);

    drawHistory();

    fillRounded(326.0f, 318.0f, 454.0f, 115.0f, 7.0f, 18, 20, 20);
    strokeRounded(326.0f, 318.0f, 454.0f, 115.0f, 7.0f, 1.0f, 58, 61, 59);
    drawSlider(ParamId::inputTrim);
    drawSlider(ParamId::truePeak);
    drawSlider(ParamId::maxGain);
    drawVerticalDivider(478.0f, 326.0f, 425.0f);
    drawVerticalDivider(626.0f, 326.0f, 425.0f);
  }

  void drawPanel(const float x, const float y, const float width,
                 const float height, const float radius) {
    fillRounded(x - 4.0f, y - 4.0f, width + 8.0f, height + 8.0f, radius + 3.0f,
                4, 5, 5);
    strokeRounded(x - 3.0f, y - 3.0f, width + 6.0f, height + 6.0f,
                  radius + 2.0f, 1.0f, 0, 0, 0);
    fillRounded(x, y, width, height, radius, 27, 29, 29);
    strokeRounded(x, y, width, height, radius, 1.2f, 122, 120, 111);
    strokeRounded(x + 4.0f, y + 4.0f, width - 8.0f, height - 8.0f,
                  std::max(2.0f, radius - 3.0f), 1.0f, 50, 52, 50);

    for (int line = static_cast<int>(y) + 7;
         line < static_cast<int>(y + height) - 5; line += 4) {
      beginPath();
      moveTo(x + 7.0f, static_cast<float>(line));
      lineTo(x + width - 7.0f, static_cast<float>(line));
      strokeWidth(0.4f);
      strokeColor(31, 33, 33);
      stroke();
    }
  }

  void drawModeButton(const Bounds &bounds, const char *const label,
                      const bool selected) {
    fillRounded(bounds.x - 4.0f, bounds.y - 4.0f, bounds.width + 8.0f,
                bounds.height + 8.0f, 7.0f, 5, 6, 6);
    strokeRounded(bounds.x - 3.0f, bounds.y - 3.0f, bounds.width + 6.0f,
                  bounds.height + 6.0f, 7.0f, 1.0f, 81, 78, 70);
    fillRounded(bounds.x, bounds.y, bounds.width, bounds.height, 4.0f,
                selected ? 10 : 24, selected ? 52 : 27, selected ? 54 : 27);
    strokeRounded(bounds.x, bounds.y, bounds.width, bounds.height, 4.0f,
                  selected ? 1.3f : 1.0f, selected ? 128 : 86,
                  selected ? 237 : 86, selected ? 231 : 81);
    if (selected)
      strokeRounded(bounds.x + 4.0f, bounds.y + 4.0f, bounds.width - 8.0f,
                    bounds.height - 8.0f, 2.0f, 1.0f, 43, 130, 130);

    drawText(bounds.x + bounds.width * 0.5f, bounds.y + bounds.height * 0.5f,
             10.5f, label, selected ? 89 : 176, selected ? 239 : 177,
             selected ? 236 : 174, ALIGN_CENTER | ALIGN_MIDDLE);
  }

  void drawSlider(const ParamId id) {
    const Bounds bounds = sliderBounds(id);
    const gainpilot::ParameterSpec &spec = gainpilot::parameterSpec(id);
    const float value = values_[paramIndex(id)];
    const float normalized = std::clamp(
        (value - spec.minValue) / (spec.maxValue - spec.minValue), 0.0f, 1.0f);

    char valueBuffer[48]{};
    if (id == ParamId::targetLevel)
      std::snprintf(valueBuffer, sizeof(valueBuffer), "%.1f LUFS", value);
    else
      std::snprintf(valueBuffer, sizeof(valueBuffer), "%+.1f dB", value);

    if (id != ParamId::targetLevel) {
      drawRotaryControl(id, bounds, normalized, valueBuffer);
      return;
    }

    drawText(bounds.x, bounds.y - 10.0f, 10.0f, shortLabel(id), 178, 179, 176,
             ALIGN_LEFT | ALIGN_MIDDLE);
    drawText(bounds.x + bounds.width, bounds.y - 10.0f, 10.5f, valueBuffer, 205,
             205, 201, ALIGN_RIGHT | ALIGN_MIDDLE);

    const float trackY = bounds.y + bounds.height * 0.5f - 3.5f;
    fillRounded(bounds.x - 2.0f, trackY - 2.0f, bounds.width + 4.0f, 11.0f,
                5.0f, 5, 6, 6);
    strokeRounded(bounds.x - 2.0f, trackY - 2.0f, bounds.width + 4.0f, 11.0f,
                  5.0f, 0.8f, 78, 76, 69);
    fillRounded(bounds.x, trackY, bounds.width, 7.0f, 3.5f, 8, 10, 10);

    const float activeWidth = std::max(1.0f, bounds.width * normalized);
    fillRounded(bounds.x, trackY + 1.0f, activeWidth, 5.0f, 2.5f, 19, 196, 193);
    fillRounded(bounds.x + 1.0f, trackY + 1.0f,
                std::max(0.0f, activeWidth - 2.0f), 2.0f, 1.0f, 61, 250, 245);
    drawKnob(bounds.x + bounds.width * normalized, trackY + 3.5f);
  }

  void drawRotaryControl(const ParamId id, const Bounds &bounds,
                         const float normalized, const char *const value) {
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float startAngle = -0.75f * kPi;
    constexpr float sweep = 1.5f * kPi;
    const float centerX = bounds.x + bounds.width * 0.5f;
    constexpr float centerY = 370.0f;
    const float valueAngle = startAngle + sweep * normalized;

    drawText(centerX, 332.0f, 9.5f, shortLabel(id), 180, 181, 178,
             ALIGN_CENTER | ALIGN_MIDDLE);

    for (int tick = 0; tick <= 20; ++tick) {
      const float amount = static_cast<float>(tick) / 20.0f;
      const float angle = startAngle + sweep * amount;
      const float cosine = std::cos(angle);
      const float sine = std::sin(angle);
      beginPath();
      moveTo(centerX + cosine * 27.0f, centerY + sine * 27.0f);
      lineTo(centerX + cosine * (tick % 5 == 0 ? 31.0f : 29.0f),
             centerY + sine * (tick % 5 == 0 ? 31.0f : 29.0f));
      strokeWidth(tick % 5 == 0 ? 1.2f : 0.7f);
      if (amount <= normalized)
        strokeColor(38, 222, 218);
      else
        strokeColor(70, 73, 70);
      stroke();
    }

    circleFill(centerX + 2.0f, centerY + 3.0f, 23.0f, 4, 5, 5);
    circleFill(centerX, centerY, 23.0f, 47, 48, 46);
    circleStroke(centerX, centerY, 23.0f, 1.2f, 142, 139, 129);
    circleFill(centerX, centerY, 19.5f, 133, 133, 128);
    circleFill(centerX - 4.0f, centerY - 4.0f, 14.0f, 194, 193, 185);
    circleStroke(centerX, centerY, 18.5f, 1.0f, 62, 62, 59);
    circleStroke(centerX, centerY, 14.5f, 0.8f, 218, 215, 205);

    beginPath();
    moveTo(centerX + std::cos(valueAngle) * 7.0f,
           centerY + std::sin(valueAngle) * 7.0f);
    lineTo(centerX + std::cos(valueAngle) * 18.0f,
           centerY + std::sin(valueAngle) * 18.0f);
    strokeWidth(2.3f);
    strokeColor(19, 27, 27);
    stroke();
    beginPath();
    moveTo(centerX + std::cos(valueAngle) * 10.0f,
           centerY + std::sin(valueAngle) * 10.0f);
    lineTo(centerX + std::cos(valueAngle) * 17.0f,
           centerY + std::sin(valueAngle) * 17.0f);
    strokeWidth(1.0f);
    strokeColor(66, 242, 237);
    stroke();

    drawKnobValue(centerX, 416.0f, value);
  }

  void drawKnob(const float x, const float y) {
    circleFill(x + 1.5f, y + 2.0f, 11.0f, 2, 3, 3);
    circleFill(x, y, 10.0f, 58, 58, 55);
    circleStroke(x, y, 10.0f, 1.0f, 163, 161, 151);
    circleFill(x, y, 8.0f, 174, 174, 168);
    circleFill(x - 2.0f, y - 2.0f, 5.7f, 217, 216, 208);
    circleStroke(x, y, 7.0f, 0.7f, 77, 77, 74);
    beginPath();
    moveTo(x, y - 7.0f);
    lineTo(x, y - 1.0f);
    strokeWidth(1.2f);
    strokeColor(31, 31, 30);
    stroke();
    beginPath();
    moveTo(x + 1.0f, y - 6.5f);
    lineTo(x + 1.0f, y - 1.0f);
    strokeWidth(0.7f);
    strokeColor(244, 241, 229);
    stroke();
  }

  void drawKnobValue(const float centerX, const float y,
                     const char *const value) {
    constexpr float width = 63.0f;
    fillRounded(centerX - width * 0.5f, y - 9.0f, width, 18.0f, 4.0f, 7, 10,
                10);
    strokeRounded(centerX - width * 0.5f, y - 9.0f, width, 18.0f, 4.0f, 0.8f,
                  43, 46, 44);
    drawText(centerX, y, 10.0f, value, 215, 218, 216,
             ALIGN_CENTER | ALIGN_MIDDLE);
  }

  void drawReadout(const float x, const float y, const char *const label,
                   const ParamId id, const char *const unit) {
    char valueBuffer[64]{};
    std::snprintf(valueBuffer, sizeof(valueBuffer), "%+.1f %s",
                  values_[paramIndex(id)], unit);
    drawText(x, y, 10.5f, label, 175, 176, 173, ALIGN_LEFT | ALIGN_MIDDLE);
    drawText(281.0f, y, 10.5f, valueBuffer, 210, 211, 208,
             ALIGN_RIGHT | ALIGN_MIDDLE);
  }

  void drawHistory() {
    constexpr Bounds graph{338.0f, 156.0f, 430.0f, 151.0f};
    drawText(graph.x + 4.0f, graph.y - 12.0f, 10.0f, "APPLIED GAIN HISTORY",
             180, 181, 178, ALIGN_LEFT | ALIGN_MIDDLE);

    char currentBuffer[48]{};
    std::snprintf(currentBuffer, sizeof(currentBuffer), "%+.1f dB",
                  values_[paramIndex(ParamId::appliedGainValue)]);
    drawText(graph.x + graph.width, graph.y - 12.0f, 10.5f, currentBuffer, 34,
             236, 232, ALIGN_RIGHT | ALIGN_MIDDLE);

    fillRounded(graph.x - 5.0f, graph.y - 4.0f, graph.width + 10.0f,
                graph.height + 9.0f, 8.0f, 5, 6, 6);
    strokeRounded(graph.x - 5.0f, graph.y - 4.0f, graph.width + 10.0f,
                  graph.height + 9.0f, 8.0f, 1.0f, 78, 78, 72);
    fillRounded(graph.x, graph.y, graph.width, graph.height, 4.0f, 4, 17, 19);

    constexpr float plotLeft = 23.0f;
    for (int gain = -15; gain <= 15; gain += 5) {
      const float y =
          graph.y + (15.0f - static_cast<float>(gain)) / 30.0f * graph.height;
      beginPath();
      moveTo(graph.x + plotLeft, y);
      lineTo(graph.x + graph.width, y);
      strokeWidth(gain == 0 ? 0.9f : 0.55f);
      strokeColor(gain == 0 ? 43 : 27, gain == 0 ? 68 : 48,
                  gain == 0 ? 68 : 49);
      stroke();

      char tick[8]{};
      if (gain > 0)
        std::snprintf(tick, sizeof(tick), "+%d", gain);
      else
        std::snprintf(tick, sizeof(tick), "%d", gain);
      drawText(graph.x + 17.0f, y, 8.0f, tick, 137, 139, 135,
               ALIGN_RIGHT | ALIGN_MIDDLE);
    }

    for (int column = 1; column < 16; ++column) {
      const float x =
          graph.x + plotLeft +
          static_cast<float>(column) / 16.0f * (graph.width - plotLeft);
      beginPath();
      moveTo(x, graph.y);
      lineTo(x, graph.y + graph.height);
      strokeWidth(0.45f);
      strokeColor(22, 45, 46);
      stroke();
    }

    drawText(graph.x + graph.width - 5.0f, graph.y + graph.height - 8.0f, 8.5f,
             "60 s", 151, 151, 146, ALIGN_RIGHT | ALIGN_MIDDLE);

    if (historySize_ < 2)
      return;

    beginPath();
    for (std::size_t i = 0; i < historySize_; ++i) {
      const std::size_t offset =
          (historyWrite_ + history_.size() - historySize_ + i) %
          history_.size();
      const float x = graph.x + plotLeft +
                      static_cast<float>(i) /
                          static_cast<float>(history_.size() - 1) *
                          (graph.width - plotLeft);
      const float y =
          graph.y + (15.0f - history_[offset]) / 30.0f * graph.height;
      if (i == 0)
        moveTo(x, y);
      else
        lineTo(x, y);
    }
    strokeWidth(4.0f);
    strokeColor(8, 60, 61);
    stroke();

    beginPath();
    for (std::size_t i = 0; i < historySize_; ++i) {
      const std::size_t offset =
          (historyWrite_ + history_.size() - historySize_ + i) %
          history_.size();
      const float x = graph.x + plotLeft +
                      static_cast<float>(i) /
                          static_cast<float>(history_.size() - 1) *
                          (graph.width - plotLeft);
      const float y =
          graph.y + (15.0f - history_[offset]) / 30.0f * graph.height;
      if (i == 0)
        moveTo(x, y);
      else
        lineTo(x, y);
    }
    strokeWidth(1.8f);
    strokeColor(45, 240, 236);
    stroke();
  }

  void drawResetButton() {
    constexpr Bounds button{67.0f, 384.0f, 218.0f, 38.0f};
    fillRounded(button.x - 5.0f, button.y - 5.0f, button.width + 10.0f,
                button.height + 10.0f, 8.0f, 5, 6, 6);
    fillRounded(button.x, button.y, button.width, button.height, 6.0f,
                resetPressed_ ? 15 : 15, resetPressed_ ? 62 : 24,
                resetPressed_ ? 62 : 25);
    strokeRounded(button.x, button.y, button.width, button.height, 6.0f, 1.2f,
                  38, 225, 220);
    strokeRounded(button.x + 4.0f, button.y + 4.0f, button.width - 8.0f,
                  button.height - 8.0f, 3.0f, 0.7f, 26, 75, 74);
    drawText(button.x + button.width * 0.5f, button.y + button.height * 0.5f,
             11.5f, "RESET / RELEARN", 48, 231, 228,
             ALIGN_CENTER | ALIGN_MIDDLE);
  }

  void drawScrew(const float x, const float y) {
    circleFill(x + 1.0f, y + 2.0f, 14.0f, 4, 5, 5);
    circleFill(x, y, 13.0f, 24, 25, 24);
    circleStroke(x, y, 13.0f, 1.0f, 111, 105, 92);
    circleFill(x - 2.0f, y - 2.0f, 9.0f, 36, 37, 35);
    circleStroke(x, y, 9.5f, 0.8f, 10, 11, 11);
    beginPath();
    moveTo(x - 5.0f, y);
    lineTo(x + 5.0f, y);
    moveTo(x, y - 5.0f);
    lineTo(x, y + 5.0f);
    strokeWidth(2.5f);
    strokeColor(4, 4, 4);
    stroke();
    beginPath();
    moveTo(x - 4.5f, y - 1.0f);
    lineTo(x + 4.5f, y - 1.0f);
    moveTo(x - 1.0f, y - 4.5f);
    lineTo(x - 1.0f, y + 4.5f);
    strokeWidth(0.8f);
    strokeColor(115, 109, 95);
    stroke();
  }

  void drawVerticalDivider(const float x, const float top, const float bottom) {
    beginPath();
    moveTo(x, top);
    lineTo(x, bottom);
    strokeWidth(0.7f);
    strokeColor(57, 59, 57);
    stroke();
  }

  void fillRect(const float x, const float y, const float width,
                const float height, const int red, const int green,
                const int blue) {
    beginPath();
    rect(x, y, width, height);
    fillColor(red, green, blue);
    fill();
  }

  void fillRounded(const float x, const float y, const float width,
                   const float height, const float radius, const int red,
                   const int green, const int blue) {
    beginPath();
    roundedRect(x, y, width, height, radius);
    fillColor(red, green, blue);
    fill();
  }

  void strokeRounded(const float x, const float y, const float width,
                     const float height, const float radius,
                     const float lineWidth, const int red, const int green,
                     const int blue) {
    beginPath();
    roundedRect(x, y, width, height, radius);
    strokeWidth(lineWidth);
    strokeColor(red, green, blue);
    stroke();
  }

  void circleFill(const float x, const float y, const float radius,
                  const int red, const int green, const int blue) {
    beginPath();
    circle(x, y, radius);
    fillColor(red, green, blue);
    fill();
  }

  void circleStroke(const float x, const float y, const float radius,
                    const float lineWidth, const int red, const int green,
                    const int blue) {
    beginPath();
    circle(x, y, radius);
    strokeWidth(lineWidth);
    strokeColor(red, green, blue);
    stroke();
  }

  void drawEmbossedText(const float x, const float y, const float size,
                        const char *const value, const int red, const int green,
                        const int blue, const int alignment) {
    drawText(x + 1.5f, y + 2.0f, size, value, 2, 3, 3, alignment);
    drawText(x - 0.5f, y - 0.5f, size, value, 104, 104, 100, alignment);
    drawText(x, y, size, value, red, green, blue, alignment);
  }

  void drawText(const float x, const float y, const float size,
                const char *const value, const int red, const int green,
                const int blue, const int alignment) {
    fontSize(size);
    textAlign(alignment);
    fillColor(red, green, blue);
    text(x, y, value, nullptr);
  }

  bool setDiscreteParameter(const ParamId id, const float value) {
    if constexpr (DISTRHO_PLUGIN_NUM_INPUTS == 1) {
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

  bool setUiScale(const std::size_t index) {
    if (index >= kUiScales.size())
      return false;

    uiScaleIndex_ = index;
    const float scaleFactor = kUiScales[index];
    setSize(
        static_cast<std::uint32_t>(std::lround(kDesignWidth * scaleFactor)),
        static_cast<std::uint32_t>(std::lround(kDesignHeight * scaleFactor)));
    repaint();
    return true;
  }

  void updateSliderFromX(const ParamId id, const float x) {
    const Bounds bounds = sliderBounds(id);
    const gainpilot::ParameterSpec &spec = gainpilot::parameterSpec(id);
    const float normalized =
        std::clamp((x - bounds.x) / bounds.width, 0.0f, 1.0f);
    const float value =
        spec.minValue + normalized * (spec.maxValue - spec.minValue);
    values_[paramIndex(id)] = value;
    setParameterValue(paramIndex(id), value);
    repaint();
  }

  void updateKnobFromY(const ParamId id, const float y) {
    const gainpilot::ParameterSpec &spec = gainpilot::parameterSpec(id);
    const float range = spec.maxValue - spec.minValue;
    const float value = gainpilot::clampToSpec(
        id, dragStartValue_ + (dragStartY_ - y) / 120.0f * range);
    values_[paramIndex(id)] = value;
    setParameterValue(paramIndex(id), value);
    repaint();
  }

  std::array<float, gainpilot::kNumParameters> values_{};
  std::array<float, 180> history_{};
  std::size_t historyWrite_{0};
  std::size_t historySize_{0};
  ParamId activeSlider_{ParamId::count};
  float dragStartY_{0.0f};
  float dragStartValue_{0.0f};
  std::size_t uiScaleIndex_{1};
  bool resetPressed_{false};

  DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GainPilotDPFUI)
};

UI *createUI() { return new GainPilotDPFUI(); }

END_NAMESPACE_DISTRHO
