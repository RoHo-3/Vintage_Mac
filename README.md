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

**Download a build:** every push is built by GitHub Actions with Retro68's
Docker image. Open the repo's **Actions** tab, pick the latest run, and
download the `MacMDWrite-…` artifact (`MacMDWrite.dsk` disk image and
`MacMDWrite.bin` MacBinary).

**Build locally** with the [Retro68](https://github.com/autc04/Retro68)
cross-compiler:

```sh
./build.sh          # assumes ~/Developer/Retro68-build; set RETRO68 to override
```

The disk image lands in `build/MacMDWrite.dsk`. Test in an emulator (Snow,
Mini vMac) and regularly on a real Mac Plus — the slowest target is the
performance baseline.

**Fonts:** `src/fonts.r` is generated from the TTFs in
`fonts/ibm-plex-mono/` by `python3 tools/make_fonts.py` (needs Pillow and
fontTools). It is committed, so a normal build doesn't need Python.

## Documentation

- [Design overview](docs/DESIGN.md)
- [Storage & journal format](docs/STORAGE.md)
- [Roadmap](docs/ROADMAP.md)
- [Fonts](fonts/README.md)

## License

Code: [BSD 3-Clause](LICENSE). Fonts are distributed under their own license — see
[fonts/README.md](fonts/README.md).
