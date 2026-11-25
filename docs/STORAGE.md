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
