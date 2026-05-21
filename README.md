# RP2350 Calculator MVP

Portable C++17 calculator core plus an optional SDL2 desktop emulator for a
320x240 RGB565 LCD and traditional calculator keys.

The core contains no SDL, Pico SDK, USB, battery, or TFT driver dependencies.
Hardware ports should implement the small platform interfaces in
`include/calc/calculator.hpp`.

## Build

```powershell
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

If SDL2 is installed, CMake also builds `calc_emulator`. If SDL2 is not found,
the core library and tests still build.

## MVP Behavior

- Home screen expression entry, editing, history, `Ans`, clear/delete, and errors.
- Scientific evaluator with arithmetic, powers, unary minus, parentheses,
  variables, assignment, `sin/cos/tan`, inverse trig, `sqrt`, `log`, `ln`,
  `pi`, and `e`.
- Graph screen with one `Y=` expression, axes, grid, pan, zoom, and deterministic
  320x240 rendering.
- Screen state machine for Home, Graph, Y=, Window, Settings, and About.
- Fixed-size buffers in the calculator runtime.
