# Future RP2350 Hardware Port

The RP2350 port should be added as a new platform target, not by changing the
portable core.

## Required Drivers

- TFT driver that exposes a 320x240 RGB565 framebuffer or flushes the core
  framebuffer to the physical display.
- Keyboard matrix scanner that debounces traditional buttons and emits semantic
  `calc::Key` events.
- Monotonic millisecond clock for `calc_tick`.
- Flash-backed storage implementation for settings, history, and future user
  programs.
- USB-C data path for future file/program transfer.
- Battery charger/fuel-gauge integration outside the core.

## Main Loop Shape

1. Scan hardware buttons and emit `calc_key_down` / `calc_key_up`.
2. Call `calc_tick`.
3. Call `calc_render`.
4. Flush dirty framebuffer regions or the full 320x240 buffer to the TFT.

The core currently redraws a full framebuffer. A later hardware port can add a
dirty-rectangle display adapter without changing calculator behavior.
