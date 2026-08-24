# Performance contract

Cubolor treats period-machine performance as a build requirement.

## Target

- CPU class: 486DX-50
- DOSBox-X deterministic approximation: `cputype=486`, `cycles=fixed 10000`
- Display: EGA 640×350×16 BIOS mode `10h` (`machine=ega`)
- Gameplay cadence: 50 Hz
- Required uncapped renderer throughput: at least 50.0 FPS

The cycle value is an emulator profile, not a claim that DOSBox-X precisely
models every 486 motherboard. It is fixed in source control so regressions are
repeatable. The executable also remains compatible with 386-class CPUs; slower
386 systems can run below the 50 Hz target without changing game logic.

## Gate

`CUBOLOR.EXE -benchmark` renders 200 animated frames, alternating between the
complete 5×5 field with its rotating outer ring and a true edge-pivot cube
roll. It turns off vertical synchronization, measures elapsed PIT channel 2 ticks at
1,193,182 Hz, and reports both total throughput and stage counters. The shell
gate rejects missing/zero counters and throughput below 50.0 FPS.

Run it with:

```sh
make perf-test
```

Recorded on 2026-08-24 with the pinned Open Watcom 2026-08-01 build and
DOSBox-X 2025.02.01:

```text
486DX-50 performance: PASS (66.9 fps at 10000 cycles; minimum 50.0)
Profile ticks: clear=342160 board=2297063 cube=1348466 hud=288999 present=1803
```

## Hot-path safeguards

- direct planar EGA writes; no chunky-to-planar conversion;
- BIOS only for standard mode entry/exit; rendering and page flips use direct
  EGA memory and port I/O;
- two 28,000-byte pages and byte-addressed CRTC page flips on 256 KiB EGA;
- per-page static HUD and target caches;
- precomputed projection and depth tables for all 40 animation angles;
- write-mode-2 color expansion;
- cached EGA bit-mask register state;
- explicit same-address latch read before every masked endpoint write;
- native one-pixel 5×7 HUD glyphs;
- 386 `rep stosb` full spans and page clears;
- fixed-point projection and edge walking;
- precomputed rolling-cube vertices, face visibility, and dirty redraw bounds;
- geometry-derived outer-platform dirty masks and clipped polygon spans;
- plane-batched black field outlines stored in a separate far-data segment;
- no dynamic allocation or asset decompression in the frame loop;
- deterministic native and target-side tests in CI.
