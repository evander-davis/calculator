# Agent Guide

This project is building a TI-84 Plus CE-style scientific and graphing
calculator for the RP2350, with a desktop emulator used to develop and verify
portable behavior before hardware-specific drivers exist.

## Core Goals

- Keep the calculator core portable. Code under `src/core` and `include/calc`
  must not include SDL, Pico SDK, USB, filesystem, battery, or display-driver
  headers.
- Target a fixed 320x240 RGB565 framebuffer. Treat this as the hardware display
  contract, not as an emulator convenience.
- Model the user experience of a TI-84 Plus CE where practical: expression
  history, `Ans`, variables, math-print style fractions/square roots/exponents,
  graphing screens, and calculator-style keyboard navigation.
- Prioritize small memory use, predictable execution time, and clear hardware
  abstraction over visual flourishes or large dependencies.
- Make the code adaptable instead of duplicative. Shared behavior belongs in the
  portable core; emulator and firmware targets should be thin platform layers.

## Architecture Rules

- `src/core` owns calculator behavior: input state, expression editing,
  evaluation, graph state, equation layout, history, and framebuffer rendering.
- `src/emulator` owns SDL2 host-window behavior only. It translates host
  keyboard/mouse input into semantic calculator keys and displays the core
  framebuffer.
- `firmware` owns RP2350 tether/demo behavior only. It should call the same core
  APIs and avoid reimplementing calculator logic.
- `tools` contains host utilities such as glyph generation, UF2 conversion, and
  the serial tether viewer.
- `tests` should exercise portable behavior without SDL or Pico dependencies.

Use the public API in `include/calc/calculator.hpp` as the boundary between
platform code and calculator code. Prefer adding narrow platform hooks there
over letting hardware-specific code leak into the core.

## Core Coding Practices

- Avoid dynamic allocation in the calculator runtime after initialization.
  Prefer fixed-capacity arrays, ring buffers, explicit bounds checks, and
  deterministic fallback behavior when capacity is exceeded.
- Keep exceptions and RTTI out of the core. Error paths should use explicit
  result values, status enums, or visible calculator error states.
- Do not add large general-purpose libraries to the core unless there is a clear
  size/performance benefit on RP2350-class hardware.
- Keep `CalcReal` centralized. The emulator may use `double` for correctness;
  firmware can build with `CALC_USE_FLOAT=ON` when size or speed tradeoffs make
  sense.
- Maintain one source of truth for equation behavior. Formatting, cursor
  navigation, history rendering, and evaluation should share the same expression
  model where possible instead of growing parallel ad hoc paths.
- Make every cursor/navigation change visually testable. Arrow keys should only
  do nothing at a real edge; the cursor should remain visible and accurately
  represent the next insertion point.
- Keep rendering primitive-based: pixels, lines, rectangles, glyph blits, and
  small purpose-built symbols. Avoid adding a retained GUI framework to the
  portable layer.
- Preserve deterministic output for tests. Rendering smoke tests should not
  depend on host timing, font libraries, DPI, or window size.

## Editing Guidance

- Read nearby code before editing. Follow the existing style and fixed-buffer
  patterns rather than introducing unrelated abstractions.
- Keep changes scoped. Do not refactor unrelated calculator behavior while
  fixing a screen, key, or expression-layout issue.
- When changing glyphs, update `glyphs.txt` and regenerate C++ tables with
  `tools/generate_glyph_tables.py` instead of hand-editing generated data.
- When changing expression rendering or cursor behavior, update or add tests in
  `tests/core_tests.cpp` or `tests/visual_verifier.cpp`.
- When changing firmware, confirm that the RP2350 target is rebuilt from current
  `src/core` sources and that UF2 artifacts are not stale.

## Verification Expectations

For portable core changes:

```powershell
cmake --build build-sdl
ctest --test-dir build-sdl --output-on-failure
```

For visual/UI changes, also inspect the emulator and/or generated screenshots:

```powershell
.\build-sdl\calc_emulator.exe
```

For RP2350 tether builds, ensure `PICO_SDK_PATH` is set and `picotool.exe` can
find `libusb-1.0.dll` through the vcpkg bin directory:

```powershell
$env:PICO_SDK_PATH = 'C:\tmp\pico-sdk'
$env:PATH = 'C:\tmp\vcpkg\installed\x64-windows\bin;' + $env:PATH
cmake --build build-xiao-tether-arm4 --clean-first
```

If Pico SDK's `picotool uf2 convert` step fails, `tools/bin_to_uf2.py` can
convert the generated `.bin` into an RP2350 UF2 as a narrow fallback.
