# Architecture

The project is split into a portable calculator core and platform hosts.

## Core

The core is built as `calc_core` and only includes standard C/C++ headers. It
owns calculator state, expression evaluation, graphing, UI screens, and RGB565
framebuffer rendering.

The runtime API is intentionally small:

- `calc_init(Platform&)`
- `calc_tick()`
- `calc_key_down(Key)`
- `calc_key_up(Key)`
- `calc_render()`

The core receives semantic calculator keys, not host keyboard scancodes. It
renders into a caller-owned `Display` buffer. The display is expected to be
320x240 pixels for calculator targets.

## Platform Interfaces

`Platform` contains:

- `Display`: RGB565 pixel buffer.
- `Storage`: fixed-size block reads and writes by key.
- `Clock`: monotonic millisecond counter.
- `Logger`: optional diagnostic output.

RP2350 support should provide these interfaces using the Pico SDK, TFT driver,
USB/storage code, and board timer. Those dependencies should not be included by
the core.

## Runtime Constraints

The calculator runtime uses fixed-capacity buffers for expression text, tokens,
RPN output, history, graph rendering, and variables. The SDL emulator may use
host allocations, but core calculator behavior should remain deterministic and
suitable for a later embedded build.
