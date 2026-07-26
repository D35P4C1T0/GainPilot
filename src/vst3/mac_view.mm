#include "mac_view.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <utility>

#import <AppKit/AppKit.h>

#include "pluginterfaces/base/fstrdefs.h"
#include "pluginterfaces/gui/iplugview.h"

@interface GainPilotVerticalSlider : NSSlider
@end

@implementation GainPilotVerticalSlider
- (void)updateValueForEvent:(NSEvent*)event {
  const NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
  const CGFloat knobDiameter =
      std::max<CGFloat>(12.0, std::min<CGFloat>(22.0, self.bounds.size.width - 6.0));
  const CGFloat trackStart = knobDiameter * 0.5 + 2.0;
  const CGFloat trackEnd = self.bounds.size.height - knobDiameter * 0.5 - 2.0;
  const CGFloat trackLength = std::max<CGFloat>(1.0, trackEnd - trackStart);
  const CGFloat normalized =
      self.isFlipped
          ? std::clamp<CGFloat>((trackEnd - point.y) / trackLength, 0.0, 1.0)
          : std::clamp<CGFloat>((point.y - trackStart) / trackLength, 0.0, 1.0);
  self.doubleValue = self.minValue + normalized * (self.maxValue - self.minValue);
  [self sendAction:self.action to:self.target];
  [self setNeedsDisplay:YES];
}

- (void)mouseDown:(NSEvent*)event {
  [self updateValueForEvent:event];
  while (true) {
    NSEvent* nextEvent =
        [self.window nextEventMatchingMask:NSEventMaskLeftMouseDragged | NSEventMaskLeftMouseUp];
    if (nextEvent == nil || nextEvent.type == NSEventTypeLeftMouseUp) {
      break;
    }
    [self updateValueForEvent:nextEvent];
  }
}

- (void)drawRect:(NSRect)dirtyRect {
  (void)dirtyRect;
  const NSRect bounds = self.bounds;
  const CGFloat centerX = NSMidX(bounds);
  const CGFloat knobDiameter = std::max<CGFloat>(12.0, std::min<CGFloat>(22.0, bounds.size.width - 6.0));
  const CGFloat knobRadius = knobDiameter * 0.5;
  const CGFloat trackStart = knobRadius + 2.0;
  const CGFloat trackEnd = bounds.size.height - knobRadius - 2.0;
  const CGFloat trackHeight = std::max<CGFloat>(1.0, trackEnd - trackStart);
  const CGFloat range = std::max<CGFloat>(self.maxValue - self.minValue, 1.0e-9);
  const CGFloat normalized =
      std::clamp<CGFloat>((self.doubleValue - self.minValue) / range, 0.0, 1.0);
  const CGFloat knobY =
      self.isFlipped ? trackEnd - normalized * trackHeight
                     : trackStart + normalized * trackHeight;
  const CGFloat trackWidth = std::max<CGFloat>(3.0, bounds.size.width * 0.11);

  NSBezierPath* backgroundTrack =
      [NSBezierPath bezierPathWithRoundedRect:NSMakeRect(centerX - trackWidth * 0.5,
                                                        trackStart,
                                                        trackWidth,
                                                        trackHeight)
                                      xRadius:trackWidth * 0.5
                                      yRadius:trackWidth * 0.5];
  [[NSColor colorWithCalibratedWhite:0.055 alpha:1.0] setFill];
  [backgroundTrack fill];

  const CGFloat activeY = self.isFlipped ? knobY : trackStart;
  const CGFloat activeHeight =
      self.isFlipped ? trackEnd - knobY : knobY - trackStart;
  NSBezierPath* activeTrack =
      [NSBezierPath bezierPathWithRoundedRect:NSMakeRect(centerX - trackWidth * 0.5,
                                                        activeY,
                                                        trackWidth,
                                                        std::max<CGFloat>(trackWidth, activeHeight))
                                      xRadius:trackWidth * 0.5
                                      yRadius:trackWidth * 0.5];
  [[NSColor colorWithCalibratedRed:0.37 green:0.70 blue:0.66 alpha:0.95] setFill];
  [activeTrack fill];

  const NSRect knobRect =
      NSMakeRect(centerX - knobRadius, knobY - knobRadius, knobDiameter, knobDiameter);
  NSBezierPath* knob = [NSBezierPath bezierPathWithOvalInRect:knobRect];
  [[NSColor colorWithCalibratedWhite:0.15 alpha:1.0] setFill];
  [knob fill];
  [[NSColor colorWithCalibratedRed:0.37 green:0.70 blue:0.66 alpha:1.0] setStroke];
  knob.lineWidth = 1.5;
  [knob stroke];

  NSBezierPath* indicator = [NSBezierPath bezierPath];
  [indicator moveToPoint:NSMakePoint(centerX - 4.0, knobY)];
  [indicator lineToPoint:NSMakePoint(centerX + 4.0, knobY)];
  [[NSColor colorWithCalibratedWhite:0.88 alpha:1.0] setStroke];
  indicator.lineWidth = 1.4;
  [indicator stroke];
}
@end

