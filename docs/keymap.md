# Emulator and Tether Key Maps

Both desktop interfaces send semantic `calc::Key` values into the same portable
calculator core. Shifted `2ND` and `ALPHA` behavior is interpreted by the core,
so button legends and results match between emulator and tether firmware.

## Common keyboard shortcuts

- Digits, `.`, `+`, `-`, `*`, `/`, `^`, `(`, `)`: expression input
- `Enter`: evaluate or accept
- `Backspace` or `Delete`: delete
- `Esc` or `C`: clear
- Arrow keys: cursor movement, graph navigation, or menu selection
- `F1` or `H`: Home
- `F2` or `Y`: Y=
- `F3` or `G`: Graph
- `F4` or `W`: Window
- `F5` or `M`: Settings
- `S`: `sin(`
- `T`: `tan(`
- `L`: `ln(`
- `P`: `pi`
- `X`: graph variable `X`

Clickable buttons expose the complete TI-style layout, including `2ND`,
`ALPHA`, math menus, variables, fractions, roots, inverse trig, `Ans`, `pi`,
`e`, the imaginary unit, `2ND`+`LOG` for `10^x`, and `2ND`+`LN` for `e^x`.

## SDL emulator

Run `artifacts\emulator\calc_emulator.exe`. The SDL window includes the LCD and
clickable keypad. `F12` writes `lcd_screenshot.ppm` in the current working
directory.

## RP2350 tether viewer

Run `py tools\tether_viewer.py`. The viewer auto-detects the RP2350 when
possible; use `--port COMx` to override it. The keypad is rendered on the laptop
with Segoe UI 7 pt labels and Segoe UI Symbol 9 pt bold arrow labels. It is not
part of the RP2350 framebuffer.

The status line reports display rate, USB throughput, dirty-tile payload,
on-device render time, key-to-frame latency, and core-1 stack high-water use.
