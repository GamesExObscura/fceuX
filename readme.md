# fceuX

fceuX is a Windows fork of [FCEUX](https://github.com/TASEmulators/fceux) with Miracle Piano support and quality-of-life changes.

**Download:** the latest zip from [Releases](../../releases). Unzip anywhere and run `fceuX.exe`. No games or system files are included.

## Features

* Interactive R.O.B. peripheral viewer for Gyromite and Stack-Up.
* Two color-coded gyros with 60-second spin timers, wobble indicators, and automatic holder resets.
* B hotkey to toggle the R.O.B. viewer.
* Miracle Piano support with a separate mouse- and touch-enabled keyboard.
* Game-driven piano audio for title music, demonstrations, lessons, and live keyboard input.
* Keyboard volume control up to 400% and toggleable sustain pedal.
* Resizable, maximizable peripheral windows with saved positioning and disconnected-monitor fallback.
* Delayed piano focus return to the main game window.
* Persistent input-device selections and live controller/peripheral switching.
* Borderless fullscreen with automatically hidden menus revealed at the top mouse edge.

## Building

Windows, MSVC + Qt 6 + SDL2: configure the repository root with CMake (`-DCMAKE_PREFIX_PATH=<Qt msvc dir> -DSDL_INSTALL_PREFIX=<SDL2 dir with include/ and lib/x64/>`), then build Release. The executable is named `fceuX.exe`.

## License and credits

Licensed under GPL-2.0-or-later, as FCEUX. Thanks to the FCEUX developers for the emulator this is built on.

---

# Upstream README

# fceux [![Build status](https://ci.appveyor.com/api/projects/status/github/TASEmulators/fceux?branch=master&svg=true)](https://ci.appveyor.com/project/zeromus/fceux)

An open source NES Emulator for Windows and Unix that features solid emulation accuracy and state of the art tools for power users. For some reason casual gamers use it too.

## Builds and Releases

Interim builds:
* Win32: [fceux-win32.zip](https://github.com/TASEmulators/fceux/releases/download/interim-build/fceux-win32.zip)
* Win64: [fceux-win64.zip](https://github.com/TASEmulators/fceux/releases/download/interim-build/fceux-win64.zip)
* Win64 Qt/SDL: [fceux-win64-QtSDL.zip](https://github.com/TASEmulators/fceux/releases/download/interim-build/fceux-win64-QtSDL.zip)
* Ubuntu: [fceux-ubuntu-x64.deb](https://github.com/TASEmulators/fceux/releases/download/interim-build/fceux-ubuntu-x64.deb)
* MacOSX: [fceux-Darwin.dmg](https://github.com/TASEmulators/fceux/releases/download/interim-build/fceux-Darwin.dmg)
* Status: [Appveyor](https://ci.appveyor.com/project/zeromus/fceux/)

But you might like Mesen more: https://github.com/SourMesen/Mesen2

You should get releases from here: https://sourceforge.net/projects/fceultra/files/

That's because github forces us to use tags we don't have for releases.

2.6.6 is the most recent release but most people are using the autobuilds.
