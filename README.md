# Cubolor

Cubolor is a tiny isometric cube-rolling puzzle for MS-DOS. The cube begins on
the unpaintable outer ring of a 5×5 field. Roll around that ring freely to choose
an orientation, then enter the inner 3×3 canvas: every canvas square takes the
color of the cube face touching it. On entry, the ring drops away and cannot be
re-entered. Reproduce the target picture shown at the top right to finish.

The game is a 16-bit real-mode executable for a 386 or newer PC with EGA. It
renders directly into high-resolution planar 640×350×16 mode `10h` video memory
and uses two 28,000-byte EGA pages. A 256 KiB EGA card is required for the
flicker-free page pair. There are no runtime data files.

EGA mode `10h` has a 640×350 framebuffer but was designed for a 4:3 CRT. The
included DOSBox-X configuration enables aspect correction, so its 640×350
guest image is displayed in a 640×480 viewport rather than with square pixels.

## Playing

- Numpad `7`, `9`, `1`, `3`: roll up-left, up-right, down-left, or down-right
  exactly as seen on screen
- Numpad `4` or `6`: rotate the field 90 degrees left or right
- Numpad `8` or `2`: rotate the field 180 degrees counter-clockwise or clockwise
- `U`: undo
- `R`: restart the current puzzle
- `Esc`: quit to DOS

Movement keys keep their visible numpad directions after a field rotation; the
world direction is recalculated for the current view on every roll. Both cube
rolls and field turns animate over ten 50 Hz frames instead of jumping. The MVP
contains six hand-authored, provably reachable 3×3 target pictures. Border
movement neither paints nor increases the move counter. Undoing across the
first inward roll restores the ring.

Black one-pixel outlines separate the field cells and visible cube faces. They
remain visible throughout both cube rolls and animated camera quarter-turns.
The target is shown as a miniature isometric field and turns with the camera.

## Build and test

Install GNU Make, Bash, curl, unzip, a C++ compiler, and DOSBox-X.

On macOS (with [Homebrew](https://brew.sh/)):

```sh
xcode-select --install
brew install dosbox-x
```

On Debian/Ubuntu Linux:

```sh
sudo apt update
sudo apt install build-essential make bash curl unzip xz-utils
```

Then install [DOSBox-X](https://dosbox-x.com/) for your distribution and ensure
the `dosbox-x` command is on `PATH`. Build and run with:

```sh
make
make test
make run
make dist
```

The first build downloads a checksum-pinned Open Watcom v2 toolchain into
`.tools/`, following the cross-compilation approach used by
[Kolobok](https://github.com/ddanila/kolobok). Linux x86-64 and macOS arm64/x64
hosts are supported. `make dist` creates a ready-to-copy DOS directory.

`make test` includes:

- native simulation tests for cube orientation, camera-relative movement,
  undo, and every shipped solution;
- a DOS-native EGA framebuffer self-test;
- a deterministic render benchmark using DOSBox-X's fixed 10,000-cycle 486
  profile, with a hard minimum of 50.0 raw frames per second.

Gameplay is paced at 50 Hz by a restored-on-exit PIT interrupt. The benchmark
disables vertical synchronization and profiles clear, board, cube, HUD, and
page-flip stages separately. On the development profile, the MVP renders at
56.8 raw FPS; see [performance.md](docs/performance.md) for the method and
latest recorded figures.

Command-line diagnostics are `-selftest`, `-benchmark`, `-capture`,
`-capture-roll`, `-capture-turn`, and `-capture-title`. The capture commands
write `CUBOLOR.PPM`; the animation variants record the cube or field halfway
through an edge-pivoted roll or camera turn.

## Credits

- Danila Sukharev — technical assistance
- Nil Sukharev — idea and design
- GPT 5.6 Sol — implementation

## License

Code and original graphics are released under the [MIT License](LICENSE).
