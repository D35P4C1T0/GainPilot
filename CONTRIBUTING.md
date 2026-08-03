# Contributing

## Scope

This project keeps one shared DSP core behind thin DPF plugin and UI adapters.
Changes should preserve that separation unless there is a strong technical
reason not to.

## Before Opening a PR

- Build the project locally
- Run:

```sh
git submodule update --init --recursive
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

- Keep changes format-aware:
  - `VST3`, `LV2`, and `CLAP` should stay behaviorally aligned unless the format
    itself requires different handling

## Guidelines

- Prefer focused changes over broad rewrites
- Keep public parameter behavior stable unless the change explicitly intends to
  revise compatibility
- Preserve cross-format state compatibility when possible
- Avoid adding host-specific hacks unless they are isolated and documented
- Keep dependencies minimal and justified

## UI Notes

- All plugin formats use the same DGL/NanoVG editor.
- Keep UI code independent of host APIs and platform toolkits.
- Verify both mono and stereo metadata when changing parameter or layout code.

If you change UI behavior, document the platform and format impact clearly.

## Licensing

By contributing, you agree that your changes can be distributed under the MIT
license used by this repository.
