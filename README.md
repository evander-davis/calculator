# RP2350 Calculator MVP

Portable C++17 calculator core plus a desktop emulator and RP2350 tether build
for a TI-84 Plus CE-style scientific and graphing calculator.

The portable core renders a fixed 320x240 RGB565 LCD framebuffer and receives
semantic calculator key events. It contains no SDL, Pico SDK, USB, battery,
filesystem, or TFT-driver dependencies. Hardware ports should implement the
small platform boundary in `include/calc/calculator.hpp`.

## Repository Layout

```text
include/calc/              Public portable calculator API and platform boundary
src/core/                  Hardware-independent calculator runtime
src/emulator/              SDL2 desktop emulator
firmware/                  RP2350 USB-tether firmware target
tests/                     Host-side core and rendering tests
tools/                     Glyph generation, UF2 conversion, serial viewer
docs/                      Architecture, keymap, and hardware-port notes
glyphs.txt                 Editable bitmap glyph source
calculator_keys.xlsx       Physical keypad layout reference
```

Core responsibilities include expression editing, `Ans`, variables, evaluator
state, math-print layout, cursor/navigation behavior, graph state, menus,
history, and primitive framebuffer rendering.

Platform responsibilities are intentionally thin:

- SDL2 emulator: host window, clickable keypad, host keyboard mapping, scaled
  framebuffer display.
- RP2350 tether firmware: USB serial frame streaming and key packet handling.
- Future hardware firmware: TFT, keyboard matrix, storage, USB-C, charging, and
  battery-specific code behind the same platform boundary.

## Build: SDL Emulator and Tests

Run commands from the project root:

```powershell
cd C:\Documents\calculator
```

On Windows, this one-line command is safe to paste into regular PowerShell. It
uses the Visual Studio build environment and vcpkg SDL2 toolchain:

```powershell
cd C:\Documents\calculator; cmd /c '"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64 && "C:\Program Files\CMake\bin\cmake.exe" -S . -B build-sdl -G "NMake Makefiles" -DCMAKE_TOOLCHAIN_FILE=C:\tmp\vcpkg\scripts\buildsystems\vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows -DCALC_BUILD_EMULATOR=ON && "C:\Program Files\CMake\bin\cmake.exe" --build build-sdl && "C:\Program Files\CMake\bin\ctest.exe" --test-dir build-sdl --output-on-failure'
```

Generic CMake commands also work when the shell already has a configured C++
compiler, build tool, Windows resource compiler, and SDL2 discovery path:

```powershell
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

If SDL2 is installed, CMake builds `calc_emulator`. If SDL2 is not found, the
core library and tests still build.

Launch the desktop emulator:

```powershell
.\build-sdl\calc_emulator.exe
```

To compare a firmware-style `float` numeric build, configure with:

```powershell
-DCALC_USE_FLOAT=ON
```

## Build: RP2350 Tether Firmware

The current RP2350 target is a USB-tether test firmware for the Seeed XIAO
RP2350. Core 0 owns USB transport and host key reception. Core 1 runs the
portable calculator and renders at up to 60 Hz. The transport compares 16x16 RGB565
tiles against the last transmitted image, sends only changed tiles, and sends a
full dirty-tile keyframe when the viewer connects or requests recovery. This
keeps key handling responsive while large screen updates are in flight and
allows small cursor/menu changes to update much faster than full-frame USB
streaming.

Configure once, if the build directory does not already exist:

```powershell
$env:PICO_SDK_PATH = 'C:\tmp\pico-sdk'
cmake -S firmware -B build-xiao-tether-arm4 -G "NMake Makefiles" -DPICO_BOARD=seeed_xiao_rp2350
```

Build from current source. The `PATH` addition lets the local `picotool.exe`
find `libusb-1.0.dll`:

```powershell
$env:PICO_SDK_PATH = 'C:\tmp\pico-sdk'
$env:PATH = 'C:\tmp\vcpkg\installed\x64-windows\bin;' + $env:PATH
cmake --build build-xiao-tether-arm4 --clean-first
```

If Pico SDK's `picotool uf2 convert` step fails after a successful link, convert
the fresh `.bin` manually:

```powershell
py tools\bin_to_uf2.py build-xiao-tether-arm4\calc_tether.bin build-xiao-tether-arm4\calc_tether.uf2 --family rp2350-arm-s --base 0x10000000 --abs-block 0x10FFFF00
```

Flash through BOOTSEL by copying `build-xiao-tether-arm4\calc_tether.uf2` to
the RP2350 bootloader drive, or use `picotool` when available.

Launch the computer-side tether screen after flashing:

```powershell
py tools\tether_viewer.py --port COM4
```

The viewer status line reports displayed update rate, recent USB throughput,
dirty tile/payload counts, on-device render time, and approximate key-to-frame
latency. Its laptop-rendered TI-style keypad sends semantic key packets to the
RP2350; the keypad is not part of the device framebuffer. Run the viewer's
protocol, key-map, keypad-layout, and RGB565 conversion self-test without
hardware:

```powershell
py tools\tether_viewer.py --self-test
```

If Python cannot import `serial`, install pyserial:

```powershell
py -m pip install pyserial
```

## Tests and Visual Verification

Run all host tests:

```powershell
ctest --test-dir build-sdl --output-on-failure
```

`ctest` runs both unit tests and the screenshot-based visual verifier. The
visual verifier writes deterministic 320x240 LCD screenshots to:

```powershell
C:\Documents\calculator\build-sdl\visual_screenshots
```

Those `.ppm` and `.bmp` files cover exponent cursor movement, fraction cursor
movement, long-expression scrolling in both directions, tall history rows,
history row selection, deep nested equation layout, square roots inside
exponents, and exponents/square roots inside fractions.

## MVP Behavior

- Home screen expression entry, editing, history, `Ans`, clear/delete, and
  errors.
- Scientific evaluator with arithmetic, powers, unary minus, parentheses,
  variables, assignment, `sin/cos/tan`, inverse trig, `sqrt`, `log`, `ln`,
  `pi`, and `e`.
- Math-print style display for fractions, square roots, exponents, store arrow,
  cursor anchors, and nested equation layout.
- Graph screen with one `Y=` expression, axes, grid, pan, zoom, and
  deterministic 320x240 rendering.
- Screen state machine for Home, Graph, Y=, Window, Settings, and About.
- Fixed-size buffers in the calculator runtime.

## Design Constraints

- No dynamic allocation in the calculator runtime after initialization.
- No exceptions or RTTI in the portable core.
- Prefer fixed-capacity arrays and explicit error handling.
- Keep `CalcReal` centralized so emulator and firmware precision can be traded
  off deliberately.
- Keep rendering deterministic and primitive-based.
- Keep calculator behavior in the core; emulator and firmware should not
  duplicate expression, graph, or UI logic.

See `AGENTS.md` for coding guidance for future agents working in this repo.
