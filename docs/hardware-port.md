# RP2350 Hardware Status and Porting Notes

The repository currently contains a working Seeed XIAO RP2350 USB tether target
in `firmware/`. It executes the real portable calculator core on the RP2350,
accepts semantic key packets from a laptop, and sends the 320x240 RGB565 display
back to `tools/tether_viewer.py`.

## Current tether target

- Core 0: USB CDC reception and batched dirty-tile transmission.
- Core 1: calculator input, evaluation, graphing, ticking, and framebuffer
  rendering.
- Framebuffer: one 320x240 RGB565 buffer (153,600 bytes).
- Change detection: 300 hashes for 16x16 tiles rather than a duplicate
  framebuffer.
- Transport: changed tiles batched into roughly 8 KiB writes; zero-pixel state
  changes still receive a frame-end acknowledgement.
- Numeric mode: `float` by default to use the Cortex-M33 FPU.
- Core-1 stack: 16 KiB reserved, with 7,336 bytes observed under the nested
  expression and graph stress workload.
- Current flash image: 141,224 bytes. The packaged UF2 and BIN are under
  `artifacts/firmware/`.

The tether does not yet drive a physical LCD or scan a physical keypad. Its
TI-style keypad is rendered by the laptop and sends semantic inputs to the
board.

## Standalone calculator drivers still required

- TFT driver that consumes the core's fixed RGB565 framebuffer or flushes its
  changed regions.
- Keyboard-matrix scanner with debouncing and semantic `calc::Key` mapping.
- Flash-backed implementations of the optional storage hooks.
- Battery charging, fuel-gauge, power-state, and brightness handling outside
  the portable core.
- Any file/program-transfer protocol beyond the current test tether.

## Recommended standalone main loop

1. Scan and debounce hardware buttons.
2. Emit semantic `calc_key_down` and `calc_key_up` events.
3. Call `calc_tick()`.
4. When `calc_needs_render()` is true, call `calc_render()`.
5. Flush the framebuffer or changed display regions to the TFT.

Keep Pico SDK, USB, TFT, filesystem, and battery headers in the platform target.
Do not introduce them into `src/core` or `include/calc`.

## Building and flashing the tether

Configure and build under ignored `out/firmware-build` as documented in the
root README. If Pico SDK's `picotool uf2 convert` crashes after a successful
link, use `tools/bin_to_uf2.py` on the generated BIN. The promoted
`artifacts/firmware/calc_tether.uf2` is the last hardware-tested image.
