# RP2350 Calculator MVP

Portable C++17 calculator core plus an optional SDL2 desktop emulator for a
320x240 RGB565 LCD and traditional calculator keys.

The core contains no SDL, Pico SDK, USB, battery, or TFT driver dependencies.
Hardware ports should implement the small platform interfaces in
`include/calc/calculator.hpp`.

## Build

Run commands from the project root:

```powershell
cd C:\Documents\calculator
```

On Windows, use the Visual Studio build environment. This one-line command is
safe to paste into regular PowerShell:

```powershell
cd C:\Documents\calculator; cmd /c '"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64 && "C:\Program Files\CMake\bin\cmake.exe" -S . -B build-sdl -G "NMake Makefiles" -DCMAKE_TOOLCHAIN_FILE=C:\tmp\vcpkg\scripts\buildsystems\vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows -DCALC_BUILD_EMULATOR=ON && "C:\Program Files\CMake\bin\cmake.exe" --build build-sdl && "C:\Program Files\CMake\bin\ctest.exe" --test-dir build-sdl --output-on-failure'
```

Generic CMake commands also work when your shell already has a configured C++
compiler, build tool, and Windows resource compiler on `PATH`:

```powershell
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

If SDL2 is installed, CMake also builds `calc_emulator`. If SDL2 is not found,
the core library and tests still build.

To compare a firmware-style `float` numeric build, add:

```powershell
-DCALC_USE_FLOAT=ON
```

## MVP Behavior

- Home screen expression entry, editing, history, `Ans`, clear/delete, and errors.
- Scientific evaluator with arithmetic, powers, unary minus, parentheses,
  variables, assignment, `sin/cos/tan`, inverse trig, `sqrt`, `log`, `ln`,
  `pi`, and `e`.
- Graph screen with one `Y=` expression, axes, grid, pan, zoom, and deterministic
  320x240 rendering.
- Screen state machine for Home, Graph, Y=, Window, Settings, and About.
- Fixed-size buffers in the calculator runtime.
