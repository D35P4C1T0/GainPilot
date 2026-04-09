#include "mac_view.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <utility>

#import <AppKit/AppKit.h>

#include "pluginterfaces/base/fstrdefs.h"
#include "pluginterfaces/gui/iplugview.h"

namespace gainpilot::vst3 {
class GainPilotMacView;
}

@interface GainPilotMacViewTarget : NSObject {
 @public
  gainpilot::vst3::GainPilotMacView* owner;
}
- (void)sliderChanged:(NSSlider*)sender;
- (void)programModeChanged:(NSPopUpButton*)sender;
- (void)resetClicked:(NSButton*)sender;
- (void)tick:(NSTimer*)timer;
@end

@implementation GainPilotMacViewTarget
- (void)sliderChanged:(NSSlider*)sender {
  if (owner == nullptr) {
    return;
  }
  owner->handleSliderChanged(static_cast<gainpilot::ParamId>(sender.tag), static_cast<float>(sender.doubleValue));
}

- (void)programModeChanged:(NSPopUpButton*)sender {
  if (owner == nullptr) {
    return;
  }
  owner->handleSliderChanged(gainpilot::ParamId::programMode, static_cast<float>(sender.indexOfSelectedItem));
}

- (void)resetClicked:(NSButton*)sender {
  (void)sender;
  if (owner == nullptr) {
    return;
  }
  owner->handleResetClicked();
}

- (void)tick:(NSTimer*)timer {
  (void)timer;
  if (owner == nullptr) {
    return;
  }
  owner->refreshFromModel();
}
@end

