# Mac MD Write

A distraction-free Markdown writing app for the classic compact Macintosh
(Plus, SE, SE/30) — built natively for the 512×342 1-bit screen, but designed
to feel like a modern writing app rather than a 1984 one.

> Status: early design and scaffolding. Nothing usable yet.

## Goals

- **Modern UX on vintage hardware.** Full-screen canvas, hidden chrome,
  hamburger drawer, Cmd+K command palette, non-blocking autosave with toast
  feedback, modern text-editing shortcuts.
- **Plain Markdown on disk.** Documents are ordinary `.md` files, rendered
  inline (WYSIWYM) while you write.
- **Crisp 1-bit typography.** Hand-tuned bitmap strikes of a monospace writing
  font at 10/12/14/16/20 px, embedded in the app.
- **Robust history.** An append-only journal records every change, enabling
  crash recovery now and version history / connected drafts later.

## Targets

| | |
|---|---|
| Hardware | Macintosh Plus, SE, SE/30 (68000 code; 32-bit clean) |
| RAM | 4 MB (app partition ~1 MB) |
| System | System 6 and System 7 |
| Screen | 512×342, 1-bit monochrome |

## Building

Requires the [Retro68](https://github.com/autc04/Retro68) cross-compiler.

```sh
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=<path-to-Retro68-build>/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake
make
```

The build produces `MacMDWrite.bin` (MacBinary), `MacMDWrite.dsk` (disk image)
and `MacMDWrite.APPL`. Test in an emulator (Mini vMac, Basilisk II) and
regularly on a real Mac Plus — the slowest target is the performance baseline.

## Documentation

- [Design overview](docs/DESIGN.md)
- [Storage & journal format](docs/STORAGE.md)
- [Fonts](fonts/README.md)

## License

Code: [BSD 3-Clause](LICENSE). Fonts are distributed under their own license — see
[fonts/README.md](fonts/README.md).
