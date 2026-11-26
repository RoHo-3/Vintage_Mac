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

## Principle: naming happens in place

Every naming task — renaming a file or project in the Library, naming a new
project, marking a version in History — works like renaming in the Finder:
the name becomes an editable field right where it sits, its text already
selected so typing replaces it. **Return** confirms, **Esc** cancels and
leaves things as they were. There are no naming dialogs or pop-ups.

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
  The only controls in the strip are the hamburger and, after a jump, the
  back chip (see Going back). Shown on launch / mouse movement; hidden on
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
  markers (`**`, `_`, `` ` ``) visible only in the paragraph holding the
  cursor (that paragraph re-wraps as they appear). A heading's `#` prefix
  never takes inline space: when shown it hangs in the left margin (every
  prefix up to `###### ` fits in 46 px), so headings never shift.
- **Hamburger drawer = document explorer, built from blades.** The 180 px
  drawer is headed with the current document's name and lists:
  `<Project name> ›`, `Sections ›`, `Links ›`, `Find ›`, `Focus Timer ›`, `History ›`,
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
- **Selection pill** (mouse selections only): see §4a.
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
search keywords (hand-coded synonyms, e.g. Stop Focus Timer also matches
"cancel", "end", "turn off" — so "cancel focus timer" finds it), shortcut, the surface it lives on, and an availability
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

### Find & Replace

A drawer blade (`Find ›`, Cmd-F), not a pill item: the pill only exists
with a selection, so it could not be the one mouse path to searching. If
text is selected when Find opens, the field is prefilled with it.

- Fields: **Find**, **Replace**. Options: Match case · Whole word. Scope:
  **This document / Project** (project-wide replace is safe — every
  document's History can undo it).
- **Results list** grouped by document, each with a short snippet; the
  current match is reversed. Clicking a result highlights it in the text
  and scrolls it into view (opening another document pushes the back stack).
- **Replace** (current match) · **Replace All**. Cmd-G next match.
- The writing is **not dimmed** while Find is open so matches stay visible;
  snippets carry the context for matches hidden behind the blade.

### Going back

Any jump pushes the place you left onto a **back stack**: following a link,
a backlink, a Links/Sections entry, or switching files in the project
blade. An entry is document + cursor position + scroll position, so going
back returns you exactly where you were — no closing, no Library. (The
current document is autosaved on every switch.)

- **Mouse:** a `‹ Day Two` chip appears in the top strip, right of the
  hamburger, naming where Back goes. Click to go back; repeat to walk
  further back. **Press and hold** the chip for a classic Mac pop-up menu
  of recent places (like holding a browser's Back button).
- **Keyboard:** Cmd-[ back, Cmd-] forward.
- The chip disappears when the stack is empty. Up to 20 places are kept for
  the session (a few hundred bytes); the stack is not saved on quit.
- Not to be confused with **History**, which is versions of a document.

## 4a. Selection pill and Markdown behavior

### The pill

Modeled on iPhone text selection: a multi-level pill that drills down like
the drawer's blades. The old Mac has no right-click or context menus; the
pill is this app's context menu.

| Level | Contents | Acts on |
|---|---|---|
| 1 | Cut · Copy · Paste · Format › | the selection |
| Format | ‹ · B · I · S · Code · Link · Note · Paragraph › | the selected text |
| Paragraph (two rows) | ‹ beside: row 1 H1–H6 · row 2 • List · 1. List · ☐ Task · “ Quote | the whole paragraph(s) |

- **Shape:** a single row, except Paragraph, which stacks two rows
  (headings, then blocks) to keep the pill compact (~200 px wide).
- **Placement:** centered just above the selection, clamped to the text
  column; flips below when the selection is near the top of the screen.
- **Timing:** appears on mouse-up (never during the drag); disappears on
  typing, clicking elsewhere, or Esc.
- **Mouse selections only.** Keyboard selections (Shift-arrows) never show
  the pill — the keyboard path is shortcuts and the Cmd-K palette.
- **Not supported:** superscript and subscript (not standard Markdown, so
  they would not round-trip, and unreadable at 10 px). Footnotes are parked.

### Formatting rules

- **Paragraph formats always apply to the whole paragraph** containing the
  selection; they never split a paragraph or insert line breaks. Selecting
  two words and choosing H2 makes the whole paragraph an H2 (Cmd-Z undoes).
- A selection spanning several paragraphs formats each one (three lines →
  three list items).
- Inline formats crossing a paragraph break are applied within each
  paragraph separately (Markdown emphasis cannot span blocks).
- Toggling: if all of the selection already has the format, the button
  removes it; if only part does, it applies to all.

### Keyboard path (shortcuts; everything is also in Cmd-K)

| Format | Keys | Paragraph | Keys |
|---|---|---|---|
| Bold | Cmd-B | Heading 1–6 | Cmd-Opt-1 … 6 |
| Italic | Cmd-I | Normal paragraph | Cmd-Opt-0 |
| Strikethrough | Cmd-Shift-X | Bulleted list | Cmd-Shift-8 |
| Code | Cmd-E | Numbered list | Cmd-Shift-7 |
| Link… | Cmd-L | Task list | Cmd-Shift-9 |
| Note | Cmd-Opt-M | Quote | Cmd-Shift-' |

Modeled on Google Docs where possible. Cmd-K stays the palette, so Link is
Cmd-L; Cmd-E is Code, so Export as Markdown has no shortcut (it is rare and
lives in the drawer and palette). Every shortcut works on the Mac Plus
keyboard (Command and Option, no Control needed).

### Notes

Notes to self, stored as HTML comments: `<!-- note: … -->` (hidden by
other Markdown viewers, visible in plain-text editors). Choosing Note
inserts one right after the selection.

- **Closed:** text hidden; an 8×8 speech-bubble mark sits in the right
  margin beside the line.
- **Open:** click the mark, or put the cursor on that line. The note opens
  inline below the line in a dotted-border box set in the 9 px UI font —
  border and typeface separate it from the prose without gray. The mark
  fills black while open.
- Notes never count toward the word count.

### Images

The app never loads or decodes images (no practical PNG/JPEG decoding on a
68000; color photos don't survive 1-bit anyway).

- An image line shows as a two-line dotted placeholder: picture glyph, alt
  text, filename. On the cursor line the raw `![alt](path)` shows instead.
- Image lines are saved byte-for-byte, so images added on a modern machine
  survive. No Insert Image command in v1 (type the syntax by hand).
- **Links whose target is an image or any non-Markdown file** render as
  links; their pill offers Copy Path instead of Open.
- The Library and the project blade show non-Markdown files (images, PDFs)
  dimmed with a generic icon: visible so nothing seems missing, not
  openable. Renaming or trashing them stays in the Finder.

### Links

- Targets: any document, or any section of any document, **in the same
  project** — written as standard relative links
  (`[the letter](Day%20Two.md#the-letter)`) so they work in GitHub,
  Obsidian, iA Writer. No `[[wiki-links]]`.
- **Creating:** Link (pill or Cmd-L) opens the drawer in **Link to…** mode
  over the project: clicking a document's name links to the document; its
  › opens its sections (plus "Whole document"). Esc cancels. One navigator
  for moving around and for linking.
- **Appearance:** dotted underline (H4 uses a solid underline).
- **Following:** clicking a link places the cursor in it and shows the
  link pill: Open · Edit Link · Remove Link. Web addresses offer Copy
  instead of Open (no browser).
- **Renames:** renaming in the Library rewrites links to that document
  across the project. A link broken by a rename made elsewhere shows a
  small "missing" mark.

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
| `Links ›` | Links | Links in this document (target, ⚠ if broken, web links marked) and **Linking here** — backlinks from other project documents. Clicking goes there; editing/removing stays in the link pill |
| `Find ›` | Find | Find & Replace (see below); opened by the row or Cmd-F, prefilled with the selection if there is one |
| `Focus Timer ›` | Focus Timer | 15 · 25 · 45 · 60 min · Custom… · Off |
| `History ›` | History | One timeline of versions (see below) |
| `Export ›` | Export | Markdown… · Print… · Template… |
| `Settings ›` | Settings | App preferences: Session clock on/off, Word count on/off |

**History** is a single timeline per document. Every autosave checkpoint is
a version, stamped with date and time by the system. `+ Mark this version…`
lets the writer name the current one: an inline field appears at the top of
the blade, prefilled with the next "Draft N" and selected, so typing replaces
it — Return marks, Esc cancels (nothing is marked). The name is stored as
the checkpoint's label in the journal (STORAGE.md). Marked versions show
bold with ◆ beside their time.
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
   add their own). Default set (by 0.05 alpha): Blank, Outline, Research,
   Meeting Notes, Article, Short Story, Novel Project (a folder template:
   chapter files + notes), Script. `Open` on a template creates a new untitled document from it; the
   template itself is never changed (edit templates in the Finder — a
   power-user feature). Cmd-N skips the Library and opens Blank.
2. **My Stuff** — projects (as folders) and loose documents side by side.
   On disk, a "Mac MD Write" folder next to the app holds three peers:
   `My Documents` (loose documents), `My Projects` (project folders) and
   `Templates`, plus the prefs file; the shelf shows the first two together,
   read with HFS catalog calls (`PBGetCatInfo`). Inside a project the row
   label is a breadcrumb (`MY STUFF › THE KEEPER'S LOG`) with an up tile.
   New Project goes to My Projects; Cmd-N goes to My Documents, or to the
   project you are working in.
3. **Recent** — recently opened files.

**Project structure (max three levels):**

| Level | Can contain |
|---|---|
| My Stuff (top level) | Documents (My Documents) and projects (My Projects) |
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
- **Writing support: spell-check and grammar.** Future consideration. A
  spell-checker is feasible on 4 MB if the word list is compressed
  (a common-words dictionary of a few hundred KB, checked on pause, never
  while typing, and shown quietly — e.g. a dotted underline). Grammar
  checking is much heavier and likely out of reach on a 68000.
- **Footnotes** (`[^1]`), the main reason writers want superscript.
- **1-bit image previews:** a dithered thumbnail instead of the placeholder.
- **Insert Image command** (v1 only shows images that are already linked).
- **Focus Timer celebration animation.** To be designed (see to-do list).
- **Parallel drafts** (Ellipsus-style connected drafts you can switch
  between and edit separately). Replaced for now by marked versions in a
  single History timeline.
- **Intent-aware help in the palette (future version).** A tiny local model
  that maps what the writer types ("make text bigger", "count words") to
  the right command — action-oriented help, not a conversation. On a 68000
  with 4 MB this means something small: a hand-built synonym/keyword table
  per command, possibly scored with a few-KB naive Bayes model, not a
  neural network. Version 1 ships the hand-coded synonym table; the
  command table's keywords field is the hook for anything smarter.

## Questions for beta users

Decisions made now that real-user feedback should confirm or overturn
(per "one way to do a thing": change the one way, never add a second).

- **Where does Quit live?** Currently in the drawer (Quit, Cmd-Q). The
  classic menu bar shows only the Apple menu (About, desk accessories), so
  Quit has exactly one mouse path. Ask: do long-time Mac users look for a
  File › Quit menu first?
- **Formatting shortcuts.** Modeled on Google Docs (Cmd-Opt-1…6 headings,
  Cmd-Shift-7/8/9 lists). Ask: do writers expect other conventions
  (e.g. iA Writer or Word)?
