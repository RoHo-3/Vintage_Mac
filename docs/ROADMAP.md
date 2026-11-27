# Roadmap

## 0.01 alpha — "write in it"

1. **Text engine** — gap buffer, 70-column word wrap (incremental re-wrap),
   cursor, mouse and Shift selection, double-click word select, modern
   movement keys, multi-level undo/redo, Cut/Copy/Paste via the Mac
   clipboard. ✅ first build
2. **Fonts** — MDW Mono (bitmap IBM Plex Mono) 10/12/14/16/20 px, Regular
   and Bold, built into the app. ✅ first build
3. **Live Markdown** — heading sizes, bold/italic/code, markers hidden
   outside the cursor's paragraph; heading # signs hang in the left
   margin. Flicker-free line drawing (offscreen strip). ✅ build 2
4. **Files**
   - 4a. Open/save `.md` (Cmd-O / Cmd-S / Cmd-N) with an exact round trip:
     UTF-8 ↔ MacRoman, original line endings and BOM, unshowable characters
     kept as escapes (fuzz-tested byte-for-byte). Autosave after a 2 s pause
     once 250 weighted characters changed or 3 min passed; save on suspend
     and quit. Classic Open/Save dialogs stand in for the Library. ✅ build 3
   - 4b. Append-only journal (`<name>.hist`): autosave appends one
     small record; the .md is rewritten on Cmd-S, quit, suspend and
     switching documents. Opening replays it: work lost in a crash is
     restored quietly; a file edited on another computer becomes a new
     segment. Fuzz-tested with 300 simulated crashes. ✅ build 5
5. **Quiet chrome** — hidden menu bar (restored on suspend and quit), top
   strip (hamburger, title) and footer (word count / session clock, save
   dot: ring, dithered, solid) shown on pause or mouse move, hidden on the
   next keystroke; the clock redraws only its own rectangle. ✅ build 4
   (the hamburger does nothing until the drawer arrives)

## 0.02 alpha — "find your work"

1. **Files** — "Writings" folder beside the app with My Documents, My
   Projects and Templates; names from the first sentence (Title Case, no
   Markdown or Windows-illegal characters); reopen the last document at its
   caret; journals invisible; orphaned journals to the Trash. ✅ builds 29-55
2. **Library** (Cmd-O) — Templates / My Stuff / Recent shelves, projects,
   rename in place, duplicate and new documents through the Save dialog,
   Move to Trash, Other... ✅ builds 34-53
3. **Fonts** — one font and size for the whole app (body, headings, bold
   and italic follow it, at real bitmap sizes).
   - 3a. Font switcher with fixed-width fonts: MDW Mono, Monaco, Courier.
     A font & size pill at the right of the title strip (click the name to
     cycle fonts, the size to step it; Option-click steps down), plus
     Cmd-Option-F and Cmd-= / Cmd--; remembered in the prefs. Moves into the
     Settings blade with the drawer. ✅ build 57
   - 3b. Proportional layout (lines measured per character) so New York,
     Geneva, Times, Helvetica, Palatino can be tried; inline code is drawn in
     Monaco. ✅ build 60
4. **Drawer** — project blade, Sections (jump to heading), Settings (font,
   size, word count, session clock, Show Markdown for this document),
   Library... and Quit rows; Cmd-\ toggles. ✅ build 67 (Cmd-M still works
   as a shortcut). With it:
   - Formatting keys: Cmd-B bold, Cmd-I italic, Cmd-E inline code (Cmd-K
     stays the command palette); they wrap or unwrap the selection, or turn
     the style on for what is typed next.
   - Copy keeps Markdown balanced: a selection that cuts through a styled
     phrase copies with the missing markers added.

## 0.03 alpha — Markdown coverage (done, builds 78-83)

- Strikethrough (\~\~text\~\~), drawn by hand (QuickDraw has no strike
  style). Spaces next to markers follow standard Markdown (`~~ b ~~` and
  `** b **` stay plain); closers pair with the nearest opener.
