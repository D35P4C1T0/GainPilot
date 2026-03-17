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
- (void)tick:(NSTimer*)timer;
@end

@implementation GainPilotMacViewTarget
- (void)sliderChanged:(NSSlider*)sender {
  if (owner == nullptr) {
    return;
  }
  owner->handleSliderChanged(static_cast<gainpilot::ParamId>(sender.tag), static_cast<float>(sender.doubleValue));
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

const Steinberg::ViewRect kDefaultViewRect{0, 0, 640, 420};
constexpr std::array<ParamId, 3> kVisibleParams{
    ParamId::targetLevel,
    ParamId::truePeak,
    ParamId::maxGain,
};

NSString* formatParamValue(ParamId id, float value) {
  switch (id) {
    case ParamId::targetLevel:
      return [NSString stringWithFormat:@"%.2f LUFS", value];
    case ParamId::truePeak:
    case ParamId::maxGain:
      return [NSString stringWithFormat:@"%.2f dB", value];
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
  NSTextField* meterValueLabel{nil};
  NSLevelIndicator* meterLevel{nil};
  NSTextField* latencyLabel{nil};
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

  auto* header = [NSTextField labelWithString:@"GainPilot"];
  header.font = [NSFont boldSystemFontOfSize:22.0];
  header.textColor = color(0.18, 0.14, 0.10);
  header.frame = NSMakeRect(24.0, height - 46.0, 220.0, 28.0);
  [impl_->root addSubview:header];

  auto* subtitle = [NSTextField labelWithString:@"Set a target, true peak, and max gain. Input loudness is learned automatically."];
  subtitle.font = [NSFont systemFontOfSize:12.0];
  subtitle.textColor = color(0.43, 0.37, 0.30);
  subtitle.frame = NSMakeRect(24.0, height - 68.0, width - 48.0, 18.0);
  [impl_->root addSubview:subtitle];

  auto* meterPanel = [[NSBox alloc] initWithFrame:NSMakeRect(24.0, 24.0, 160.0, height - 110.0)];
  meterPanel.boxType = NSBoxCustom;
  meterPanel.borderWidth = 1.0;
  meterPanel.cornerRadius = 14.0;
  meterPanel.borderColor = color(0.83, 0.77, 0.67);
  meterPanel.fillColor = color(1.0, 0.98, 0.94);
  [impl_->root addSubview:meterPanel];

  auto* meterTitle = [NSTextField labelWithString:@"Integrated Loudness"];
  meterTitle.font = [NSFont boldSystemFontOfSize:14.0];
  meterTitle.textColor = color(0.18, 0.14, 0.10);
  meterTitle.frame = NSMakeRect(16.0, meterPanel.frame.size.height - 34.0, 128.0, 20.0);
  [meterPanel.contentView addSubview:meterTitle];

  impl_->meterLevel =
      [[NSLevelIndicator alloc] initWithFrame:NSMakeRect(20.0, 64.0, meterPanel.frame.size.width - 40.0, 20.0)];
  impl_->meterLevel.minValue = -70.0;
  impl_->meterLevel.maxValue = 10.0;
  impl_->meterLevel.warningValue = -18.0;
  impl_->meterLevel.criticalValue = -10.0;
  impl_->meterLevel.levelIndicatorStyle = NSLevelIndicatorStyleContinuousCapacity;
  [meterPanel.contentView addSubview:impl_->meterLevel];

  impl_->meterValueLabel = [NSTextField labelWithString:@"-70.00 LUFS"];
  impl_->meterValueLabel.font = [NSFont boldSystemFontOfSize:18.0];
  impl_->meterValueLabel.textColor = color(0.77, 0.36, 0.12);
  impl_->meterValueLabel.alignment = NSTextAlignmentCenter;
  impl_->meterValueLabel.frame = NSMakeRect(12.0, 94.0, meterPanel.frame.size.width - 24.0, 26.0);
  [meterPanel.contentView addSubview:impl_->meterValueLabel];

  impl_->latencyLabel = [NSTextField labelWithString:@"Latency: --"];
  impl_->latencyLabel.font = [NSFont systemFontOfSize:12.0];
  impl_->latencyLabel.textColor = color(0.43, 0.37, 0.30);
  impl_->latencyLabel.alignment = NSTextAlignmentCenter;
  impl_->latencyLabel.frame = NSMakeRect(12.0, 26.0, meterPanel.frame.size.width - 24.0, 18.0);
  [meterPanel.contentView addSubview:impl_->latencyLabel];

  auto* controlsPanel = [[NSBox alloc] initWithFrame:NSMakeRect(200.0, 24.0, width - 224.0, height - 110.0)];
  controlsPanel.boxType = NSBoxCustom;
  controlsPanel.borderWidth = 1.0;
  controlsPanel.cornerRadius = 14.0;
  controlsPanel.borderColor = color(0.83, 0.77, 0.67);
  controlsPanel.fillColor = color(1.0, 0.98, 0.94);
  [impl_->root addSubview:controlsPanel];

  auto* controlsTitle = [NSTextField labelWithString:@"Controls"];
  controlsTitle.font = [NSFont boldSystemFontOfSize:14.0];
  controlsTitle.textColor = color(0.18, 0.14, 0.10);
  controlsTitle.frame = NSMakeRect(20.0, controlsPanel.frame.size.height - 34.0, 160.0, 20.0);
  [controlsPanel.contentView addSubview:controlsTitle];

  CGFloat rowTop = controlsPanel.frame.size.height - 92.0;
  for (std::size_t index = 0; index < kVisibleParams.size(); ++index) {
    const auto param = kVisibleParams[index];
    const auto& spec = parameterSpec(param);
    const CGFloat rowY = rowTop - static_cast<CGFloat>(index) * 92.0;

    auto* label = [NSTextField labelWithString:[NSString stringWithUTF8String:spec.name.data()]];
    label.font = [NSFont systemFontOfSize:13.0 weight:NSFontWeightSemibold];
    label.textColor = color(0.18, 0.14, 0.10);
    label.frame = NSMakeRect(20.0, rowY + 30.0, 160.0, 18.0);
    [controlsPanel.contentView addSubview:label];

    auto* valueLabel = [NSTextField labelWithString:formatParamValue(param, spec.defaultValue)];
    valueLabel.font = [NSFont boldSystemFontOfSize:13.0];
    valueLabel.textColor = color(0.77, 0.36, 0.12);
    valueLabel.alignment = NSTextAlignmentRight;
    valueLabel.frame = NSMakeRect(controlsPanel.frame.size.width - 180.0, rowY + 30.0, 160.0, 18.0);
    [controlsPanel.contentView addSubview:valueLabel];
    impl_->valueLabels[index] = valueLabel;

    auto* slider = [NSSlider sliderWithValue:spec.defaultValue
                                    minValue:spec.minValue
                                    maxValue:spec.maxValue
                                      target:impl_->target
                                      action:@selector(sliderChanged:)];
    slider.continuous = YES;
    slider.tag = static_cast<NSInteger>(param);
    slider.frame = NSMakeRect(20.0, rowY, controlsPanel.frame.size.width - 40.0, 24.0);
    [controlsPanel.contentView addSubview:slider];
    impl_->sliders[index] = slider;
  }

  [parentView addSubview:impl_->root];
  impl_->timer = [NSTimer scheduledTimerWithTimeInterval:0.05
                                                  target:impl_->target
                                                selector:@selector(tick:)
                                                userInfo:nil
                                                 repeats:YES];
  [meterPanel release];
  [controlsPanel release];

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
    impl_->root.frame = NSMakeRect(0.0, 0.0, newSize->getWidth(), newSize->getHeight());
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

  if (callbacks_.getMeterValue) {
    const float meterValue = callbacks_.getMeterValue();
    [impl_->meterLevel setDoubleValue:meterValue];
    [impl_->meterValueLabel setStringValue:formatParamValue(ParamId::meterValue, meterValue)];
  }

  if (callbacks_.getLatencyMilliseconds) {
    [impl_->latencyLabel setStringValue:formatLatency(callbacks_.getLatencyMilliseconds())];
  }
  suppressCallbacks_ = false;
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
