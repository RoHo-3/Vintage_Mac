# Mac MD Write — Design Overview

Living record of design decisions. Pixel values assume the 512×342, 72 ppi,
1-bit screen of the compact Macs (1 pt = 1 px).

## 1. Platform

| Decision | Value |
|---|---|
| Hardware | Mac Plus, SE, SE/30 — single 68000 build, 32-bit clean |
| RAM target | 4 MB machine; ~1 MB app partition (768 KB minimum) |
| System | System 6 and 7. Use pre-FSSpec HFS calls; gate System 7 extras with `Gestalt` |
| Toolchain | Retro68 (GCC). No C standard library — Toolbox calls only |
| Perf baseline | Real Mac Plus (8 MHz 68000) |

## 2. Layout

- Full-screen borderless canvas. The system menu bar is hidden while writing
  and reappears when the cursor touches the top edge (so classic users can
  still find commands).
- Optional 20 px top strip: hamburger (16×16) left, document title centered,
  `[ Draft 2 ▾ ]` pill right. Shown on launch / mouse movement; hidden on
  keystroke.
- Text column: **420 px** wide, **46 px** margins each side ("balanced compact").
- Scroll position: 2 px hairline on the right edge instead of a scrollbar.
- Status footer (10 px): word count + saved state, fades in after ~2 s idle.

## 3. Typography

Writing canvas: monospace writing font (iA Writer Mono family, pending the
license/rename check — see fonts/README.md), hand-tuned bitmap strikes.

| Element | Size | Style |
|---|---|---|
| Body | 10 px | Regular — 6 px advance, ~70 chars/line |
| H6 | 10 px | Bold + all caps |
| H5 | 12 px | Bold |
| H4 | 12 px | Bold + underline |
| H3 | 14 px | Bold |
| H2 | 16 px | Bold |
| H1 | 20 px | Bold |

Strikes to tune: 10/12/14/16/20 px × Regular/Bold. Bold comes from the real
Bold font, never QuickDraw synthetic bold. Integer advance widths per size.

UI chrome uses a proportional bitmap face (Chicago 12 / Geneva 9) so the
interface is visually distinct from the prose.

## 4. Interaction patterns

- **Inline Markdown (WYSIWYM):** headings rendered at size as you type; syntax
  markers (`#`, `**`, `_`) visible only on the caret's line.
- **Hamburger drawer:** 180 px left drawer — chapters, drafts, export. 50%
  dither over the canvas behind it.
- **Command palette (Cmd+K):** ~260×170 centered card, substring filtering,
  shortcuts right-aligned.
- **Draft switcher pill:** popover to branch / switch / compare drafts (future).
- **Floating format pill** on selection: `[ B | I | H1 | Note ]`.
- **Find (Cmd+F):** slim inline bar, not a modal dialog.
- **No blocking alerts** for routine events; toasts instead.
- **Mouse and keyboard parity:** every action reachable both ways.

### Keyboard

| Keys | Action |
|---|---|
| Option+←/→ | Word left/right |
| Cmd+←/→ | Line start/end |
| Cmd+Delete | Delete to line start |
| Cmd+Z / Cmd+Shift+Z | Undo / redo (multi-level) |
| Cmd+K | Command palette |
| Cmd+\ | Toggle drawer |
| Cmd+Shift+D | Drafts |
| Cmd+F | Find |
| Cmd+S | Save now |
| Cmd+= / Cmd+- | Body 10 px ↔ 12 px |

## 5. Text engine

Custom gap buffer (not TextEdit, which caps at 32 KB). Budget ~384 KB text
buffer within the 1 MB partition, plus undo stack and fonts (<100 KB).

## 6. Saving

See [STORAGE.md](STORAGE.md). Summary:

- Autosave fires only when the user **pauses (~2 s)** *and* **≥250 weighted
  characters** changed — or the oldest unsaved change is **>3 min** old.
  Paste / selection-delete / heading change count as ≥50.
- Always save on quit, close, draft switch, suspend, and Cmd+S.
- Unsaved-changes dot in the top strip; "Saved" toast after each save.