namespace gainpilot::vst3 {

namespace {

const Steinberg::ViewRect kDefaultViewRect{0, 0, 720, 460};
constexpr std::array<ParamId, 4> kVisibleParams{
    ParamId::targetLevel,
    ParamId::truePeak,
    ParamId::maxGain,
    ParamId::inputTrim,
};

NSString* formatParamValue(ParamId id, float value) {
  switch (id) {
    case ParamId::targetLevel:
      return [NSString stringWithFormat:@"%.2f LUFS", value];
    case ParamId::truePeak:
    case ParamId::maxGain:
    case ParamId::inputTrim:
    case ParamId::gainReductionValue:
      return [NSString stringWithFormat:@"%.2f dB", value];
    case ParamId::meterValue:
      return [NSString stringWithFormat:@"In: %.2f LUFS-I", value];
    case ParamId::inputIntegratedValue:
      return [NSString stringWithFormat:@"Input: %.2f LUFS-I", value];
    case ParamId::outputIntegratedValue:
      return [NSString stringWithFormat:@"Output: %.2f LUFS-I", value];
    case ParamId::outputShortTermValue:
      return [NSString stringWithFormat:@"Short-Term: %.2f LUFS", value];
    case ParamId::programMode:
      return value >= 0.5f ? @"Speech" : @"Auto";
    default:
      return [NSString stringWithFormat:@"%.2f", value];
  }
}

NSString* formatLatency(float latencyMs) {
  return [NSString stringWithFormat:@"Latency: %.2f ms", latencyMs];
}

NSColor* color(double red, double green, double blue) {
  return [NSColor colorWithCalibratedRed:red green:green blue:blue alpha:1.0];
}

}  // namespace

struct GainPilotMacView::Impl {
  NSView* root{nil};
  NSTextField* headerLabel{nil};
  NSTextField* subtitleLabel{nil};
  NSBox* meterPanel{nil};
  NSTextField* meterTitleLabel{nil};
  NSTextField* meterValueLabel{nil};
  NSTextField* inputIntegratedLabel{nil};
  NSTextField* outputIntegratedLabel{nil};
  NSTextField* outputShortTermLabel{nil};
  NSTextField* gainReductionLabel{nil};
  NSLevelIndicator* meterLevel{nil};
  NSTextField* latencyLabel{nil};
  NSBox* controlsPanel{nil};
  NSTextField* controlsTitleLabel{nil};
  std::array<NSTextField*, kVisibleParams.size()> paramLabels{};
  NSPopUpButton* programModePopup{nil};
  NSTextField* programModeLabel{nil};
  NSButton* resetButton{nil};
  GainPilotMacViewTarget* target{nil};
  NSTimer* timer{nil};
  std::array<NSSlider*, kVisibleParams.size()> sliders{};
  std::array<NSTextField*, kVisibleParams.size()> valueLabels{};
};

GainPilotMacView::GainPilotMacView(MacViewCallbacks callbacks)
    : Steinberg::CPluginView(&kDefaultViewRect), callbacks_(std::move(callbacks)) {}

GainPilotMacView::~GainPilotMacView() {
  destroyUi();
}

Steinberg::tresult PLUGIN_API GainPilotMacView::isPlatformTypeSupported(Steinberg::FIDString type) {
  return Steinberg::FIDStringsEqual(type, Steinberg::kPlatformTypeNSView) ? Steinberg::kResultTrue
                                                                           : Steinberg::kResultFalse;
}

Steinberg::tresult PLUGIN_API GainPilotMacView::attached(void* parent, Steinberg::FIDString type) {
  if (isPlatformTypeSupported(type) != Steinberg::kResultTrue || parent == nullptr) {
    return Steinberg::kResultFalse;
  }

  auto* parentView = (__bridge NSView*)parent;
  if (parentView == nil) {
    return Steinberg::kResultFalse;
  }

  destroyUi();
  impl_ = new Impl{};
  impl_->target = [GainPilotMacViewTarget new];
  impl_->target->owner = this;

  const auto width = static_cast<CGFloat>(getRect().getWidth());
  const auto height = static_cast<CGFloat>(getRect().getHeight());

  impl_->root = [[NSView alloc] initWithFrame:NSMakeRect(0.0, 0.0, width, height)];
  impl_->root.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
  impl_->root.wantsLayer = YES;
  impl_->root.layer.backgroundColor = color(0.95, 0.93, 0.86).CGColor;

  impl_->headerLabel = [NSTextField labelWithString:@"GainPilot"];
  impl_->headerLabel.font = [NSFont boldSystemFontOfSize:22.0];
  impl_->headerLabel.textColor = color(0.18, 0.14, 0.10);
  [impl_->root addSubview:impl_->headerLabel];

  impl_->subtitleLabel =
      [NSTextField labelWithString:@"Target, trim, speech mode, and relearn with live input/output loudness feedback."];
  impl_->subtitleLabel.font = [NSFont systemFontOfSize:12.0];
  impl_->subtitleLabel.textColor = color(0.43, 0.37, 0.30);
  [impl_->root addSubview:impl_->subtitleLabel];

  impl_->meterPanel = [[NSBox alloc] initWithFrame:NSMakeRect(24.0, 24.0, 220.0, height - 110.0)];
  impl_->meterPanel.boxType = NSBoxCustom;
  impl_->meterPanel.borderWidth = 1.0;
  impl_->meterPanel.cornerRadius = 14.0;
  impl_->meterPanel.borderColor = color(0.83, 0.77, 0.67);
  impl_->meterPanel.fillColor = color(1.0, 0.98, 0.94);
  [impl_->root addSubview:impl_->meterPanel];

  impl_->meterTitleLabel = [NSTextField labelWithString:@"Gain Reduction"];
  impl_->meterTitleLabel.font = [NSFont boldSystemFontOfSize:14.0];
  impl_->meterTitleLabel.textColor = color(0.18, 0.14, 0.10);
  [impl_->meterPanel.contentView addSubview:impl_->meterTitleLabel];

  impl_->meterLevel =
      [[NSLevelIndicator alloc] initWithFrame:NSMakeRect(20.0, 74.0, impl_->meterPanel.frame.size.width - 40.0, 20.0)];
  impl_->meterLevel.minValue = 0.0;
  impl_->meterLevel.maxValue = 24.0;
  impl_->meterLevel.warningValue = 6.0;
  impl_->meterLevel.criticalValue = 12.0;
  impl_->meterLevel.levelIndicatorStyle = NSLevelIndicatorStyleContinuousCapacity;
  [impl_->meterPanel.contentView addSubview:impl_->meterLevel];

  impl_->gainReductionLabel = [NSTextField labelWithString:@"0.00 dB"];
  impl_->gainReductionLabel.font = [NSFont boldSystemFontOfSize:18.0];
  impl_->gainReductionLabel.textColor = color(0.77, 0.36, 0.12);
  impl_->gainReductionLabel.alignment = NSTextAlignmentCenter;
  [impl_->meterPanel.contentView addSubview:impl_->gainReductionLabel];

  impl_->meterValueLabel = [NSTextField labelWithString:@"In: -70.00 LUFS-I"];
  impl_->meterValueLabel.font = [NSFont systemFontOfSize:12.0];
  impl_->meterValueLabel.textColor = color(0.18, 0.14, 0.10);
  impl_->meterValueLabel.alignment = NSTextAlignmentCenter;
  [impl_->meterPanel.contentView addSubview:impl_->meterValueLabel];

  impl_->inputIntegratedLabel = [NSTextField labelWithString:@"Input: -70.00 LUFS-I"];
  impl_->inputIntegratedLabel.font = [NSFont systemFontOfSize:12.0];
  impl_->inputIntegratedLabel.textColor = color(0.18, 0.14, 0.10);
  impl_->inputIntegratedLabel.alignment = NSTextAlignmentCenter;
  [impl_->meterPanel.contentView addSubview:impl_->inputIntegratedLabel];

  impl_->outputIntegratedLabel = [NSTextField labelWithString:@"Output: -70.00 LUFS-I"];
  impl_->outputIntegratedLabel.font = [NSFont systemFontOfSize:12.0];
  impl_->outputIntegratedLabel.textColor = color(0.18, 0.14, 0.10);
  impl_->outputIntegratedLabel.alignment = NSTextAlignmentCenter;
  [impl_->meterPanel.contentView addSubview:impl_->outputIntegratedLabel];

  impl_->outputShortTermLabel = [NSTextField labelWithString:@"Short-Term: -70.00 LUFS"];
  impl_->outputShortTermLabel.font = [NSFont systemFontOfSize:12.0];
  impl_->outputShortTermLabel.textColor = color(0.18, 0.14, 0.10);
  impl_->outputShortTermLabel.alignment = NSTextAlignmentCenter;
  [impl_->meterPanel.contentView addSubview:impl_->outputShortTermLabel];

  impl_->latencyLabel = [NSTextField labelWithString:@"Latency: --"];
  impl_->latencyLabel.font = [NSFont systemFontOfSize:12.0];
  impl_->latencyLabel.textColor = color(0.43, 0.37, 0.30);
  impl_->latencyLabel.alignment = NSTextAlignmentCenter;
  [impl_->meterPanel.contentView addSubview:impl_->latencyLabel];

  impl_->controlsPanel = [[NSBox alloc] initWithFrame:NSMakeRect(260.0, 24.0, width - 284.0, height - 110.0)];
  impl_->controlsPanel.boxType = NSBoxCustom;
  impl_->controlsPanel.borderWidth = 1.0;
  impl_->controlsPanel.cornerRadius = 14.0;
  impl_->controlsPanel.borderColor = color(0.83, 0.77, 0.67);
  impl_->controlsPanel.fillColor = color(1.0, 0.98, 0.94);
  [impl_->root addSubview:impl_->controlsPanel];

  impl_->controlsTitleLabel = [NSTextField labelWithString:@"Controls"];
  impl_->controlsTitleLabel.font = [NSFont boldSystemFontOfSize:14.0];
  impl_->controlsTitleLabel.textColor = color(0.18, 0.14, 0.10);
  [impl_->controlsPanel.contentView addSubview:impl_->controlsTitleLabel];

  for (std::size_t index = 0; index < kVisibleParams.size(); ++index) {
    const auto param = kVisibleParams[index];
    const auto& spec = parameterSpec(param);

    auto* label = [NSTextField labelWithString:[NSString stringWithUTF8String:spec.name.data()]];
    label.font = [NSFont systemFontOfSize:13.0 weight:NSFontWeightSemibold];
    label.textColor = color(0.18, 0.14, 0.10);
    [impl_->controlsPanel.contentView addSubview:label];
    impl_->paramLabels[index] = label;

    auto* valueLabel = [NSTextField labelWithString:formatParamValue(param, spec.defaultValue)];
    valueLabel.font = [NSFont boldSystemFontOfSize:13.0];
    valueLabel.textColor = color(0.77, 0.36, 0.12);
    valueLabel.alignment = NSTextAlignmentRight;
    [impl_->controlsPanel.contentView addSubview:valueLabel];
    impl_->valueLabels[index] = valueLabel;

    auto* slider = [NSSlider sliderWithValue:spec.defaultValue
                                    minValue:spec.minValue
                                    maxValue:spec.maxValue
                                      target:impl_->target
                                      action:@selector(sliderChanged:)];
    slider.continuous = YES;
    slider.tag = static_cast<NSInteger>(param);
    [impl_->controlsPanel.contentView addSubview:slider];
    impl_->sliders[index] = slider;
  }

  impl_->programModeLabel = [NSTextField labelWithString:@"Program Mode"];
  impl_->programModeLabel.font = [NSFont systemFontOfSize:13.0 weight:NSFontWeightSemibold];
  impl_->programModeLabel.textColor = color(0.18, 0.14, 0.10);
  [impl_->controlsPanel.contentView addSubview:impl_->programModeLabel];

  impl_->programModePopup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(20.0, 20.0, 160.0, 28.0)];
  [impl_->programModePopup addItemsWithTitles:@[ @"Auto", @"Speech" ]];
  impl_->programModePopup.target = impl_->target;
  impl_->programModePopup.action = @selector(programModeChanged:);
  [impl_->controlsPanel.contentView addSubview:impl_->programModePopup];

