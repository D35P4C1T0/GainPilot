# VST3 Integration Notes

The shared DSP core is format-agnostic. VST3 support is intended to sit on top of
that core with separate mono and stereo plugin classes using the stable IDs in
`include/gainpilot/vst3/plugin_ids.hpp`.

Expected SDK location:

- `external/vst3sdk`

Planned wrapper split:

- `processor`: owns `gainpilot::dsp::GainPilotProcessor`
- `controller`: owns parameter metadata, string conversion, and editor state
- `editor`: modern UI layer with the input loudness meter and reset action

This repository does not yet include the actual VST3 wrapper implementation.
