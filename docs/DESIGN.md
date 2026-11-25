# Mac MD Write — Design Overview

Living record of design decisions. Pixel values assume the 512×342, 72 ppi,
1-bit screen of the compact Macs (1 pt = 1 px).

## Principle: a quiet app

The app stays silent by default so writing stays distraction-free.

- Status is ambient, never announced: the save dot, the footer clock and
  other indicators appear only when the writer pauses, and change in place.
- Toasts are for truly important events only — most often ones the writer
  asked for (e.g. their Focus Timer finishing). Routine system events
  (saves, autosaves, recoveries that succeed) never toast.
- System-initiated messages are kept to the bare minimum; when one is
  unavoidable it is non-modal and never takes focus from the text.

## Principle: each surface has one job

- **The writing view** is for writing. Nothing else lives on the canvas.
- **The hamburger drawer is the document explorer:** it navigates the
  document you are in and the project it belongs to (project files, headings
  on this page, drafts). It exists only in the writing view.
- **The Library** manages files: new, open, duplicate, rename, export,
  trash. It has its own actions and no hamburger.

When a new feature arrives, it goes on the surface whose job it matches.

## Principle: one way to do a thing

- Every task has exactly one way to do it. No cluttered UI with many routes
  to the same result.
- If feedback shows the one way is wrong, we **change** it; we do not add a
  second way alongside it.
- If feedback is split (60/40, 50/50), we leave it alone until there is a
  necessary reason to change it.
- "One way" means **one path per input method**. With the mouse there is
  exactly one path to each task. Keyboard input (shortcuts, typed Markdown,
  the Cmd-K palette) is an interaction-support and accessibility layer for
  the same commands, not a second way. Example: the selection pill's **B**
  is the mouse path to bold; typing `**` or Cmd-B is the keyboard path.
