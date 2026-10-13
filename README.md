# VGM Music Maker v1.1 QT Port

A native rebuild of Shiru's VGM Music Maker 1.1 (`vgm_mm.exe`, Windows, Borland C++Builder).
The program logic was decompiled from the executable using Ghidra, rewritten in C++ using QT and code was then renamed and commented using Opus for readability.
I asked for his (Shiru) permission to publish this and he gave me the greenlight.

Might add more features to this in the future if people want them.

## To Build

Needs CMake >= 3.16, a C++17 compiler, Qt 6 (Widgets), SDL2, zlib and optionally ALSA (MIDI in).

    cmake -S . -B build
    cmake --build build -j
    ./build/vgmmm [module.vge]

Inside Qt Creator: open `CMakeLists.txt`, pick the Desktop kit on the *Configure Project* page,
then build and run the `vgmmm` target.
