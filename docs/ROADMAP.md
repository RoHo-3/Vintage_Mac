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
   - 4b. Append-only journal (`<name>.history`): autosave appends one
     small record; the .md is rewritten on Cmd-S, quit, suspend and
     switching documents. Opening replays it: work lost in a crash is
     restored quietly; a file edited on another computer becomes a new
     segment. Fuzz-tested with 300 simulated crashes. ✅ build 5
5. **Quiet chrome** — hidden menu bar (restored on suspend and quit), top
   strip (hamburger, title) and footer (word count / session clock, save
   dot: ring, dithered, solid) shown on pause or mouse move, hidden on the
   next keystroke; the clock redraws only its own rectangle. ✅ build 4
   (the hamburger does nothing until the drawer arrives)

## Later alphas

- Drawer and blades (project, Sections, Links, Find, Focus Timer, History,
  Export, Settings)
- Library (templates, my files, recent)
- Selection pill, Cmd-K palette with synonyms
- Notes, image placeholders, links and back stack
- **Default templates — no later than 0.05 alpha:** Blank, Outline,
  Research, Meeting Notes, Article, Short Story, Novel Project, Script
  (structures designed together; see to-do list)

## Known limits in the first build

- A new document needs one Save (dialog) before autosave takes over; the
  Library will remove that step.
- Mixed line endings in one file are normalized to the first one found.
- Tab inserts four spaces; tab characters in pasted text show as a box.
- Fonts are automatic rasterizations, not hand-tuned.
- H6 uses capitals as a stand-in for small caps; testing focuses on System
  7.1 until a full System 6.0.8 pass later.

## Future exploration

- **Cross-device `.history` (old Mac ↔ new Mac).** Journals sit beside their
  documents, flagged invisible for the classic Finder; modern macOS ignores
  the flag, and Finder-level moves leave journals behind (cleaned up on
  launch). If a modern-Mac version is built, decide then how history
  travels (e.g. a sidecar folder, extended attributes, or a sync format).
  Until then: nothing in the file format may preclude it. Documents are plain
  UTF-8 Markdown; the journal format is documented in STORAGE.md.
