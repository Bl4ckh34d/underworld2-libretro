# Verification record

Publication verification: 2026-10-29. Windows host, Android NDK r27d, Android 11 ARMv7 TV. The source fetch/check scripts and Android test compilation passed. All patches passed forward application checks against a pristine Git index at their pinned revisions and reverse checks against the built working source. The working source was reused without resetting it.

The build SHA-256 in `verification.json` identifies this local build, not a reproducible byte-for-byte promise across paths or hosts. It is not a hash read from RetroArch's protected installed-core directory. Tests of the publication build use an isolated ADB shell process and disposable saves, leaving the installed application and player saves untouched.

## Reset regression

`python scripts/build_tests.py` builds the test host and raw/wrapped fixtures under `build/tests`. Upload them to a new directory under `/data/local/tmp` on an authorized ARMv7 Android device, then mark `restart_test` executable. Run:

```sh
./restart_test ./restart_fixture.so
# Expected exit 5: FAIL cycle 1 expected 7 got 14 shutdown=0
./restart_test ./restart_wrapped.so
# Expected exit 0: PASS 10 resets, callbacks and controller device preserved
```

The raw fixture intentionally has static state that survives Reset. The wrapper must actually reload the module and preserve video/audio/input callbacks and the selected controller device. This tests reset behavior rather than merely checking a function exists.

## Engine checks

The generic probe accepts `CORE SYSTEM CONTENT SAVE OPTIONS --interactive`. Use absolute Android paths, an empty options file where no options are required, and a new writable SAVE directory. Commands on stdin include `run 120`, `pad 14` (L3), `pad 15` (R3), `axis 0 1 -24000 120`, `reset 120`, `ff 1`, and `quit`. It writes a single overwritten `probe-9999.ppm` image in SAVE. Exit alone does not prove gameplay: inspect the image and engine evidence. The `--lifecycle` mode rejects same-module reloading and should only be used where that is the intended engine contract.

The independent native build passed. The publication library completed startup, R3 at the cold title screen, full Reset, another cold-title R3, and Close with nonzero audio and no shutdown. The restart log confirms a fresh engine load. The cold-title quickload guard is intentional: use Journey Onward to enter a saved game before native R3 becomes available.

Earlier installed-version testing covered in-game native slot IV save/load, movement, fast-forward, Reset and reopening after Close Content. Optimized ARM replays matched the DOS reference at 43 new-game, 68 walking, 80 Nystul-dialogue and 92 Lord-British-dialogue checkpoints. Full saved LEV.ARK hashes from the supplied floppy data differed from the GOG reference even though the tested runtime checkpoints matched; no universal save equivalence or full campaign validation is claimed. The publication pass does not repeat all those gameplay replays or measure physical controller feel.
