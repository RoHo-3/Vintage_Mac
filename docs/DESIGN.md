# Mac MD Write — Design Overview

Living record of design decisions. Pixel values assume the 512×342, 72 ppi,
1-bit screen of the compact Macs (1 pt = 1 px).

## Principle: a quiet app

The app stays silent by default so writing stays distraction-free.

- Status is ambient, never announced: the save dot, the footer clock and
  other indicators appear only when the writer pauses, and change in place.
- Toasts are for truly important events only — most often ones the writer
  asked for (e.g. their writing timer finishing). Routine system events
  (saves, autosaves, recoveries that succeed) never toast.
- System-initiated messages are kept to the bare minimum; when one is
  unavoidable it is non-modal and never takes focus from the text.

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
- Status footer (10 px): fades in after ~2 s idle. Left: word count and
  session time, separated by a slash — `1,284 words / 5:45`. Right: the
  6 px save dot (see Save state).

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
- **Hamburger drawer:** 180 px left drawer — File (`Files…`, `Quit`), chapters, drafts, export, settings (session timer, writing timer). 50%
  dither over the canvas behind it.
- **Command palette (Cmd+K):** ~260×170 centered card, substring filtering,
  shortcuts right-aligned.
- **Draft switcher pill:** popover to branch / switch / compare drafts (future).
- **Floating format pill** on selection: `[ B | I | H1 | Note ]`.
- **Find (Cmd+F):** slim inline bar, not a modal dialog.
- **No blocking alerts** for routine events (see Principle: a quiet app).
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

### Session timer

The footer shows how long the current writing session has run (from document
open), e.g. `5:45`. It can be turned off in the drawer's Settings.

**Real-time updates, cheaply:** the footer is only visible while the user is
paused, so nothing is redrawn while typing. While visible, the idle branch
of the event loop compares `TickCount() / 60` with the last second drawn and,
when it changes, erases and redraws only the timer's own small rectangle
(~30×10 px) with `EraseRect` + `DrawString`. `WaitNextEvent`'s sleep is
capped at 30 ticks while the footer shows so the clock never lags more than
half a second. Cost: one tiny redraw per second.

**Writing timer (drawer → Settings):**
- `Session timer` checkbox turns the footer clock on or off.
- When on, `Set writing timer…` sets a goal (e.g. 25 min). The footer clock
  then counts down instead of up.
- When the countdown reaches zero, the app celebrates with a brief,
  non-blocking 1-bit animation and a toast; it never interrupts typing or
  forces a dialog. (Celebration animation: still to be designed.)

### Save state

A 6 px dot at the footer's bottom right replaces any save text:

| State | Dot | When |
|---|---|---|
| Unsaved changes | empty ring | after any edit not yet journaled |
| Saving | dithered (50% checkerboard) | drawn just before the write starts |
| Saved | solid | drawn once the write completes |

No progress bar: each save is one blocking `FSWrite`, so the screen cannot
redraw mid-write and a bar would only flash. The dithered dot is an honest
"saving" signal and stays visible during slower full `.md` rewrites.

### One document at a time

The app keeps exactly one document open — a deliberate focus choice (it also
keeps the whole 1 MB partition available to one text buffer and its undo).

- **No Close command.** Opening another file from the Library autosaves the
  current one and switches; nothing ever asks "Save changes?".
- **Quit (Cmd-Q)** saves and exits.
- **Cmd-W** opens the Library (muscle-memory alias, not a close).
- **On launch** the app reopens the last document at the caret where you
  left off; with no previous document it opens the Library.

### Library (Cmd-O, drawer → Files…)

Full-screen browser that replaces the classic Open dialog and handles both
new and open. Three horizontally scrolling rows of 32×32 1-bit icons:

1. **Templates** — ordinary `.md` files in a Templates folder (users can
   add their own): Blank, Outline, Journal Entry, Short Story, Novel
   Project (a folder template: chapter files + notes), Article, Meeting
   Notes, Letter, Poem. Cmd-N skips the Library and opens Blank.
2. **My Files** — real folder hierarchy inside a "Mac MD Write" documents
   folder, read with HFS catalog calls (`PBGetCatInfo`). The row label is a
   breadcrumb (`MY FILES › THE KEEPER'S LOG`) with an up tile.
3. **Recent** — recently opened files.

`Other location…` falls back to the classic Standard File dialog for files
elsewhere on disk. Return opens, Esc returns to writing.

## 5. Text engine

Custom gap buffer (not TextEdit, which caps at 32 KB). Budget ~384 KB text
buffer within the 1 MB partition, plus undo stack and fonts (<100 KB).

## 6. Saving

See [STORAGE.md](STORAGE.md). Summary:

- Autosave fires only when the user **pauses (~2 s)** *and* **≥250 weighted
  characters** changed — or the oldest unsaved change is **>3 min** old.
  Paste / selection-delete / heading change count as ≥50.
- Always save on quit, close, draft switch, suspend, and Cmd+S.
- Save state shown only by the footer's three-state dot (see §4, Save state) — no "Saved" toast.