- Within the keyboard layer, each command still has one shortcut (no
  aliases — e.g. Cmd-W does not duplicate Cmd-O).

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
- Optional 20 px top strip: hamburger (16×16) left, `Project › Document`
  title centered (just the document name for a standalone file).
  Nothing in the strip is a control besides the hamburger. Shown on launch / mouse movement; hidden on
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
- **Hamburger drawer = document explorer, built from blades.** The 180 px
  drawer is headed with the current document's name and lists:
  `<Project name> ›`, `Sections ›`, `Focus Timer ›`, `History ›`,
  `Export ›`, `Settings ›`, then `Library…` and `Quit`. A `›` row slides a
  full-height blade over the drawer; a 14 px strip of the panel underneath
  stays visible with `‹` as the back target (no vertical labels — QuickDraw
  can't rotate text). Max stack: drawer → Project → one folder, matching the
  three-level project rule. The drawer reopens to the last blade used, so
  switching files stays one click. Keys: ↑↓ move, → opens, ← / Esc back.
  The project row is hidden for standalone documents. Blade slide is a few frames
  on an SE/30, instant on a Plus. 50%
  dither over the canvas behind it.
- **Command palette (Cmd+K):** ~260×170 centered card, substring filtering,
  shortcuts right-aligned.
- **Floating format pill** on selection: `[ B | I | H1 | Note ]`.
- **Find (Cmd+F):** slim inline bar, not a modal dialog.
- **No blocking alerts** for routine events (see Principle: a quiet app).
- **Mouse and keyboard parity:** every command can be clicked, and frequent ones have a shortcut shown beside them (an accelerator, not a second way).

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

### Command palette (Cmd-K)

The palette lists **every command in the app, and nothing that exists only
there.** It is the keyboard layer over the same command set, and it doubles
as discoverable help: type what you want, see the command and its shortcut.

In code, one **command table** drives everything: each entry has a name,
search keywords, shortcut, the surface it lives on, and an availability
test (e.g. "Next section" only when the document has headings). Drawer
rows, shortcuts and the palette all read from it, so they cannot drift.

| Group | Commands |
|---|---|
| Navigate | Go to file… (current project) · Go to section… · Next / previous section · Library… · Find… · Find next |
| Write | Bold · Italic · Heading 1–6 · Note · Undo · Redo · Cut · Copy · Paste · Select all |
| Document | New document · Mark this version… · History… · Export as Markdown… · Export as template… · Export project as one file · Print… |
| Focus | Start Focus Timer (15/25/45/60/custom) · Stop Focus Timer |
| View | Text size 10 / 12 px · Show/hide session clock · Show/hide word count |
| App | About Mac MD Write · Quit |

### Session timer

The footer shows how long the current writing session has run (from document
open), e.g. `5:45`. It can be turned off in the drawer's Settings.

Naming: **Focus Timer** is the countdown you set; the **session clock** is
the always-counting-up time. One term each, used everywhere.

**Real-time updates, cheaply:** the footer is only visible while the user is
paused, so nothing is redrawn while typing. While visible, the idle branch
of the event loop compares `TickCount() / 60` with the last second drawn and,
when it changes, erases and redraws only the timer's own small rectangle
(~30×10 px) with `EraseRect` + `DrawString`. `WaitNextEvent`'s sleep is
capped at 30 ticks while the footer shows so the clock never lags more than
half a second. Cost: one tiny redraw per second.

**Focus Timer (drawer → Focus Timer ›, top level):**
- Sets a focus goal (e.g. 25 min); the footer clock then counts down
  instead of up.
- The session clock itself is turned on or off in Settings.
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

### Drawer blades

| Blade (row label) | Blade title | Contents |
|---|---|---|
| `<Project name> ›` | the project's name | Project documents (Outline pinned first) and folders › |
| `Sections ›` | Sections | Headings of the current document, indented by level |
| `Focus Timer ›` | Focus Timer | 15 · 25 · 45 · 60 min · Custom… · Off |
| `History ›` | History | One timeline of versions (see below) |
| `Export ›` | Export | Markdown… · Print… · Template… |
| `Settings ›` | Settings | App preferences: Session clock on/off, Word count on/off |

**History** is a single timeline per document. Every autosave checkpoint is
a version; `+ Mark this version…` names the current one (e.g. "Draft 2") so
it is easy to find and return to. Marked versions show bold with ◆.
Restoring a version never deletes anything newer: the restored text becomes
the newest version, so it can always be undone. (Parallel, separately
editable drafts — the Ellipsus "connected drafts" idea — are in the parking
lot.)

### One document at a time

The app keeps exactly one document open — a deliberate focus choice (it also
keeps the whole 1 MB partition available to one text buffer and its undo).

- **No Close command.** Opening another file from the Library autosaves the
  current one and switches; nothing ever asks "Save changes?".
- **Quit (Cmd-Q)** saves and exits.
- **On launch** the app reopens the last document at the caret where you
  left off; with no previous document it opens the Library.

### Naming documents

No naming step before writing. A new document (Cmd-N, or a template in the
Library) is named automatically from its first line:

- Markdown markers are stripped (`# The Keeper's Log` → `The Keeper's Log`),
  the name is cut at a word boundary to fit HFS's 31-character limit with
  `.md` (≤ 28 characters), and `:` (illegal in HFS) becomes `-`.
- A clash gets a number: `Day Two 2.md`.
- The name follows the first line until the writer renames the document in
  the Library; after a manual rename it stays fixed.
- Cmd-N inside a project creates the document in that project.
- A new project starts as "Untitled Project" with its name field already
  selected for typing, as in the Finder.

### Library (Cmd-O, drawer → Library…)

Full-screen browser that replaces the classic Open dialog and handles both
new and open. Three horizontally scrolling rows of 32×32 1-bit icons:

1. **Templates** — ordinary `.md` files in a Templates folder (users can
   add their own): Blank, Outline, Journal Entry, Short Story, Novel
   Project (a folder template: chapter files + notes), Article, Meeting
   Notes, Letter, Poem. `Open` on a template creates a new untitled document from it; the
   template itself is never changed (edit templates in the Finder — a
   power-user feature). Cmd-N skips the Library and opens Blank.
2. **My Files** — real folder hierarchy inside a "Mac MD Write" documents
   folder, read with HFS catalog calls (`PBGetCatInfo`). The row label is a
   breadcrumb (`MY FILES › THE KEEPER'S LOG`) with an up tile.
3. **Recent** — recently opened files.

**Project structure (max three levels):**

| Level | Can contain |
|---|---|
| My Files (top level) | Documents and projects |
| Project | Documents and folders |
| Folder inside a project | Documents only |

- `New Folder` is offered only inside a project.
- A document named "Outline" is pinned first in its project.
- Deeper folders created in the Finder are still shown (files never seem to
  vanish), but the app never creates them. This is a documented power-user
  feature: "Want folders of folders? Make them in the Finder."

**Export** on a project writes one combined `.md` (documents in project
order) — the way to assemble a manuscript. A destructive Merge command is
deliberately on hold; combining is covered by this export.

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

## Parking lot

Ideas kept for later, deliberately not built yet:

- **A document-level UX element.** The top strip's right side once held a
  "Draft 2 ▾" pill. It was repurposed (drafts → History blade), but that
  slot is reserved should we find a genuine need for a control that belongs
  to the document as a whole.
- **Merge documents.** On hold; Export project (one combined file) covers
  the main need.
- **Focus Timer celebration animation.** To be designed (see to-do list).
- **Parallel drafts** (Ellipsus-style connected drafts you can switch
  between and edit separately). Replaced for now by marked versions in a
  single History timeline.
- **Intent-aware help in the palette (future version).** A tiny local model
  that maps what the writer types ("make text bigger", "count words") to
  the right command — action-oriented help, not a conversation. On a 68000
  with 4 MB this means something small: a hand-built synonym/keyword table
  per command, possibly scored with a few-KB naive Bayes model, not a
  neural network. The command table's keywords field is the hook for it.
