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

## 0.03 alpha — Markdown coverage (done, builds 78-87)

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

## 0.04 alpha — Library: every writing is a folder (done, build 99)

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
  locally). No "Use a Backup…" button: History covers getting a version
  back, and restoring a whole library is rare, so the Product Guide
  explains it instead (release readiness).
- Mac-only paths (Move Library, Duplicate folder, loose documents wrapped
  at launch, Import/Open from another folder, System 6): **verified by
  Robert in the 92 pass.** Fixes that came from it: Import/Open no longer
  bombs (the 384 KB scratch buffer, then a journal snapshot written into
  an edit-sized buffer), Standard File's port restored, working
  directories closed after Move Library so System 6 can empty the Trash.
  Re-check these in the release readiness pass, as files gained invisible
  companions since (Writing Kind).
- **Safe saves (from the Neo review; done, build 103):** saving rewrites the document in
  place, so on a nearly full floppy a longer version can stop part way
  and leave the new start joined to the old end. The journal still has
  the text, but the .md that travels to a modern Mac would be damaged.
  Fix: grow the file to its new length first (SetEOF, checking for
  dskFulErr) and write only if that succeeds; say "Disk is full" quietly
  in the strip and keep the old file whole.

## Performance passes (done, builds 93-99)

Measured on an emulated SE with System 6.0.8, Product Guide open:
6.1 s → **1.1 s** from a hard disk, 5.0 s from a floppy (the rest there is
the floppy reading font resources). Untitled page: 4.4 s → 1.6 s from a
floppy. Changes: text sizes measured when first used; fixed-width fonts
fill width tables in one step; launch draws once; journals replay from the
last snapshot; re-layout reaches neighbours only around underlines; the
palette works its labels out once. Later, only if needed on floppies:
smaller heading strikes (H1 18 pt instead of 20 saves ~3 KB per style).

### The plan, as scheduled


Launch got slower around builds 81-90. Before changing anything, measure:

1. **Measure.** Every launch records ticks for each phase (Cmd-K > Launch Timing shows them)
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
- **Focus goal: N minutes or N words (decided: both count up, "12 of 25
  min" like "312 of 500 new words"; keep the two the same way round) (one milestone with the Focus
  Timer; first version done, build 104: Cmd-K > Focus for Minutes... /
  Focus for Words... / Clear Focus Goal).** One place to set a session goal, either a timer or a word
  count. The quiet footer shows progress beside the word count and the
  session clock (for words: "312 / 500"), only when a goal is set. Reaching
  either goal gives one small celebration, then the goal clears.
  The word count already exists, so the word goal is little extra work;
  it moves people toward finishing a piece. To settle: what the
  celebration looks like while staying quiet (a strip message and a
  brief footer flash, no sound by default). Decided: a word goal counts
  words added in this session, not the document's total.
