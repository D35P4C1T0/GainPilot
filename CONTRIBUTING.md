# Contributing

## Scope

This project aims to keep one shared DSP core and thin native plugin wrappers.
Changes should preserve that separation unless there is a strong technical
reason not to.

## Before Opening a PR

- Build the project locally
- Run:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

- Keep changes format-aware:
  - `VST3` and `LV2` should stay behaviorally aligned unless the format itself
    requires different handling

## Guidelines

- Prefer focused changes over broad rewrites
- Keep public parameter behavior stable unless the change explicitly intends to
  revise compatibility
- Preserve cross-format state compatibility when possible
- Avoid adding host-specific hacks unless they are isolated and documented
- Keep dependencies minimal and justified

## UI Notes

- Windows `VST3` currently uses a custom wxWidgets editor
- Linux `LV2` currently uses a GTK3 UI
- Linux `VST3` currently falls back to the host generic UI

If you change UI behavior, document the platform and format impact clearly.

## Licensing

By contributing, you agree that your changes can be distributed under the MIT
license used by this repository.
