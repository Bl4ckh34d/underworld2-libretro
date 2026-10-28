# Ultima Underworld II — Android libretro port

Native ARMv7 integration of Underworld Exhumed and its Exhume runtime for RetroArch on Android TV. The game executes translated C code with a cooperative platform backend; it does not run through DOSBox.

## Build

The supported build host is Windows with Python 3.10+, Git for Windows and Android NDK r27d. Output is ARMv7 for 32-bit RetroArch, not arm64. Set `ANDROID_NDK_HOME` to the NDK directory if it is not `C:/tools/android/android-ndk-r27d`.

```powershell
python scripts/fetch_sources.py
python scripts/build_underworld2_tv.py
```

`dependencies.json` locks each upstream revision and patch hash. `vendored.json` records the provenance and SHA-256 of copied headers. Fetching and building refuse a different checkout revision or origin and never reset existing work. Builds apply the recorded patches if needed. `PORT_SOURCES_DIR` can point to an existing source directory with exactly these dependencies. Additional local edits are preserved; a clean pinned checkout plus the patches is required to reproduce the published source state.

The resulting library is `release/underworld2_libretro_android.so`. Generated sources, objects, binaries and logs are ignored. No original game data or player saves are included.

## RetroArch setup

Install the resulting core into a 32-bit Android RetroArch installation using its supported core installation mechanism. Put your own game files in a readable directory; choose a separate writable save directory. `profiles/android-example.json` contains a playlist entry and per-game configuration/remap examples. Replace `YOUR_USB_ID` and adapt the paths and app package for your device. It is a reference profile, not an automatic installer.

The Xbox labels below match the tested TV mapping. Configure physical L2 as RetroArch fast-forward. Leave L3/R3 unbound from RetroArch generic Save State/Load State: the core handles native game saves on those buttons. Guide opens the RetroArch Quick Menu. Reset restarts the engine; Close Content returns to RetroArch. Fast-forward has no fixed multiplier and depends on CPU capacity. Native saves are separate from generic serialized RetroArch save states, which these cores do not implement.

Preview images belong in RetroArch's thumbnail folders under the playlist name, using the exact entry label. Screenshots and commercial cover art are not part of this source repository.

## Verification

See [verification notes](docs/verification.md) for the recorded checks and their limits. `tests/native_probe.cpp` is a small Android libretro host for isolated video/audio, input and lifecycle checks. Tests must use a separate writable save directory. Its `--lifecycle` mode is specifically for cores that reject a second load into the same module; it is not a universal core compliance test.

The build fetches the two pinned Nuked-OPL3 source files on first use and checks their SHA-256. Their source revision and checksums are in `scripts/build_underworld2_tv.py`. The nested Exhume dependency is fetched separately at the exact locked revision before applying its patch.

## Game data

Load your own `UW2.EXE`, with its original game data directories beside it. The upstream-supported GOG executable remains accepted. This port also accepts the tested floppy edition after the official Origin patch: size 675184 bytes, CRC32 `C0C97A43`, SHA-256 `d54d5078873e351eb97fb3850165467cf243a2de766a430b7539520d484f9596`. Other executables still fail validation. Neither the executable nor the official patch is redistributed here.

## Changes

* Libretro video/audio/input and cooperative engine scheduling, including 44100 Hz audio delivery.
* ARM portability fixes and exact validation for the tested patched floppy executable.
* Embedded-engine reload and tracked resource cleanup for Reset.
* Virtual keyboard, mouse-look, native slot IV quicksave/load and a guard against quickloading before gameplay is initialized.

## Controller

Left stick moves/strafe; right stick moves the cursor or looks when Y enables mouse-look. A confirms, B jumps, X uses/talks, Y toggles mouse-look. LB left-clicks, RB right-clicks and R2 attacks. D-pad up opens the map, down looks, left selects runes and right opens statistics. View opens the virtual keyboard; Start opens the game menu. L3 saves to slot IV; R3 loads it during gameplay. At a cold title screen use Journey Onward and slot IV first; R3 is deliberately inactive there.

The virtual keyboard uses the left stick/D-pad to select, A to type, B for Backspace, X for Enter and Y for Space. View closes it.

## Licensing and provenance

The root MIT license covers this repository's original integration code, scripts and documentation only. Underworld Exhumed explicitly excludes game-derived code and assets from its MIT grant. Its upstream root NOTICE, UW2 NOTICE, third-party notices and Exhume license are preserved in `licenses/`; they remain applicable to fetched sources and patches.

Vendored `libretro.h` and `minicoro.h` retain their embedded notices. Nuked-OPL3 is LGPL-2.1-or-later and is statically linked by this build; any binary redistribution must satisfy its corresponding-source and relinking requirements. Upstream third-party notices describe additional components; this Android build does not use every component of the upstream desktop releases. No original executable or game data is included.