- **History: see and recover past versions (needed before v1; first
  version done, build 105: drawer > History, Cmd-K > History...; one entry
  per sitting plus named moments, up to 60; picking one opens a copy
  beside the document. Still to do: step back and forward through every
  version in place, read-only).** Every
  document already keeps a journal (`.hist`: snapshots, edits and
  checkpoints), but since 0.04 nothing shows it. Add a History blade (and
  Cmd-K › History) that lists this document's past versions, newest
  first, by day and time, with the checkpoints that have names
  ("Imported", a save, a session's end). Picking one shows it read-only
  in place, with keys to step back and forward one version at a time,
  an unlimited back stack through the whole journal. Restore never
  overwrites: it makes a new document beside the current one, named
  "Name (Nov 27, 4:24)", so no words are ever lost. Open questions: how
  far back to keep on a floppy (compacting old edits into snapshots),
  and memory for replaying a long journal on a 4 MB Mac.
- **From the Neo review** (docs: "Neo by Hugh Howey — Lessons for Mac
  MD Write"):
  - *No last-write-wins* when moving writings between the vintage Mac
    and a modern one: if only one side changed, take it; if both
    changed, keep both, the other as "Name (from other Mac)". Part of
    the file-transfer work (BlueSCSI with Wi-Fi).
  - *Placeholders (done, build 103: Cmd-K > Insert TK, Next TK; the
    drawer dot is still to do):* a standard `TK` mark ("to come") for a fact or name
    to fill in later; Cmd-K › Next TK jumps to it; the drawer shows a dot
    beside documents that still have one. Plain text, no Markdown change.
  - *Cuts (Neo's "Darlings"; done, build 108):* Cmd-K > Move to Cuts
    appends the selection to `Cuts.md` beside the document, under "## From
    <document>, Nov 27 16.24", then removes it (undo brings it back).
    Cuts' own History marks that change "Edited outside the app", the label
    now used for any change made outside the app.
  - *Footnote gesture (editing half done, build 107: Cmd-K > Insert
    Footnote, Jump to Footnote; marks show as plain text until their look
    is designed),* for the footnotes item: one key adds `[^n]` and
    opens its note at the bottom; Enter goes back to the sentence; marks
    shown as numbers in order on screen, never renumbered in the file.
  - *Smart typography,* polish if asked: curly quotes, em dash and
    ellipsis as you type (all in Mac Roman, round-trip to UTF-8). Off by
    default: it changes the Markdown source.
  - *Feature bar,* adopted now: a change belongs if it helps someone
    finish a piece; one that adds a panel, a prompt or a dependency
    usually does not.
- Notes, image placeholders, links and back stack
- **Default templates — 0.05 alpha:** six, one shelf with no scrolling,
  alphabetical: Article, Book Project (Outline; Chapters/Chapter 1;
  Notes/People & Characters, Places, Research), Essay, Journal, Script
  (Fountain, saved as .md, opens with a `#` section line), Short Story.
  Blank leaves the shelf (New already makes one). Each opens with a `#`
  name that reads well if kept ("Untitled Story"), short labelled prompts,
  then "Start here." Parked: Poem, Meeting Notes, a chosen shelf order.
- **Library ideation — end of 0.05, before the next polish round:** is a
  shelf of big tiles the right picture for the Library, and for adding?
  Up, New Document and New Folder might read better as three small text
  rows at the left, closer to Finder's list sizes and names, keeping them
  beside the writing's files so the layout still shows how writings are
  built. Explore with sketches before any code.

## Polish (to decide with real use)

- **Does a name follow the first line?** Today: it follows while the app
  chose the name (quiet naming, New, templates) and stops once a person
  names it (Cmd-S dialog, Rename, Duplicate's dialog, Import, the Product
  Guide). Logical, but the state is invisible and may confuse. Options:
  (a) always follows, like Apple Notes / Bear (names chosen elsewhere
  would change too); (b) named once at the first save, like TextEdit /
  Pages; (c) today's rule, made visible (a mark in the title strip or
  drawer when the name follows). Decide after writing real pieces with it.
  Also tabled: how a template's "Untitled" name prompts a rename (in the
  document or in the Library), and whether Library Rename stays at all;
  it is today the only way to rename a folder such as a Book Project.
- **Fountain files:** import and export of .fountain wait until moving
  files between vintage and modern Macs is solved (likely a BlueSCSI with
  Wi-Fi). Then a title page can be built from the script's `#` line.
- **Footnotes before tables** (templates parking lot).
- **Template guides (P2, not on the v1 path):** each template carries one
  line linking to a short best-practice guide in the Product Guide folder
  (one per template or style: inverted pyramid, persuasive vs informative
  essay, Fountain lines), each guide listing outside reading such as
  Wikipedia. Needs link following (Later alphas). Note: a relative link
  copied into a writing breaks once the file moves to another Mac without
  the Guide; consider linking by name and resolving inside the app.

## Readying for release

- Release disk images: an app disk in Disk Copy 4.2 format (writable to a
  real 800K floppy) and a BlueSCSI image. App only: no Apple System
  software is redistributed.
- A "Start Here" guide shipped as a real Markdown document in My
  Documents, replacing the first-run welcome text.
- Full System 6.0.8 test pass; real-hardware pass (Plus, SE, SE/30).
- **Product Guide editorial pass:** one read-through of the whole guide
  for voice, length and order, now that it has grown section by section
  (templates, links, History, Focus, TK, footnotes, Cuts), bringing in
  what it doesn't cover yet (Cmd-click links, writing icons, Recent's
  writing names and path, Focus starting on the first keystroke, any
  writing holding more documents). Keep it short enough to read in one
  sitting. Add **Restoring a backup** (no button for it): quit the app;
  drag the current Writings folder to the Trash, and don't empty it yet
  (the app looks for its library where Move Library last put it, and
  skips a folder in the Trash; a renamed folder would still be found);
  copy the backup folder beside the app and rename it exactly
  "Writings"; open the app. Settings, histories, templates and writing
  icons come back as they were on the backup's date; Recent forgets
  anything newer. To keep the library elsewhere, use Move Library
  afterward. Empty the Trash once all is well.