- Block quotes (bars at the left, three deep), lists (bullets, numbers,
  task boxes; Return continues, Return on an empty item ends or outdents,
  Tab / Shift-Tab nest under the parent's text), horizontal rules, fenced
  code blocks, setext headings, hard line breaks, backslash escapes.
- Paragraph formats on Cmd-Opt-0..6, Cmd-Shift-8/7/9/'.
- Selection pill (Cut · Copy · Paste · Format › B I S Code · Paragraph ›)
  and the Cmd-K palette with synonyms, both from one command table. Link
  and Note join the pill when those features are built.
- Tabled: **tables and footnotes**, which need special handling at
  512 x 342. First research how other editors render them, looking for
  permissively licensed (BSD/MIT) solutions we may reuse.

## 0.04 alpha — Library: every writing is a folder (Robert's design)

- **No project/document choice up front.** Every writing is a folder
  holding its documents, so a piece can grow into a project later without
  moving anything.
- **My Writings** replaces My Stuff (and the My Documents / My Projects
  folders). Its first tile, a + named **New Writing**, opens a blank page;
  the first save (Cmd-S, or quiet first-line naming) makes the folder and
  its first document, one name for both, and the folder follows the first
  line with its document while app-named.
- Loose documents found in My Writings at launch are wrapped in a folder of
  their own name (journal included).
- Action bar: double-click opens (folders in the Library, documents for
  writing); Duplicate (folders too) · Rename · Move to Trash; New Document
  and New Folder inside a writing; the **Import…** pill copies a .md from
  anywhere into a new writing with its history.
- Cmd-S shows the title strip for ~2 s, so the name used is seen; Import
  too.
- **Settings › Move Library…:** documents, invisible histories, prefs and
  templates move together (same disk: one move; another disk: copied, then
  the original to the Trash).

## Before 0.05: quality pass

- Product Guide: a complete guide to the app, with a "This build adds..."
  section kept current build by build toward 0.05's code complete.
- Library polish (done locally): lighter + and up symbols, one click on
  + and up tiles, "New" instead of "New Writing", one 26-character name
  limit cut at whole words (Rename stops there too).
- Settings blade in three groups (App, Library, This document); **Back Up
  Library…** copies the whole library, dated, to any folder or disk (done
  locally). Later, perhaps: a "Use a Backup…" to switch to one.
- Mac-only paths to verify: Move Library on one disk, Duplicate folder,
  loose documents wrapped at launch, System 6 (checklist sent with 92).

## Performance passes (scheduled after 0.04 feedback)

Launch got slower around builds 81-90. Before changing anything, measure:

1. **Measure.** A timing build that records ticks for each launch phase
   (Files_Init, wrapping loose documents, tidying journals, prefs, reopening
   the last document and its journal, first layout, first draw) and shows
   them, so we fix what's actually slow on a 68000.
2. **Parse and layout.** The 0.03 block rules look at neighbouring lines;
   the cheap tests now run first (done). Next: cache each line's block
   info instead of re-deriving it, and re-wrap only the lines an edit can
   change (today: one paragraph either side, always).
3. **File system at launch.** Fewer catalog calls: list My Writings once
   and share it between wrapping loose documents, tidying journals and the
   Library; skip the journal sweep when nothing changed since last quit.
4. **Drawing.** Measure the first full draw; avoid parsing a paragraph
   twice for layout and then for drawing.

## Later alphas

- Drawer blades: Links, Find, Focus Timer, History, Export
- Notes, image placeholders, links and back stack
- **Default templates — no later than 0.05 alpha:** Blank, Outline,
  Research, Meeting Notes, Article, Short Story, Novel Project, Script
  (structures designed together; see to-do list)

## Readying for release

- Release disk images: an app disk in Disk Copy 4.2 format (writable to a
  real 800K floppy) and a BlueSCSI image. App only: no Apple System
  software is redistributed.
- A "Start Here" guide shipped as a real Markdown document in My
  Documents, replacing the first-run welcome text.
- Full System 6.0.8 test pass; real-hardware pass (Plus, SE, SE/30).

## Known limits in the first build

- A new document needs one Save (dialog) before autosave takes over; the
  Library will remove that step.
- Mixed line endings in one file are normalized to the first one found.
- Tab inserts four spaces; tab characters in pasted text show as a box.
- Fonts are automatic rasterizations, not hand-tuned.
- H6 uses capitals as a stand-in for small caps; testing focuses on System
  7.1 until a full System 6.0.8 pass later.

## Future exploration

- **Cross-device `.hist` (old Mac ↔ new Mac).** Journals sit beside their
  documents, flagged invisible for the classic Finder; modern macOS ignores
  the flag, and Finder-level moves leave journals behind (cleaned up on
  launch). If a modern-Mac version is built, decide then how history
  travels (e.g. a sidecar folder, extended attributes, or a sync format).
  Until then: nothing in the file format may preclude it. Documents are plain
  UTF-8 Markdown; the journal format is documented in STORAGE.md.

- **Find existing .md files (migration).** A Library affordance that searches
  the Mac's disks for Markdown files made elsewhere and copies them into
  My Writings (each in a writing of its own, like Import), so moving to the app is one step. Copies, never
  moves; skips Writings itself and the Trash; shows what it found before
  copying. Classic search: PBCatSearch on HFS volumes that support it,
  otherwise a recursive catalog walk.
