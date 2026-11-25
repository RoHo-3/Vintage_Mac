# Fonts

## Current placeholder: MDW Mono

Until hand-tuned strikes exist, the app uses **MDW Mono**: an automatic
1-bit rasterization of **IBM Plex Mono** (Regular and Bold) at 10, 12, 14,
16 and 20 px, built as classic Mac `FONT` resources by
`tools/make_fonts.py` into `src/fonts.r`.

| Family | Font number | Source |
|---|---|---|
| MDW Mono | 200 (resource IDs 25600 + size) | IBMPlexMono-Regular.ttf |
| MDW Mono Bold | 201 (resource IDs 25728 + size) | IBMPlexMono-Bold.ttf |

Metrics: 10 px → ascent 11, descent 3, 6 px advance (70 columns in the
420 px column, 14 px lines); 12 → 7 px; 14 → 8 px; 16 → 10 px; 20 → 12 px.

**License:** IBM Plex is under the SIL Open Font License with Reserved
Font Name "Plex", so the modified bitmap versions may not use that name —
hence "MDW Mono". The original TTFs and `LICENSE.txt` are in
`ibm-plex-mono/`. These files stay under the OFL, not the project's BSD
license.

## Later: hand-tuned strikes

The long-term plan (pending the iA Writer license check on the to-do list)
is hand-cleaned strikes in FontForge or Bits'N'Picas: every counter keeps
at least one white pixel, fixed integer advance per size, Bold drawn from
the real Bold face.