@interface GainPilotHorizontalSlider : NSSlider
@end

@implementation GainPilotHorizontalSlider
- (void)drawRect:(NSRect)dirtyRect {
  (void)dirtyRect;
  const NSRect bounds = self.bounds;
  const CGFloat centerY = NSMidY(bounds);
  const CGFloat knobDiameter = std::max<CGFloat>(10.0, std::min<CGFloat>(16.0, bounds.size.height - 2.0));
  const CGFloat knobRadius = knobDiameter * 0.5;
  const CGFloat trackLeft = knobRadius + 1.0;
  const CGFloat trackRight = bounds.size.width - knobRadius - 1.0;
  const CGFloat trackWidth = std::max<CGFloat>(1.0, trackRight - trackLeft);
  const CGFloat range = std::max<CGFloat>(self.maxValue - self.minValue, 1.0e-9);
  const CGFloat normalized =
      std::clamp<CGFloat>((self.doubleValue - self.minValue) / range, 0.0, 1.0);
  const CGFloat knobX = trackLeft + normalized * trackWidth;

  [[NSColor colorWithCalibratedWhite:0.055 alpha:1.0] setFill];
  NSRectFill(NSMakeRect(trackLeft, centerY - 1.0, trackWidth, 2.0));
  [[NSColor colorWithCalibratedRed:0.37 green:0.70 blue:0.66 alpha:0.82] setFill];
  NSRectFill(NSMakeRect(trackLeft, centerY - 1.0, std::max<CGFloat>(1.0, knobX - trackLeft), 2.0));

  NSBezierPath* knob =
      [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(knobX - knobRadius,
                                                       centerY - knobRadius,
                                                       knobDiameter,
                                                       knobDiameter)];
  [[NSColor colorWithCalibratedWhite:0.16 alpha:1.0] setFill];
  [knob fill];
  [[NSColor colorWithCalibratedWhite:0.55 alpha:1.0] setStroke];
  knob.lineWidth = 1.0;
  [knob stroke];
}
@end

@interface GainPilotSegmentedControl : NSSegmentedControl
@end

@implementation GainPilotSegmentedControl
- (void)drawRect:(NSRect)dirtyRect {
  (void)dirtyRect;
  const NSRect bounds = NSInsetRect(self.bounds, 0.5, 0.5);
  NSBezierPath* outline = [NSBezierPath bezierPathWithRoundedRect:bounds xRadius:3.0 yRadius:3.0];
  [[NSColor colorWithCalibratedWhite:0.075 alpha:1.0] setFill];
  [outline fill];
  [[NSColor colorWithCalibratedWhite:0.23 alpha:1.0] setStroke];
  outline.lineWidth = 1.0;
  [outline stroke];

  const NSInteger segments = std::max<NSInteger>(self.segmentCount, 1);
  const CGFloat segmentWidth = bounds.size.width / static_cast<CGFloat>(segments);
  NSDictionary<NSAttributedStringKey, id>* normalAttributes = @{
    NSFontAttributeName : [NSFont systemFontOfSize:10.5 weight:NSFontWeightMedium],
    NSForegroundColorAttributeName : [NSColor colorWithCalibratedWhite:0.55 alpha:1.0]
  };
  NSDictionary<NSAttributedStringKey, id>* selectedAttributes = @{
    NSFontAttributeName : [NSFont systemFontOfSize:10.5 weight:NSFontWeightSemibold],
    NSForegroundColorAttributeName :
        [NSColor colorWithCalibratedRed:0.44 green:0.78 blue:0.73 alpha:1.0]
  };

  for (NSInteger index = 0; index < segments; ++index) {
    const NSRect segmentRect =
        NSMakeRect(bounds.origin.x + segmentWidth * static_cast<CGFloat>(index),
                   bounds.origin.y,
                   segmentWidth,
                   bounds.size.height);
    if (index == self.selectedSegment) {
      NSBezierPath* selection =
          [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(segmentRect, 2.0, 2.0)
                                         xRadius:2.0
                                         yRadius:2.0];
      [[NSColor colorWithCalibratedWhite:0.13 alpha:1.0] setFill];
      [selection fill];
    }

    NSString* label = [self labelForSegment:index];
    NSDictionary<NSAttributedStringKey, id>* attributes =
        index == self.selectedSegment ? selectedAttributes : normalAttributes;
    const NSSize textSize = [label sizeWithAttributes:attributes];
    [label drawAtPoint:NSMakePoint(NSMidX(segmentRect) - textSize.width * 0.5,
                                  NSMidY(segmentRect) - textSize.height * 0.5)
        withAttributes:attributes];
  }
}
@end

