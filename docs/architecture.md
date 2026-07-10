# Architecture

The project has one portable calculator implementation and two thin platform
hosts: an SDL2 desktop emulator and a Seeed XIAO RP2350 USB tether firmware.

## Portable core

`src/core` builds as `calc_core` and depends only on standard C/C++ headers. It
owns expression editing and layout, history, evaluation, variables, graph and
trace state, menus, tables, solver behavior, and rendering into a caller-owned
320x240 RGB565 framebuffer.

The main runtime boundary in `include/calc/calculator.hpp` is:

- `calc_init(Platform&)`
- `calc_tick()`
- `calc_key_down(Key)` / `calc_key_up(Key)`
- `calc_needs_render()`
- `calc_render()`

`Platform` supplies a framebuffer, optional fixed-size storage hooks, a
monotonic millisecond clock, and an optional logger. The core receives semantic
calculator keys rather than host scancodes or physical matrix coordinates.

The evaluator also exposes a narrow compiled-expression API. Graph expressions
compile to fixed RPN storage, common real-valued expressions use a scalar fast
path, and complex expressions retain the full complex evaluator. Adaptive graph
sampling and fixed curve-sample caches avoid unnecessary per-pixel evaluation
and repeated work during trace redraws.

## Desktop emulator

`src/emulator/main.cpp` owns SDL window/event handling, host keyboard mapping,
clickable calculator buttons, framebuffer scaling, screenshots, and host file
storage. It calls the same calculator API used by firmware and does not contain
separate calculator behavior.

The current packaged Windows Release build is in `artifacts/emulator`. New
development builds belong under ignored `out/` directories.

## RP2350 USB tether

`firmware/tether_main.cpp` runs the portable core on the RP2350:

- Core 0 handles USB CDC input and batched dirty-region transport.
- Core 1 handles semantic keys, calculator state, evaluation, and rendering.
- Core 1 uses an explicit 16 KiB stack in main SRAM with canary-based high-water
  telemetry; the measured stress-test peak is 7,336 bytes.
- A single 153,600-byte RGB565 framebuffer is shared behind a mutex.
- Per-tile hashes replace a second framebuffer. Changed 16x16 tiles are grouped
  into roughly 8 KiB USB packets.
- Rendering is event-driven through `calc_needs_render()` rather than continuous
  full-screen redraws.

`tools/tether_viewer.py` displays the transmitted framebuffer and renders the
computer-side keypad. Keypad pixels never consume the RP2350 framebuffer.

## Repository and generated outputs

Source, tests, hardware design files, tools, and documentation remain in their
named top-level directories. Generated CMake trees, screenshots, objects, test
executables, and local tool installations belong under ignored `out/` paths.
Only verified runnable packages are promoted into `artifacts/`.

## Runtime constraints

The calculator runtime avoids dynamic allocation after initialization and uses
fixed-capacity expressions, token/RPN workspaces, history, variables, graph
caches, queues, and transport buffers. Exceptions and RTTI are disabled for the
embedded core build. The host normally uses `double`; firmware uses
hardware-accelerated `float` through `CALC_USE_FLOAT=ON`.
