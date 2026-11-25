# Fonts

The writing canvas uses hand-tuned 1-bit bitmap strikes derived from the
iA Writer Mono family (itself based on IBM Plex Mono).

**Pending:** confirm the font license terms. The iA Writer fonts are under the
SIL Open Font License; if a Reserved Font Name applies, the modified bitmap
fonts must ship under a different name. Until resolved, no font files are
committed here.

## Pipeline (planned)

1. Open the Regular and Bold `.ttf` files in FontForge.
2. Element → Bitmap Strikes Available: 10, 12, 14, 16, 20 px.
3. Hand-clean each strike in the bitmap editor (every counter keeps ≥1 white
   pixel; fixed integer advance per size: 6/7/8/10/12 px).
4. Export as NFNT + FOND and embed in the app's resource fork.

Font files in this directory remain under the SIL Open Font License, not the
project's MIT license.