@interface GainPilotButton : NSButton
@end

@implementation GainPilotButton
- (void)drawRect:(NSRect)dirtyRect {
  (void)dirtyRect;
  const NSRect bounds = NSInsetRect(self.bounds, 0.5, 0.5);
  NSBezierPath* background = [NSBezierPath bezierPathWithRoundedRect:bounds xRadius:3.0 yRadius:3.0];
  [[NSColor colorWithCalibratedWhite:0.11 alpha:1.0] setFill];
  [background fill];
  [[NSColor colorWithCalibratedWhite:0.28 alpha:1.0] setStroke];
  background.lineWidth = 1.0;
  [background stroke];

  NSDictionary* attributes = @{
    NSFontAttributeName : [NSFont systemFontOfSize:10.5 weight:NSFontWeightMedium],
    NSForegroundColorAttributeName : [NSColor colorWithCalibratedWhite:0.72 alpha:1.0]
  };
  const NSSize textSize = [self.title sizeWithAttributes:attributes];
  [self.title drawAtPoint:NSMakePoint(NSMidX(bounds) - textSize.width * 0.5,
                                     NSMidY(bounds) - textSize.height * 0.5)
           withAttributes:attributes];
}
@end

@interface GainPilotGainGraphView : NSView {
 @private
  NSMutableArray<NSNumber*>* history_;
}
- (void)appendGain:(double)value;
@end

@implementation GainPilotGainGraphView
- (instancetype)initWithFrame:(NSRect)frame {
  self = [super initWithFrame:frame];
  if (self != nil) {
    history_ = [[NSMutableArray alloc] initWithCapacity:180];
    self.wantsLayer = YES;
    self.layer.backgroundColor = [NSColor colorWithCalibratedWhite:0.045 alpha:1.0].CGColor;
    self.layer.cornerRadius = 3.0;
  }
  return self;
}

- (void)dealloc {
  [history_ release];
  [super dealloc];
}

- (void)appendGain:(double)value {
  [history_ addObject:@(std::clamp(value, -12.0, 12.0))];
  while (history_.count > 180) {
    [history_ removeObjectAtIndex:0];
  }
  [self setNeedsDisplay:YES];
}