  impl_->resetButton = [[NSButton alloc] initWithFrame:NSMakeRect(200.0, 20.0, 160.0, 32.0)];
  impl_->resetButton.bezelStyle = NSBezelStyleRounded;
  impl_->resetButton.title = @"Reset / Relearn";
  impl_->resetButton.target = impl_->target;
  impl_->resetButton.action = @selector(resetClicked:);
  [impl_->controlsPanel.contentView addSubview:impl_->resetButton];
  [impl_->programModePopup release];
  [impl_->resetButton release];
  [impl_->meterPanel release];
  [impl_->controlsPanel release];

  [parentView addSubview:impl_->root];
  impl_->timer = [NSTimer scheduledTimerWithTimeInterval:0.05
                                                  target:impl_->target
                                                selector:@selector(tick:)
                                                userInfo:nil
                                                 repeats:YES];
  layoutUi(width, height);

  systemWindow = parent;
  refreshFromModel();
  return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API GainPilotMacView::removed() {
  destroyUi();
  systemWindow = nullptr;
  return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API GainPilotMacView::onSize(Steinberg::ViewRect* newSize) {
  const auto result = Steinberg::CPluginView::onSize(newSize);
  if (impl_ != nullptr && impl_->root != nil && newSize != nullptr) {
    layoutUi(static_cast<float>(newSize->getWidth()), static_cast<float>(newSize->getHeight()));
  }
  return result;
}

void GainPilotMacView::handleSliderChanged(ParamId id, float value) {
  if (suppressCallbacks_ || !callbacks_.setParameterValue) {
    return;
  }

  if (impl_ != nullptr) {
    for (std::size_t index = 0; index < kVisibleParams.size(); ++index) {
      if (kVisibleParams[index] == id) {
        [impl_->valueLabels[index] setStringValue:formatParamValue(id, value)];
        break;
      }
    }
  }

  callbacks_.setParameterValue(id, value);
}

void GainPilotMacView::handleResetClicked() {
  if (!callbacks_.resetIntegrated) {
    return;
  }
  callbacks_.resetIntegrated();
}

void GainPilotMacView::refreshFromModel() {
  if (impl_ == nullptr) {
    return;
  }

  suppressCallbacks_ = true;
  for (std::size_t index = 0; index < kVisibleParams.size(); ++index) {
    const auto id = kVisibleParams[index];
    if (callbacks_.getParameterValue) {
      const float value = callbacks_.getParameterValue(id);
      [impl_->sliders[index] setDoubleValue:value];
      [impl_->valueLabels[index] setStringValue:formatParamValue(id, value)];
    }
  }

  if (callbacks_.getParameterValue) {
    [impl_->programModePopup selectItemAtIndex:static_cast<NSInteger>(callbacks_.getParameterValue(ParamId::programMode))];
    const float gainReduction = callbacks_.getParameterValue(ParamId::gainReductionValue);
    [impl_->meterLevel setDoubleValue:gainReduction];
    [impl_->gainReductionLabel setStringValue:formatParamValue(ParamId::gainReductionValue, gainReduction)];
    [impl_->meterValueLabel setStringValue:formatParamValue(ParamId::meterValue, callbacks_.getParameterValue(ParamId::meterValue))];
    [impl_->inputIntegratedLabel setStringValue:formatParamValue(
                                        ParamId::inputIntegratedValue,
                                        callbacks_.getParameterValue(ParamId::inputIntegratedValue))];
    [impl_->outputIntegratedLabel setStringValue:formatParamValue(
                                         ParamId::outputIntegratedValue,
                                         callbacks_.getParameterValue(ParamId::outputIntegratedValue))];
    [impl_->outputShortTermLabel setStringValue:formatParamValue(
                                        ParamId::outputShortTermValue,
                                        callbacks_.getParameterValue(ParamId::outputShortTermValue))];
  }

  if (callbacks_.getLatencyMilliseconds) {
    [impl_->latencyLabel setStringValue:formatLatency(callbacks_.getLatencyMilliseconds())];
  }
  suppressCallbacks_ = false;
}

void GainPilotMacView::layoutUi(float width, float height) {
  if (impl_ == nullptr || impl_->root == nil) {
    return;
  }

  const CGFloat viewWidth = std::max<CGFloat>(width, 1.0);
  const CGFloat viewHeight = std::max<CGFloat>(height, 1.0);
  const CGFloat outerMargin = 24.0;
  const CGFloat panelGap = 16.0;
  const CGFloat headerTop = 46.0;
  const CGFloat subtitleTop = 68.0;
  const CGFloat panelTopInset = 110.0;
  const bool stackedPanels = viewWidth < 680.0 || viewHeight < 430.0;

  impl_->root.frame = NSMakeRect(0.0, 0.0, viewWidth, viewHeight);
  impl_->headerLabel.frame = NSMakeRect(outerMargin, viewHeight - headerTop, 220.0, 28.0);
  impl_->subtitleLabel.frame = NSMakeRect(outerMargin, viewHeight - subtitleTop, viewWidth - outerMargin * 2.0, 18.0);

  const CGFloat availablePanelHeight = std::max<CGFloat>(viewHeight - panelTopInset, 180.0);
  if (stackedPanels) {
    CGFloat meterHeight = std::clamp(availablePanelHeight * 0.40, 90.0, 210.0);
    CGFloat controlsHeight = availablePanelHeight - meterHeight - panelGap;
    if (controlsHeight < 140.0) {
      meterHeight = std::max<CGFloat>(70.0, availablePanelHeight - panelGap - 140.0);
      controlsHeight = availablePanelHeight - meterHeight - panelGap;
    }
    impl_->meterPanel.frame =
        NSMakeRect(outerMargin, outerMargin + controlsHeight + panelGap, viewWidth - outerMargin * 2.0, meterHeight);
    impl_->controlsPanel.frame = NSMakeRect(outerMargin, outerMargin, viewWidth - outerMargin * 2.0, controlsHeight);
  } else {
    CGFloat meterWidth = std::clamp(viewWidth * 0.30, 190.0, 230.0);
    CGFloat controlsWidth = viewWidth - outerMargin * 2.0 - panelGap - meterWidth;
    if (controlsWidth < 300.0) {
      meterWidth = std::max<CGFloat>(160.0, viewWidth - outerMargin * 2.0 - panelGap - 300.0);
      controlsWidth = viewWidth - outerMargin * 2.0 - panelGap - meterWidth;
    }

    impl_->meterPanel.frame = NSMakeRect(outerMargin, outerMargin, meterWidth, availablePanelHeight);
    impl_->controlsPanel.frame =
        NSMakeRect(CGRectGetMaxX(impl_->meterPanel.frame) + panelGap, outerMargin, controlsWidth, availablePanelHeight);
  }

  const CGFloat meterInnerWidth = impl_->meterPanel.frame.size.width - 24.0;
  const CGFloat meterPanelHeight = impl_->meterPanel.frame.size.height;
  impl_->meterTitleLabel.frame = NSMakeRect(16.0, meterPanelHeight - 34.0, meterInnerWidth, 20.0);
  impl_->gainReductionLabel.frame = NSMakeRect(12.0, meterPanelHeight - 74.0, meterInnerWidth, 26.0);
  impl_->meterLevel.frame = NSMakeRect(20.0, meterPanelHeight - 108.0, impl_->meterPanel.frame.size.width - 40.0, 20.0);

  const CGFloat latencyY = 26.0;
  const CGFloat readoutBottomY = latencyY + 34.0;
  const CGFloat readoutStep = std::clamp((meterPanelHeight - 144.0 - readoutBottomY) / 3.0, 18.0, 24.0);
  const CGFloat readoutStartY = readoutBottomY + readoutStep * 3.0;
  impl_->meterValueLabel.frame = NSMakeRect(12.0, readoutStartY, meterInnerWidth, 18.0);
  impl_->inputIntegratedLabel.frame = NSMakeRect(12.0, readoutStartY - readoutStep, meterInnerWidth, 18.0);
  impl_->outputIntegratedLabel.frame = NSMakeRect(12.0, readoutStartY - readoutStep * 2.0, meterInnerWidth, 18.0);
  impl_->outputShortTermLabel.frame = NSMakeRect(12.0, readoutStartY - readoutStep * 3.0, meterInnerWidth, 18.0);
  impl_->latencyLabel.frame = NSMakeRect(12.0, latencyY, meterInnerWidth, 18.0);

  const CGFloat controlsPanelHeight = impl_->controlsPanel.frame.size.height;
  const CGFloat controlsPanelWidth = impl_->controlsPanel.frame.size.width;
  const CGFloat controlsInsetX = 20.0;
  const CGFloat controlsInnerWidth = controlsPanelWidth - controlsInsetX * 2.0;
  const CGFloat valueLabelWidth = std::min<CGFloat>(170.0, std::max<CGFloat>(120.0, controlsInnerWidth * 0.36));
  const CGFloat labelWidth = std::max<CGFloat>(140.0, controlsInnerWidth - valueLabelWidth - 12.0);
  const CGFloat modeControlsY = controlsInnerWidth >= 430.0 ? 24.0 : 60.0;
  const CGFloat rowPitch =
      std::clamp((controlsPanelHeight - 110.0 - modeControlsY) / static_cast<CGFloat>(kVisibleParams.size()), 34.0, 54.0);
  const CGFloat sliderHeight = 24.0;
  const CGFloat sliderValueGap = 30.0;

  impl_->controlsTitleLabel.frame = NSMakeRect(controlsInsetX, controlsPanelHeight - 34.0, 160.0, 20.0);

  for (std::size_t index = 0; index < kVisibleParams.size(); ++index) {
    const CGFloat rowY = controlsPanelHeight - 92.0 - static_cast<CGFloat>(index) * rowPitch;
    [impl_->paramLabels[index] setFrame:NSMakeRect(controlsInsetX, rowY + sliderValueGap, labelWidth, 18.0)];
    [impl_->valueLabels[index]
        setFrame:NSMakeRect(controlsPanelWidth - controlsInsetX - valueLabelWidth, rowY + sliderValueGap, valueLabelWidth, 18.0)];
    [impl_->sliders[index] setFrame:NSMakeRect(controlsInsetX, rowY, controlsInnerWidth, sliderHeight)];
  }

  const CGFloat modeRowY =
      std::max<CGFloat>(modeControlsY, controlsPanelHeight - 92.0 - rowPitch * static_cast<CGFloat>(kVisibleParams.size()) - 18.0);
  impl_->programModeLabel.frame = NSMakeRect(controlsInsetX, modeRowY + 34.0, 160.0, 18.0);

  if (controlsInnerWidth >= 430.0) {
    const CGFloat popupWidth = std::min<CGFloat>(180.0, controlsInnerWidth * 0.42);
    impl_->programModePopup.frame = NSMakeRect(controlsInsetX, modeRowY, popupWidth, 28.0);
    impl_->resetButton.frame =
        NSMakeRect(controlsPanelWidth - controlsInsetX - 160.0, modeRowY - 2.0, 160.0, 32.0);
  } else {
    impl_->programModePopup.frame = NSMakeRect(controlsInsetX, modeRowY, controlsInnerWidth, 28.0);
    impl_->resetButton.frame = NSMakeRect(controlsInsetX, modeRowY - 40.0, controlsInnerWidth, 32.0);
  }
}

void GainPilotMacView::destroyUi() {
  if (impl_ == nullptr) {
    return;
  }

  if (impl_->timer != nil) {
    [impl_->timer invalidate];
    impl_->timer = nil;
  }
  if (impl_->root != nil) {
    [impl_->root removeFromSuperview];
    [impl_->root release];
    impl_->root = nil;
  }
  if (impl_->target != nil) {
    impl_->target->owner = nullptr;
    [impl_->target release];
    impl_->target = nil;
  }
  delete impl_;
  impl_ = nullptr;
}

}  // namespace gainpilot::vst3
