#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>
#include "gainpilot/parameters.hpp"

static void check(OSStatus status, const char* operation) {
  if (status != noErr) {
    std::cerr << operation << ": OSStatus " << status << '\n';
    std::exit(1);
  }
}
static OSStatus input(void*, AudioUnitRenderActionFlags*, const AudioTimeStamp* time,
                      UInt32, UInt32 frames, AudioBufferList* buffers) {
  for (UInt32 ch = 0; ch < buffers->mNumberBuffers; ++ch) {
    auto* samples = static_cast<float*>(buffers->mBuffers[ch].mData);
    if (!samples) return kAudioUnitErr_InvalidPropertyValue;
    for (UInt32 n = 0; n < frames; ++n)
      samples[n] = time->mSampleTime == 0 && n == 0 ? .1f : 0.f;
  }
  return noErr;
}
int main(int argc, char** argv) {
  if (argc != 3) return 1;
  const UInt32 channels = static_cast<UInt32>(std::stoi(argv[2]));
  auto url = CFURLCreateFromFileSystemRepresentation(nullptr,
      reinterpret_cast<const UInt8*>(argv[1]), std::strlen(argv[1]), true);
  auto bundle = CFBundleCreate(nullptr, url);
  CFRelease(url);
  if (!bundle) { std::cerr << "Cannot create AU bundle\n"; return 1; }
  CFErrorRef error = nullptr;
  if (!CFBundleLoadExecutableAndReturnError(bundle, &error)) {
    if (error) { CFShow(error); CFRelease(error); }
    std::cerr << "Cannot load AU executable\n"; return 1;
  }
  const auto factory = reinterpret_cast<AudioComponentFactoryFunction>(
      CFBundleGetFunctionPointerForName(bundle, CFSTR("PluginAUFactory")));
  if (!factory) { std::cerr << "Missing AU factory\n"; return 1; }
  AudioComponentDescription desc{kAudioUnitType_Effect, static_cast<OSType>(channels == 1 ? 'GnPm' : 'GnPs'), 'GnPl', 0, 0};
  const auto component = AudioComponentRegister(&desc, CFSTR("GainPilot test instance"), 1, factory);
  if (!component) { std::cerr << "Cannot register process-local AU\n"; return 1; }
  AudioUnit unit = nullptr;
  check(AudioComponentInstanceNew(component, &unit), "Instantiate");
  AudioStreamBasicDescription format{48000, kAudioFormatLinearPCM,
      static_cast<AudioFormatFlags>(kAudioFormatFlagsNativeFloatPacked) | static_cast<AudioFormatFlags>(kAudioFormatFlagIsNonInterleaved),
      sizeof(float), 1, sizeof(float), channels, 32, 0};
  check(AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &format, sizeof(format)), "Input format");
  check(AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &format, sizeof(format)), "Output format");
  UInt32 maxFrames = 256;
  check(AudioUnitSetProperty(unit, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &maxFrames, sizeof(maxFrames)), "Block size");
  AURenderCallbackStruct callback{input, nullptr};
  check(AudioUnitSetProperty(unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &callback, sizeof(callback)), "Input callback");
  const auto set = [&](gainpilot::ParamId id, float value) {
    check(AudioUnitSetParameter(unit, static_cast<AudioUnitParameterID>(id), kAudioUnitScope_Global, 0, value, 0), "Set parameter");
  };
  set(gainpilot::ParamId::correctionHigh, 0);
  set(gainpilot::ParamId::correctionLow, 0);
  set(gainpilot::ParamId::targetLevel, -23);
  set(gainpilot::ParamId::inputLevel, -23);
  check(AudioUnitInitialize(unit), "Initialize");
  Float64 latency = 0;
  UInt32 size = sizeof(latency);
  check(AudioUnitGetProperty(unit, kAudioUnitProperty_Latency, kAudioUnitScope_Global, 0, &latency, &size), "Latency");
  const auto delay = static_cast<std::size_t>(std::llround(latency * 48000));
  if (delay != 1698) { std::cerr << "Unexpected AU latency " << delay << '\n'; return 1; }
  struct Buffers { UInt32 count; AudioBuffer buffers[2]; } audio{};
  audio.count = channels;
  std::vector<float> left(256), right(256);
  audio.buffers[0] = {1, 256 * sizeof(float), left.data()};
  audio.buffers[1] = {1, 256 * sizeof(float), right.data()};
  for (std::size_t frame = 0; frame < 4096; frame += 256) {
    AudioTimeStamp time{};
    time.mFlags = kAudioTimeStampSampleTimeValid;
    time.mSampleTime = static_cast<Float64>(frame);
    AudioUnitRenderActionFlags flags = 0;
    check(AudioUnitRender(unit, &flags, &time, 0, 256, reinterpret_cast<AudioBufferList*>(&audio)), "Render");
    for (std::size_t n = 0; n < 256; ++n) {
      const float expected = frame + n == delay ? .1f : 0;
      if (std::abs(left[n] - expected) > 1e-6f || (channels == 2 && std::abs(right[n] - expected) > 1e-6f)) {
        std::cerr << "AU routing/delay mismatch at " << frame + n << '\n'; return 1;
      }
    }
  }
  AudioUnitParameterValue resetBefore = 0, resetAfter = 0;
  check(AudioUnitGetParameter(unit, static_cast<AudioUnitParameterID>(gainpilot::ParamId::meterResetCount), kAudioUnitScope_Global, 0, &resetBefore), "Reset count");
  // GUI press and release can both arrive before the next audio callback.
  set(gainpilot::ParamId::meterReset, 1);
  set(gainpilot::ParamId::meterReset, 0);
  AudioTimeStamp resetTime{};
  resetTime.mFlags = kAudioTimeStampSampleTimeValid;
  resetTime.mSampleTime = 4096;
  AudioUnitRenderActionFlags resetFlags = 0;
  check(AudioUnitRender(unit, &resetFlags, &resetTime, 0, 256, reinterpret_cast<AudioBufferList*>(&audio)), "Render after reset pulse");
  check(AudioUnitGetParameter(unit, static_cast<AudioUnitParameterID>(gainpilot::ParamId::meterResetCount), kAudioUnitScope_Global, 0, &resetAfter), "Reset acknowledgement");
  if (resetAfter == resetBefore) { std::cerr << "Short reset pulse was lost\n"; return 1; }
  set(gainpilot::ParamId::referenceMode, 1);
  set(gainpilot::ParamId::lockedReference, -29.25f);
  CFPropertyListRef state = nullptr;
  size = sizeof(state);
  check(AudioUnitGetProperty(unit, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &state, &size), "Save state");
  set(gainpilot::ParamId::lockedReference, -12);
  check(AudioUnitSetProperty(unit, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &state, sizeof(state)), "Restore state");
  CFRelease(state);
  AudioUnitParameterValue restored = 0;
  check(AudioUnitGetParameter(unit, static_cast<AudioUnitParameterID>(gainpilot::ParamId::lockedReference), kAudioUnitScope_Global, 0, &restored), "Read restored reference");
  if (restored != -29.25f) return 1;
  check(AudioUnitUninitialize(unit), "Uninitialize");
  check(AudioComponentInstanceDispose(unit), "Dispose");
  CFRelease(bundle);
  std::cout << "AU " << channels << " channel routing, latency and locked state passed\n";
}