- (void)drawRect:(NSRect)dirtyRect {
  [super drawRect:dirtyRect];
  const NSRect bounds = self.bounds;
  [[NSColor colorWithCalibratedWhite:0.055 alpha:1.0] setFill];
  NSRectFill(bounds);

  [[NSColor colorWithCalibratedWhite:0.13 alpha:0.8] setStroke];
  for (NSInteger line = 0; line < 7; ++line) {
    const CGFloat y = 12.0 + (bounds.size.height - 24.0) * static_cast<CGFloat>(line) / 6.0;
    NSBezierPath* grid = [NSBezierPath bezierPath];
    [grid moveToPoint:NSMakePoint(34.0, y)];
    [grid lineToPoint:NSMakePoint(bounds.size.width - 10.0, y)];
    grid.lineWidth = 0.7;
    [grid stroke];
  }

  NSDictionary* attributes = @{
    NSFontAttributeName : [NSFont monospacedDigitSystemFontOfSize:9.0 weight:NSFontWeightRegular],
    NSForegroundColorAttributeName : [NSColor colorWithCalibratedWhite:0.48 alpha:1.0]
  };
  for (NSInteger label = -12; label <= 12; label += 4) {
    const CGFloat y = 8.0 + (bounds.size.height - 24.0) * (static_cast<CGFloat>(label + 12) / 24.0);
    [[NSString stringWithFormat:@"%+ld", static_cast<long>(label)]
        drawAtPoint:NSMakePoint(5.0, y)
     withAttributes:attributes];
  }

  if (history_.count < 2) {
    return;
  }

  NSBezierPath* curve = [NSBezierPath bezierPath];
  const CGFloat graphWidth = bounds.size.width - 48.0;
  for (NSUInteger index = 0; index < history_.count; ++index) {
    const CGFloat x = 36.0 + graphWidth * static_cast<CGFloat>(index) /
                                 static_cast<CGFloat>(std::max<NSUInteger>(history_.count - 1, 1));
    const double value = history_[index].doubleValue;
    const CGFloat y = 12.0 + (bounds.size.height - 24.0) * static_cast<CGFloat>((value + 12.0) / 24.0);
    if (index == 0) {
      [curve moveToPoint:NSMakePoint(x, y)];
    } else {
      [curve lineToPoint:NSMakePoint(x, y)];
    }
  }
  [[NSColor colorWithCalibratedRed:0.37 green:0.70 blue:0.66 alpha:1.0] setStroke];
  curve.lineWidth = 1.5;
  [curve stroke];
}
@end

namespace gainpilot::vst3 {
class GainPilotMacView;
}

@interface GainPilotMacViewTarget : NSObject {
 @public
  gainpilot::vst3::GainPilotMacView* owner;
}
- (void)sliderChanged:(NSSlider*)sender;
- (void)programModeChanged:(NSSegmentedControl*)sender;
- (void)channelModeChanged:(NSSegmentedControl*)sender;
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

- (void)programModeChanged:(NSSegmentedControl*)sender {
  if (owner == nullptr) {
    return;
  }
  owner->handleSliderChanged(gainpilot::ParamId::programMode, static_cast<float>(sender.selectedSegment));
}

- (void)channelModeChanged:(NSSegmentedControl*)sender {
  if (owner == nullptr) {
    return;
  }
  owner->handleSliderChanged(gainpilot::ParamId::channelMode, static_cast<float>(sender.selectedSegment));
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

const Steinberg::ViewRect kDefaultViewRect{0, 0, 760, 620};
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
    case ParamId::appliedGainValue:
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
    case ParamId::channelMode:
      return value >= 0.5f ? @"Mono" : @"Stereo";
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
  return color(0.052, 0.055, 0.055);
}

NSColor* panelColor() {
  return color(0.082, 0.085, 0.085);
}

NSColor* borderColor() {
  return color(0.20, 0.21, 0.21);
}

NSColor* titleColor() {
  return color(0.87, 0.88, 0.87);
}

NSColor* textColor() {
  return color(0.74, 0.75, 0.74);
}

NSColor* subtleColor() {
  return color(0.47, 0.49, 0.48);
}

