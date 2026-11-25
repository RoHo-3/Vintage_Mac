# Roadmap

## 0.01 alpha — "write in it"

1. **Text engine** — gap buffer, 70-column word wrap (incremental re-wrap),
   cursor, mouse and Shift selection, double-click word select, modern
   movement keys, multi-level undo/redo, Cut/Copy/Paste via the Mac
   clipboard. ✅ first build
2. **Fonts** — MDW Mono (bitmap IBM Plex Mono) 10/12/14/16/20 px, Regular
   and Bold, built into the app. ✅ first build
3. **Live Markdown** — heading sizes, bold/italic, markers hidden except on
   the cursor line.
4. **Files** — open/save `.md` (UTF-8 ↔ MacRoman, line endings preserved),
   change-threshold autosave, append-only journal for crash recovery.
   Classic Open dialog as a stand-in until the Library exists.
5. **Quiet chrome** — hidden menu bar, top strip on pause, footer with word
   count, session clock and three-state save dot.

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

- No saving yet; text is lost on quit.
- Tab inserts four spaces; tab characters in pasted text show as a box.
- Fonts are automatic rasterizations, not hand-tuned.