- **System 7 full evaluation pass** (day-to-day testing and feedback are
  on System 6.0.8, the more limited system; 7.1 is re-validated here).
  Work found or held for it:
  - **Writing icons in the Finder (System 7 only):** give each writing's
    folder its Library icon (Book, Essay, Manuscript...) as a custom
    folder icon: an invisible "Icon" file inside it holding the icon,
    plus the folder's has-custom-icon flag, added when the writing is
    made and once for older writings. System 6 has no custom folder
    icons, so writings stay plain folders there (expected, build 128).
- **Web links through FrogFind (Robert tests, BlueSCSI Wi-Fi Mac):** check
  the template "How To Write Great" pages read well on 512x342 through
  `http://frogfind.com/read.php?a=<url>` (start with the Short Story and
  Essay links). If they do, web links in the app open wrapped that way
  on the vintage Mac (files keep the plain https link; the proxy is a
  setting). Today a web link says it opens on a modern computer. Fallbacks
  to note: 68k.news, Wiby. FrogFind is GPL-3.0: link to it, never copy it.

## Known limits in the first build

- Mixed line endings in one file are normalized to the first one found.
- Tab inserts four spaces; tab characters in pasted text show as a box.
- Fonts are automatic rasterizations, not hand-tuned.
- H6 uses capitals as a stand-in for small caps.
- Testing and feedback happen on System 6.0.8, the more limited system;
  System 7.1 is re-validated in the release readiness pass.

## Library: A2 chosen (first build: 111)

- A2 chosen (B ruled out: it loses the quiet look; C not taken). Built
  in 111: everything below, to check on System 6 and 7.
- Recent hides only while a writing is open in My Writings, which then
  takes two rows of tiles; it comes back at the top level. A classic
  zoom-outline animation, not a slide.
- Scrolling: a 2-pixel hairline at the right (DESIGN.md's "scroll
  position" spec), one line of tiles at a time, replacing the sideways
  arrows on My Writings; click above or below the mark to move a line.
  The writing view doesn't draw its hairline yet: to add.
- The lines between shelves stay.
- Shelf order flipped: Recent, My Writings, Templates (top to bottom).
  Every template also has a Cmd-K command (New Article, New Book
  Project, ...) and the Product Guide lists them, so templates stay one
  step away at the bottom.
- The path is clickable too (local, after 111): each name in "MY
  WRITINGS > BOOK PROJECT > CHAPTERS" goes back to that level, lit while
  pressed; Cmd-Up goes up one (System 7's Finder key). The Up tile stays
  as the obvious way, so nobody gets lost; the folder just left is
  picked on arrival. Revisit the Up tile only if testing shows it unused.
- Folder-aware New: inside a folder whose name is a plain English plural,
  the New tile reads "New <singular>" ("New Chapter" in Chapters, "New
  Note" in Notes, renamed folders followed); else "New Document". The new
  page starts as "# Chapter N" (one past the highest) in a Chapters-like
  numbered folder, else "# Untitled <singular>".
- Language scope: English only for now (US, UK, Canada, Australia); no
  localization planned. Plural rules are the same across those.

## v.next feature conversations

Tabled for after v1; each needs a design talk before any code.

- **History, beyond "open as a copy"** (the copy stays: restoring is never
  destructive). Three ways to go further, cheapest first:
  1. *Step through in place, read-only.* The History blade picks a
     version; the page shows it greyed with "Viewing Nov 27 16:24" in the
     strip; Cmd-[ and Cmd-] step to the older or newer version; any key
     that would edit, or Esc, returns to the live text. Needs a second
     text buffer (memory: one more copy of the document) and a guard so
     autosave never writes the viewed version.
  2. *Compare two versions (WinDiff style).* Pick two versions (or a
     version and now); the page shows them merged, removed text struck
     through and added text underlined (1-bit friendly: no colour), with
     Cmd-] / Cmd-[ jumping change to change. Needs a line-level diff
     (paragraphs as lines keeps it cheap on a 68000) and the read-only
     view from option 1.
  3. *Side by side.* Two columns of ~30 characters on a 512-pixel screen
     is too narrow for prose; better on a modern build than here.
  Also: `+ Mark this version...` (name a version), already in DESIGN.md.
- **TK, the rest:** a keyboard shortcut (Neo uses Cmd-Shift-X, which is
  our Strikethrough, so another key), and a dot beside documents in the
  drawer that still hold a TK (reading every file in a writing: check
  the cost on a floppy first, or keep a count in each journal).

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
