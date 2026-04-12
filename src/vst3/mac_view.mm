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

const Steinberg::ViewRect kDefaultViewRect{0, 0, 800, 470};
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

NSColor* canvasColor() {
  return color(0.16, 0.17, 0.20);
}

NSColor* panelColor() {
  return color(0.13, 0.15, 0.17);
}

NSColor* borderColor() {
  return color(0.24, 0.27, 0.32);
}

NSColor* titleColor() {
  return color(0.90, 0.75, 0.48);
}

NSColor* textColor() {
  return color(0.67, 0.70, 0.75);
}

NSColor* subtleColor() {
  return color(0.36, 0.39, 0.44);
}

NSColor* accentColor() {
  return color(0.82, 0.60, 0.40);
}

NSColor* badgeColor() {
  return color(0.38, 0.69, 0.94);
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
  impl_->root.layer.backgroundColor = canvasColor().CGColor;

  impl_->headerLabel = [NSTextField labelWithString:@"GainPilot"];
  impl_->headerLabel.font = [NSFont boldSystemFontOfSize:22.0];
  impl_->headerLabel.textColor = titleColor();
  [impl_->root addSubview:impl_->headerLabel];

  impl_->subtitleLabel =
      [NSTextField labelWithString:@"Trim, speech mode, and live input/output loudness feedback."];
  impl_->subtitleLabel.font = [NSFont systemFontOfSize:12.0];
  impl_->subtitleLabel.textColor = subtleColor();
  [impl_->root addSubview:impl_->subtitleLabel];

  impl_->meterPanel = [[NSBox alloc] initWithFrame:NSMakeRect(24.0, 24.0, 220.0, height - 110.0)];
  impl_->meterPanel.boxType = NSBoxCustom;
  impl_->meterPanel.borderWidth = 1.0;
  impl_->meterPanel.cornerRadius = 14.0;
  impl_->meterPanel.borderColor = borderColor();
  impl_->meterPanel.fillColor = panelColor();
  [impl_->root addSubview:impl_->meterPanel];

  impl_->meterTitleLabel = [NSTextField labelWithString:@"Gain Reduction"];
  impl_->meterTitleLabel.font = [NSFont boldSystemFontOfSize:14.0];
  impl_->meterTitleLabel.textColor = titleColor();
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
  impl_->gainReductionLabel.textColor = accentColor();
  impl_->gainReductionLabel.alignment = NSTextAlignmentCenter;
  [impl_->meterPanel.contentView addSubview:impl_->gainReductionLabel];

  impl_->meterValueLabel = [NSTextField labelWithString:@"In: -70.00 LUFS-I"];
  impl_->meterValueLabel.font = [NSFont systemFontOfSize:12.0];
  impl_->meterValueLabel.textColor = textColor();
  impl_->meterValueLabel.alignment = NSTextAlignmentCenter;
  [impl_->meterPanel.contentView addSubview:impl_->meterValueLabel];

  impl_->inputIntegratedLabel = [NSTextField labelWithString:@"Input: -70.00 LUFS-I"];
  impl_->inputIntegratedLabel.font = [NSFont systemFontOfSize:12.0];
  impl_->inputIntegratedLabel.textColor = textColor();
  impl_->inputIntegratedLabel.alignment = NSTextAlignmentCenter;
  [impl_->meterPanel.contentView addSubview:impl_->inputIntegratedLabel];

  impl_->outputIntegratedLabel = [NSTextField labelWithString:@"Output: -70.00 LUFS-I"];
  impl_->outputIntegratedLabel.font = [NSFont systemFontOfSize:12.0];
  impl_->outputIntegratedLabel.textColor = textColor();
  impl_->outputIntegratedLabel.alignment = NSTextAlignmentCenter;
  [impl_->meterPanel.contentView addSubview:impl_->outputIntegratedLabel];

  impl_->outputShortTermLabel = [NSTextField labelWithString:@"Short-Term: -70.00 LUFS"];
  impl_->outputShortTermLabel.font = [NSFont systemFontOfSize:12.0];
  impl_->outputShortTermLabel.textColor = textColor();
  impl_->outputShortTermLabel.alignment = NSTextAlignmentCenter;
  [impl_->meterPanel.contentView addSubview:impl_->outputShortTermLabel];

  impl_->latencyLabel = [NSTextField labelWithString:@"Latency: --"];
  impl_->latencyLabel.font = [NSFont systemFontOfSize:12.0];
  impl_->latencyLabel.textColor = subtleColor();
  impl_->latencyLabel.alignment = NSTextAlignmentCenter;
  [impl_->meterPanel.contentView addSubview:impl_->latencyLabel];

  impl_->controlsPanel = [[NSBox alloc] initWithFrame:NSMakeRect(260.0, 24.0, width - 284.0, height - 110.0)];
  impl_->controlsPanel.boxType = NSBoxCustom;
  impl_->controlsPanel.borderWidth = 1.0;
  impl_->controlsPanel.cornerRadius = 14.0;
  impl_->controlsPanel.borderColor = borderColor();
  impl_->controlsPanel.fillColor = panelColor();
  [impl_->root addSubview:impl_->controlsPanel];

  impl_->controlsTitleLabel = [NSTextField labelWithString:@"Controls"];
  impl_->controlsTitleLabel.font = [NSFont boldSystemFontOfSize:14.0];
  impl_->controlsTitleLabel.textColor = titleColor();
  [impl_->controlsPanel.contentView addSubview:impl_->controlsTitleLabel];

  for (std::size_t index = 0; index < kVisibleParams.size(); ++index) {
    const auto param = kVisibleParams[index];
    const auto& spec = parameterSpec(param);

    auto* label = [NSTextField labelWithString:[NSString stringWithUTF8String:spec.name.data()]];
    label.font = [NSFont systemFontOfSize:13.0 weight:NSFontWeightSemibold];
    label.textColor = textColor();
    [impl_->controlsPanel.contentView addSubview:label];
    impl_->paramLabels[index] = label;

    auto* valueLabel = [NSTextField labelWithString:formatParamValue(param, spec.defaultValue)];
    valueLabel.font = [NSFont boldSystemFontOfSize:13.0];
    valueLabel.textColor = accentColor();
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
  impl_->programModeLabel.textColor = textColor();
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
  if (newSize == nullptr) {
    return Steinberg::kResultFalse;
  }

  const auto result = Steinberg::CPluginView::onSize(newSize);
  if (impl_ != nullptr && impl_->root != nil) {
    layoutUi(static_cast<float>(newSize->getWidth()), static_cast<float>(newSize->getHeight()));
  }
  return result;
}

Steinberg::tresult PLUGIN_API GainPilotMacView::canResize() {
  return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API GainPilotMacView::checkSizeConstraint(Steinberg::ViewRect* rect) {
  return rect != nullptr ? Steinberg::kResultTrue : Steinberg::kResultFalse;
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

  constexpr CGFloat kBaseWidth = 800.0;
  constexpr CGFloat kBaseHeight = 470.0;
  constexpr CGFloat kViewportInset = 6.0;
  const CGFloat viewWidth = std::max<CGFloat>(width, 1.0);
  const CGFloat viewHeight = std::max<CGFloat>(height, 1.0);
  const CGFloat availableWidth = std::max<CGFloat>(viewWidth - kViewportInset * 2.0, 1.0);
  const CGFloat availableHeight = std::max<CGFloat>(viewHeight - kViewportInset * 2.0, 1.0);
  const CGFloat scale = std::min(availableWidth / kBaseWidth, availableHeight / kBaseHeight);
  const CGFloat contentWidth = kBaseWidth * scale;
  const CGFloat contentHeight = kBaseHeight * scale;
  const CGFloat offsetX = std::floor((viewWidth - contentWidth) * 0.5);
  const CGFloat offsetY = std::floor((viewHeight - contentHeight) * 0.5);

  auto rootRect = [scale, offsetX, offsetY](CGFloat x, CGFloat y, CGFloat w, CGFloat h) {
    return NSMakeRect(offsetX + x * scale, offsetY + y * scale, w * scale, h * scale);
  };
  auto localRect = [scale](CGFloat x, CGFloat y, CGFloat w, CGFloat h) {
    return NSMakeRect(x * scale, y * scale, w * scale, h * scale);
  };

  auto scaledFont = [scale](CGFloat size, bool bold = false) {
    return bold ? [NSFont boldSystemFontOfSize:size * scale] : [NSFont systemFontOfSize:size * scale];
  };

  impl_->root.frame = NSMakeRect(0.0, 0.0, viewWidth, viewHeight);

  impl_->headerLabel.font = scaledFont(21.0, true);
  impl_->subtitleLabel.font = scaledFont(11.0);
  impl_->meterTitleLabel.font = scaledFont(14.0, true);
  impl_->gainReductionLabel.font = scaledFont(17.0, true);
  impl_->meterValueLabel.font = scaledFont(11.0);
  impl_->inputIntegratedLabel.font = scaledFont(11.0);
  impl_->outputIntegratedLabel.font = scaledFont(11.0);
  impl_->outputShortTermLabel.font = scaledFont(11.0);
  impl_->latencyLabel.font = scaledFont(11.0);
  impl_->controlsTitleLabel.font = scaledFont(14.0, true);
  impl_->programModeLabel.font = [NSFont systemFontOfSize:12.0 * scale weight:NSFontWeightSemibold];

  impl_->headerLabel.frame = rootRect(18.0, 428.0, 220.0, 26.0);
  impl_->subtitleLabel.frame = rootRect(18.0, 408.0, 548.0, 16.0);

  impl_->meterPanel.frame = rootRect(18.0, 18.0, 196.0, 356.0);
  impl_->controlsPanel.frame = rootRect(228.0, 18.0, 554.0, 356.0);

  impl_->meterTitleLabel.frame = localRect(12.0, 324.0, 172.0, 18.0);
  impl_->gainReductionLabel.frame = localRect(8.0, 288.0, 180.0, 22.0);
  impl_->meterLevel.frame = localRect(16.0, 258.0, 164.0, 16.0);
  impl_->meterValueLabel.frame = localRect(8.0, 118.0, 180.0, 16.0);
  impl_->inputIntegratedLabel.frame = localRect(8.0, 98.0, 180.0, 16.0);
  impl_->outputIntegratedLabel.frame = localRect(8.0, 78.0, 180.0, 16.0);
  impl_->outputShortTermLabel.frame = localRect(8.0, 58.0, 180.0, 16.0);
  impl_->latencyLabel.frame = localRect(8.0, 22.0, 180.0, 16.0);

  impl_->controlsTitleLabel.frame = localRect(16.0, 330.0, 150.0, 18.0);

  constexpr std::array<CGFloat, 4> kRowY{268.0, 222.0, 176.0, 130.0};
  for (std::size_t index = 0; index < kVisibleParams.size(); ++index) {
    const CGFloat rowY = kRowY[index];
    impl_->paramLabels[index].font = [NSFont systemFontOfSize:12.0 * scale weight:NSFontWeightSemibold];
    impl_->valueLabels[index].font = scaledFont(12.0, true);
    [impl_->paramLabels[index] setFrame:localRect(16.0, rowY + 24.0, 260.0, 16.0)];
    [impl_->valueLabels[index] setFrame:localRect(384.0, rowY + 24.0, 102.0, 16.0)];
    [impl_->sliders[index] setFrame:localRect(16.0, rowY, 418.0, 20.0)];
  }

  impl_->programModeLabel.frame = localRect(16.0, 70.0, 150.0, 16.0);
  impl_->programModePopup.frame = localRect(16.0, 40.0, 166.0, 24.0);
  impl_->resetButton.frame = localRect(296.0, 38.0, 146.0, 28.0);
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
