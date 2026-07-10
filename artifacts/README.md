# Current Build Artifacts

This directory contains only the latest verified runnable packages. Generated
CMake trees, test executables, screenshots, object files, and local tool
installations are intentionally excluded from the repository.

## Emulator

`emulator/calc_emulator.exe` is a Windows x64 Release build from the current
portable core and SDL emulator sources. Keep `emulator/SDL2.dll` beside it.
The build passed all three host test suites and was launch-tested from this
directory on 2026-07-10.

## RP2350 tether firmware

`firmware/calc_tether.uf2` is the current flashable Seeed XIAO RP2350 tether
firmware with the 16 KiB core-1 calculator stack. The matching flat flash image
is `firmware/calc_tether.bin`. The UF2 was flashed and hardware-tested on
2026-07-10.

## SHA-256

```text
F035C9C2A14EE8B06BCACCC95DC2F13A3B0B73FF32A7D93DF608CD678401FC1D  emulator/calc_emulator.exe
6D332BE1EF39C7F9E256BB080E82B8775C53BDE94D17588D7272FD340865DB7A  emulator/SDL2.dll
C747FD90C1CF9FC237039FF18F82A6C7D8A03DF975EE4845886A3A6F348F9B9F  firmware/calc_tether.bin
2862474D4D8A8CA208AC5DD2153D6CE70E2035333ECAC9CAC3B77BC2FA258060  firmware/calc_tether.uf2
```

Build new outputs under ignored `out/` directories, verify them, and copy only
the promoted emulator package or firmware image into this directory.
