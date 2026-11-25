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

Metrics: 12 px (body) → ascent 13, descent 4, 7 px advance (60 columns in
the 420 px column, 17 px lines); 10 px is not generated (a 6 px cell is too
narrow for an automatic rasterization of m, w, a); 14 → 8 px; 16 → 10 px; 20 → 12 px.

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
