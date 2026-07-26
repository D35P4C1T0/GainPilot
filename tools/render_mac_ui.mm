#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>

#import <AppKit/AppKit.h>

#include "mac_view.hpp"
#include "pluginterfaces/base/fstrdefs.h"

namespace {

constexpr int kWidth = 760;
constexpr int kHeight = 620;

}  // namespace

int main(int argc, char** argv) {
  @autoreleasepool {
    [NSApplication sharedApplication];

    const std::filesystem::path outputPath =
        argc > 1 ? argv[1] : "docs/assets/gainpilot-ui.png";
    if (outputPath.has_parent_path()) {
      std::filesystem::create_directories(outputPath.parent_path());
    }

    gainpilot::ParameterState state;
    state.set(gainpilot::ParamId::targetLevel, -21.0f);
    state.set(gainpilot::ParamId::inputTrim, 1.5f);
    state.set(gainpilot::ParamId::truePeak, -1.0f);
    state.set(gainpilot::ParamId::maxGain, 12.0f);
    state.set(gainpilot::ParamId::programMode, static_cast<float>(gainpilot::ProgramMode::speech));
    state.set(gainpilot::ParamId::channelMode, static_cast<float>(gainpilot::ChannelMode::stereo));
    state.set(gainpilot::ParamId::meterValue, -18.42f);
    state.set(gainpilot::ParamId::inputIntegratedValue, -21.87f);
    state.set(gainpilot::ParamId::outputIntegratedValue, -16.05f);
    state.set(gainpilot::ParamId::outputShortTermValue, -15.72f);
    state.set(gainpilot::ParamId::gainReductionValue, 4.8f);

    gainpilot::vst3::GainPilotMacView view({
        .getParameterValue =
            [&state](gainpilot::ParamId id) {
              return state.get(id);
            },
        .getMeterValue =
            [&state]() {
              return state.get(gainpilot::ParamId::meterValue);
            },
        .getLatencyMilliseconds =
            []() {
              return 35.375f;
            },
        .setParameterValue = {},
        .resetIntegrated = {},
    });

    auto* host = [[NSView alloc] initWithFrame:NSMakeRect(0.0, 0.0, kWidth, kHeight)];
    host.wantsLayer = YES;
    if (view.attached((__bridge void*)host, Steinberg::kPlatformTypeNSView) != Steinberg::kResultOk) {
      std::cerr << "Failed to attach the Cocoa plugin view.\n";
      [host release];
      return EXIT_FAILURE;
    }

    for (int step = 0; step < 120; ++step) {
      const float gain = -2.0f + static_cast<float>(step) * 0.055f +
                         1.2f * std::sin(static_cast<float>(step) * 0.22f);
      state.set(gainpilot::ParamId::appliedGainValue, gain);
      view.refreshFromModel();
    }

    [host layoutSubtreeIfNeeded];
    [host displayIfNeeded];
    auto* bitmap = [host bitmapImageRepForCachingDisplayInRect:host.bounds];
    [host cacheDisplayInRect:host.bounds toBitmapImageRep:bitmap];
    NSData* png = [bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
    const BOOL saved = [png writeToFile:[NSString stringWithUTF8String:outputPath.string().c_str()] atomically:YES];

    view.removed();
    [host release];

    if (!saved) {
      std::cerr << "Failed to save " << outputPath << "\n";
      return EXIT_FAILURE;
    }
    std::cout << "Wrote " << outputPath << "\n";
    return EXIT_SUCCESS;
  }
}
