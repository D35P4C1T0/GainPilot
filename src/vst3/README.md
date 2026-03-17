# VST3 Integration Notes

The shared DSP core is format-agnostic. The VST3 wrapper in this repository sits
on top of that core with separate mono and stereo processor classes and a
shared controller/editor layer.

SDK location:

- `external/vst3sdk`

Current wrapper split:

- `processor`: owns `gainpilot::dsp::GainPilotProcessor`
- `controller`: owns parameter metadata, string conversion, and editor state
- `editor`: platform-specific UI layer
  - macOS: native Cocoa editor
  - Windows: wxWidgets editor
  - Linux: host generic UI fallback

Host-facing parameters are intentionally limited to the simplified user control
set plus read-only loudness metering.