NSColor* accentColor() {
  return color(0.44, 0.78, 0.73);
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
  NSTextField* appliedGainLabel{nil};
  NSLevelIndicator* meterLevel{nil};
  GainPilotGainGraphView* gainGraph{nil};
  NSTextField* latencyLabel{nil};
  NSBox* controlsPanel{nil};
  NSTextField* controlsTitleLabel{nil};
  std::array<NSTextField*, kVisibleParams.size()> paramLabels{};
  std::array<NSTextField*, 6> targetScaleLabels{};
  NSSegmentedControl* programModeControl{nil};
  NSTextField* programModeLabel{nil};
  NSSegmentedControl* channelModeControl{nil};
  NSTextField* channelModeLabel{nil};
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
  impl_->headerLabel.font = [NSFont systemFontOfSize:20.0 weight:NSFontWeightSemibold];
  impl_->headerLabel.textColor = titleColor();
  [impl_->root addSubview:impl_->headerLabel];

  impl_->subtitleLabel =
      [NSTextField labelWithString:@"LUFS leveler · BS.1770"];
  impl_->subtitleLabel.font = [NSFont systemFontOfSize:11.0];
  impl_->subtitleLabel.textColor = subtleColor();
  [impl_->root addSubview:impl_->subtitleLabel];

  impl_->meterPanel = [[NSBox alloc] initWithFrame:NSMakeRect(24.0, 24.0, 220.0, height - 110.0)];
  impl_->meterPanel.boxType = NSBoxCustom;
  impl_->meterPanel.borderWidth = 1.0;
  impl_->meterPanel.cornerRadius = 4.0;
  impl_->meterPanel.borderColor = borderColor();
  impl_->meterPanel.fillColor = panelColor();
  [impl_->root addSubview:impl_->meterPanel];

  impl_->meterTitleLabel = [NSTextField labelWithString:@"Target"];
  impl_->meterTitleLabel.font = [NSFont systemFontOfSize:12.0 weight:NSFontWeightSemibold];
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
  impl_->gainReductionLabel.stringValue = @"GR 0.00 dB";

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
  impl_->controlsPanel.cornerRadius = 4.0;
  impl_->controlsPanel.borderColor = borderColor();
  impl_->controlsPanel.fillColor = panelColor();
  [impl_->root addSubview:impl_->controlsPanel];

  impl_->controlsTitleLabel = [NSTextField labelWithString:@"Gain history"];
  impl_->controlsTitleLabel.font = [NSFont systemFontOfSize:12.0 weight:NSFontWeightSemibold];
  impl_->controlsTitleLabel.textColor = titleColor();
  [impl_->controlsPanel.contentView addSubview:impl_->controlsTitleLabel];

  for (std::size_t index = 0; index < kVisibleParams.size(); ++index) {
    const auto param = kVisibleParams[index];
    const auto& spec = parameterSpec(param);

    auto* label = [NSTextField labelWithString:[NSString stringWithUTF8String:spec.name.data()]];
    label.font = [NSFont systemFontOfSize:13.0 weight:NSFontWeightSemibold];
    label.textColor = textColor();
    NSView* parameterHost = index == 0 ? impl_->meterPanel.contentView : impl_->controlsPanel.contentView;
    [parameterHost addSubview:label];
    impl_->paramLabels[index] = label;

    auto* valueLabel = [NSTextField labelWithString:formatParamValue(param, spec.defaultValue)];
    valueLabel.font = [NSFont boldSystemFontOfSize:13.0];
    valueLabel.textColor = accentColor();
    valueLabel.alignment = NSTextAlignmentRight;
    [parameterHost addSubview:valueLabel];
    impl_->valueLabels[index] = valueLabel;

    NSSlider* slider = nil;
    if (index == 0) {
      slider = [[[GainPilotVerticalSlider alloc] initWithFrame:NSZeroRect] autorelease];
      slider.minValue = spec.minValue;
      slider.maxValue = spec.maxValue;
      slider.doubleValue = spec.defaultValue;
      slider.target = impl_->target;
      slider.action = @selector(sliderChanged:);
    } else {
      slider = [[[GainPilotHorizontalSlider alloc] initWithFrame:NSZeroRect] autorelease];
      slider.minValue = spec.minValue;
      slider.maxValue = spec.maxValue;
      slider.doubleValue = spec.defaultValue;
      slider.target = impl_->target;
      slider.action = @selector(sliderChanged:);
    }
    slider.continuous = YES;
    slider.tag = static_cast<NSInteger>(param);
    [parameterHost addSubview:slider];
    impl_->sliders[index] = slider;
  }

  constexpr std::array<int, 6> kTargetScaleValues{-10, -14, -18, -22, -26, -30};
  for (std::size_t index = 0; index < kTargetScaleValues.size(); ++index) {
    auto* label = [NSTextField labelWithString:
                                   [NSString stringWithFormat:@"%d", kTargetScaleValues[index]]];
    label.font = [NSFont monospacedDigitSystemFontOfSize:10.0 weight:NSFontWeightRegular];
    label.textColor = subtleColor();
    label.alignment = NSTextAlignmentLeft;
    [impl_->meterPanel.contentView addSubview:label];
    impl_->targetScaleLabels[index] = label;
  }

  impl_->gainGraph = [[GainPilotGainGraphView alloc] initWithFrame:NSMakeRect(20.0, 160.0, 400.0, 180.0)];
  [impl_->controlsPanel.contentView addSubview:impl_->gainGraph];
  [impl_->gainGraph release];

  impl_->appliedGainLabel = [NSTextField labelWithString:@"Gain 0.00 dB"];
  impl_->appliedGainLabel.font = [NSFont systemFontOfSize:12.0 weight:NSFontWeightMedium];
  impl_->appliedGainLabel.textColor = accentColor();
  impl_->appliedGainLabel.alignment = NSTextAlignmentCenter;
  [impl_->controlsPanel.contentView addSubview:impl_->appliedGainLabel];

  impl_->channelModeLabel = [NSTextField labelWithString:@"Channels"];
  impl_->channelModeLabel.font = [NSFont systemFontOfSize:10.5 weight:NSFontWeightMedium];
  impl_->channelModeLabel.textColor = subtleColor();
  [impl_->controlsPanel.contentView addSubview:impl_->channelModeLabel];

  impl_->channelModeControl = [[GainPilotSegmentedControl alloc] initWithFrame:NSMakeRect(230.0, 310.0, 250.0, 28.0)];
  impl_->channelModeControl.segmentCount = 2;
  [impl_->channelModeControl setLabel:@"STEREO" forSegment:0];
  [impl_->channelModeControl setLabel:@"MONO" forSegment:1];
  impl_->channelModeControl.trackingMode = NSSegmentSwitchTrackingSelectOne;
  impl_->channelModeControl.selectedSegment = 0;
  impl_->channelModeControl.target = impl_->target;
  impl_->channelModeControl.action = @selector(channelModeChanged:);
  [impl_->controlsPanel.contentView addSubview:impl_->channelModeControl];

  impl_->programModeLabel = [NSTextField labelWithString:@"Mode"];
  impl_->programModeLabel.font = [NSFont systemFontOfSize:10.5 weight:NSFontWeightMedium];
  impl_->programModeLabel.textColor = textColor();
  [impl_->controlsPanel.contentView addSubview:impl_->programModeLabel];

  impl_->programModeControl = [[GainPilotSegmentedControl alloc] initWithFrame:NSMakeRect(20.0, 20.0, 160.0, 28.0)];
  impl_->programModeControl.segmentCount = 2;
  [impl_->programModeControl setLabel:@"AUTO" forSegment:0];
  [impl_->programModeControl setLabel:@"SPEECH" forSegment:1];
  impl_->programModeControl.trackingMode = NSSegmentSwitchTrackingSelectOne;
  impl_->programModeControl.selectedSegment = 0;
  impl_->programModeControl.target = impl_->target;
  impl_->programModeControl.action = @selector(programModeChanged:);
  [impl_->controlsPanel.contentView addSubview:impl_->programModeControl];

  impl_->resetButton = [[GainPilotButton alloc] initWithFrame:NSMakeRect(200.0, 20.0, 160.0, 32.0)];
  impl_->resetButton.bordered = NO;
  impl_->resetButton.title = @"RESET HISTORY";
  impl_->resetButton.target = impl_->target;
  impl_->resetButton.action = @selector(resetClicked:);
  [impl_->controlsPanel.contentView addSubview:impl_->resetButton];
  [impl_->programModeControl release];
  [impl_->channelModeControl release];
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
    [impl_->programModeControl
        setSelectedSegment:static_cast<NSInteger>(callbacks_.getParameterValue(ParamId::programMode))];
    [impl_->channelModeControl
        setSelectedSegment:static_cast<NSInteger>(callbacks_.getParameterValue(ParamId::channelMode))];
    const float gainReduction = callbacks_.getParameterValue(ParamId::gainReductionValue);
    [impl_->meterLevel setDoubleValue:gainReduction];
    [impl_->gainReductionLabel
        setStringValue:[NSString stringWithFormat:@"GR  %@", formatParamValue(ParamId::gainReductionValue, gainReduction)]];
    const float appliedGain = callbacks_.getParameterValue(ParamId::appliedGainValue);
    const float maxGain = callbacks_.getParameterValue(ParamId::maxGain);
    NSString* gainStatus = appliedGain >= maxGain - 0.05f ? @"  LIMIT" : @"";
    [impl_->appliedGainLabel
        setStringValue:[NSString stringWithFormat:@"Gain %@%@", formatParamValue(ParamId::appliedGainValue, appliedGain), gainStatus]];
    [impl_->gainGraph appendGain:appliedGain];
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

  constexpr CGFloat kBaseWidth = 760.0;
  constexpr CGFloat kBaseHeight = 620.0;
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
  impl_->appliedGainLabel.font = scaledFont(13.0, true);
  impl_->meterValueLabel.font = scaledFont(11.0);
  impl_->inputIntegratedLabel.font = scaledFont(11.0);
  impl_->outputIntegratedLabel.font = scaledFont(11.0);
  impl_->outputShortTermLabel.font = scaledFont(11.0);
  impl_->latencyLabel.font = scaledFont(11.0);
  impl_->controlsTitleLabel.font = scaledFont(14.0, true);
  impl_->programModeLabel.font = [NSFont systemFontOfSize:10.5 * scale weight:NSFontWeightMedium];
  impl_->channelModeLabel.font = [NSFont systemFontOfSize:10.5 * scale weight:NSFontWeightMedium];

  impl_->headerLabel.frame = rootRect(20.0, 578.0, 220.0, 26.0);
  impl_->subtitleLabel.frame = rootRect(190.0, 582.0, 548.0, 16.0);

  impl_->meterPanel.frame = rootRect(18.0, 18.0, 190.0, 542.0);
  impl_->controlsPanel.frame = rootRect(220.0, 18.0, 522.0, 542.0);

  impl_->meterTitleLabel.frame = localRect(14.0, 505.0, 162.0, 18.0);
  impl_->paramLabels[0].frame = localRect(14.0, 470.0, 162.0, 16.0);
  impl_->sliders[0].frame = localRect(72.0, 218.0, 34.0, 238.0);
  for (std::size_t index = 0; index < impl_->targetScaleLabels.size(); ++index) {
    impl_->targetScaleLabels[index].font =
        [NSFont monospacedDigitSystemFontOfSize:9.0 * scale weight:NSFontWeightRegular];
    const CGFloat y = 445.0 - static_cast<CGFloat>(index) * 45.4;
    impl_->targetScaleLabels[index].frame = localRect(112.0, y, 46.0, 14.0);
  }
  impl_->valueLabels[0].frame = localRect(14.0, 178.0, 162.0, 24.0);
  impl_->valueLabels[0].alignment = NSTextAlignmentCenter;
  impl_->gainReductionLabel.frame = localRect(8.0, 140.0, 174.0, 22.0);
  impl_->meterLevel.frame = localRect(18.0, 120.0, 154.0, 12.0);
  impl_->meterValueLabel.frame = localRect(8.0, 91.0, 174.0, 16.0);
  impl_->inputIntegratedLabel.frame = localRect(8.0, 72.0, 174.0, 16.0);
  impl_->outputIntegratedLabel.frame = localRect(8.0, 53.0, 174.0, 16.0);
  impl_->outputShortTermLabel.frame = localRect(8.0, 34.0, 174.0, 16.0);
  impl_->latencyLabel.frame = localRect(8.0, 12.0, 174.0, 16.0);

  impl_->controlsTitleLabel.frame = localRect(16.0, 505.0, 180.0, 18.0);
  impl_->programModeLabel.frame = localRect(16.0, 470.0, 150.0, 16.0);
  impl_->programModeControl.frame = localRect(16.0, 438.0, 170.0, 25.0);
  impl_->channelModeLabel.frame = localRect(258.0, 470.0, 130.0, 16.0);
  impl_->channelModeControl.frame = localRect(258.0, 436.0, 246.0, 28.0);
  impl_->gainGraph.frame = localRect(16.0, 194.0, 488.0, 224.0);
  impl_->appliedGainLabel.frame = localRect(16.0, 166.0, 488.0, 18.0);

  constexpr std::array<CGFloat, 3> kRowY{116.0, 76.0, 36.0};
  for (std::size_t index = 1; index < kVisibleParams.size(); ++index) {
    const CGFloat rowY = kRowY[index - 1];
    impl_->paramLabels[index].font = [NSFont systemFontOfSize:12.0 * scale weight:NSFontWeightSemibold];
    impl_->valueLabels[index].font = scaledFont(12.0, true);
    [impl_->paramLabels[index] setFrame:localRect(16.0, rowY + 19.0, 240.0, 16.0)];
    [impl_->valueLabels[index] setFrame:localRect(400.0, rowY + 19.0, 104.0, 16.0)];
    [impl_->sliders[index] setFrame:localRect(16.0, rowY, 488.0, 18.0)];
  }

  impl_->resetButton.frame = localRect(358.0, 5.0, 146.0, 25.0);
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
