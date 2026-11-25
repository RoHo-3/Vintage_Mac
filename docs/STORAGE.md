# Storage & Journal Format

Goal: fast, crash-safe saves on slow vintage disks, and a file format that can
support full version history and connected drafts later.

## Files

Each document is two sibling files:

| File | Purpose |
|---|---|
| `Name.md` | Plain Markdown, readable by any app. Regenerated from the journal. |
| `Name.history` | Append-only journal — the source of truth. Data fork only. |

If a user moves the `.md` without its `.history`, they keep the text but lose
the history.

## Journal records

Every record: `type (1) · length (4) · payload · checksum (2)`. A torn final
record (bad length/checksum) is discarded on recovery.

| Type | Payload |
|---|---|
| Header | magic, format version, document ID |
| Snapshot | timestamp, draft ID, full text |
| Edit batch | timestamp, draft ID, list of insert/delete ops (pos, len, text) |
| Checkpoint | timestamp, draft ID, text length, text checksum, caret position, optional label |

Keystrokes are coalesced in memory; each autosave appends **one edit batch +
one checkpoint** in a single write.

## When things are written

| Event | Journal | `.md` |
|---|---|---|
| Autosave (pause + threshold) | append batch + checkpoint | — |
| Cmd+S, quit, close, draft switch, suspend | append batch + checkpoint | full rewrite |

## Recovery

On open: load latest snapshot → replay records to the last valid checkpoint.
If the result differs from the `.md`, show a non-modal banner offering to
restore.

## Write-performance rules

1. **Few, large writes.** Autosave = one `FSWrite` of the batched records.
   `.md` rewrite = two `FSWrite`s (the two halves of the gap buffer).
2. **Pre-allocate.** Grow physical file space with `Allocate` (journal in
   32 KB steps, `.md` with headroom). Never pad the logical EOF — other apps
   must see exactly the text, and recovery must find the true journal end.
3. **Metadata stays out of saves.** Preferences and draft list are written on
   quit only; caret position rides in checkpoints. No resource-fork writes
   during editing.

Not used: chunked/interruptible saves and temp-file swapping — the journal
makes them unnecessary.

## Growth

When the journal grows large, start a new snapshot segment rather than
discarding history. A "prune history older than…" setting can come later.

## Future features this enables

- Version history: every checkpoint is restorable.
- Named versions: labeled checkpoints.
- Connected drafts: records tagged with a draft ID, branching from a checkpoint.

## Round-trip with modern editors (v1 requirement)

Writers will also edit the same `.md` files in a modern editor on another
machine. The rule: **never damage a file you didn't write** — the app only
changes the bytes the writer actually edited.

- **Line endings:** read LF, CRLF and CR; write back in the file's original
  style (new files: LF, the modern default).
- **Text encoding:** files are UTF-8 on disk; the app converts to MacRoman
  for display and back on save. Characters with no MacRoman equivalent
  (emoji, CJK, many symbols) are kept byte-for-byte and shown as a small
  placeholder glyph; they survive any number of round trips.
- **Unknown Markdown** (tables, footnotes, front matter, raw HTML) is shown
  as plain text and never rewritten.
- **Finder metadata:** files arriving without a type/creator are opened by
  the `.md` extension; saving sets type `TEXT`, creator `MMDW` so they
  double-click into the app. Name length is limited by HFS (31 chars).
- **Edited elsewhere:** if a document's text no longer matches its latest
  journal checkpoint, the app quietly records the new text as a version in
  History, marked "◆ Edited on another computer". Nothing is lost on either
  side.
- The `.history` journal stays with the file but is meaningless to other
  editors; they simply ignore it.
