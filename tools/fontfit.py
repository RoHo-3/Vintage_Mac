"""Grid-fitted rasterization for make_fonts.py --fitted.

Each glyph is rendered at 8x and box-filtered to coverage, at sub-pixel
offsets (x: -1/2..+1/2 px, y: -3/8..+3/8 px, never moving ink past the
glyph's own top or bottom row). The offset kept is the one whose 1-bit
image is closest to the coverage and whose stems are closest to a target
width (Regular ~0.075 em, Bold ~0.13 em, at least 2 px). Faint strokes keep
their strongest pixel (dropout control) so hairlines and bowls survive.
"""
import numpy as np
from PIL import Image, ImageDraw, ImageFont
SS = 8
def coverage(font_ss, ch, w, asc, h, dx, dy=0, xscale=1.0):
    img = Image.new('L', ((w + 2) * SS, h * SS), 0)
    d = ImageDraw.Draw(img)
    d.text(((1 + dx) * SS, (asc + dy) * SS), ch, font=font_ss, fill=255, anchor='ls')
    if xscale != 1.0:                    # narrow the glyph about the cell's centre
        W, H = img.size
        small = img.resize((max(1, int(W * xscale + 0.5)), H), Image.BOX)
        img = Image.new('L', (W, H), 0)
        img.paste(small, (int((1 + (w - 1) / 2) * SS * (1 - xscale) + 0.5), 0))
    a = np.array(img, dtype=float) / 255
    return a.reshape(h, SS, w + 2, SS).mean(axis=(1, 3))
def stem_dev(b, t):
    d = 0
    for row in b:
        n = 0
        for v in list(row) + [0]:
            if v: n += 1
            elif n:
                if n <= t + 2: d += abs(n - t)
                n = 0
    return d

def fitted(path, size, ch, w, asc, h, gap=False):
    """gap: keep the cell's last column blank (letter spacing in a narrow
    cell), narrowing glyphs that cannot otherwise fit. '_' may touch."""
    ratio = 0.13 if 'Bold' in path else 0.075
    t = max(2 if 'Bold' in path else 1, int(size * ratio + 0.5))
    font_ss = ImageFont.truetype(path, size * SS)
    gap = gap and ch != '_'
    c0 = coverage(font_ss, ch, w, asc, h, 0, 0)
    r0 = np.nonzero((c0 >= 0.25).any(axis=1))[0]
    ref = (r0.min(), r0.max()) if len(r0) else (0, h - 1)
    for xscale in ((1.0, 0.92, 0.85, 0.78, 0.7) if gap else (1.0,)):
        best = None
        for k, j in [(k, j) for k in range(-4, 5) for j in (-3, -2, -1, 0, 1, 2, 3)]:
            c = coverage(font_ss, ch, w, asc, h, k / 8, j / 8, xscale)
            b = (c >= 0.5)
            p = np.pad(c, 1)
            vmax = (c > p[:-2, 1:-1]) & (c >= p[2:, 1:-1])   # strongest in its column run
            hmax = (c > p[1:-1, :-2]) & (c >= p[1:-1, 2:])   # strongest in its row run
            b = (b | ((c >= 0.25) & (vmax | hmax))).astype(np.uint8)
            last = w if gap else w + 1   # first padded column that must stay blank
            if b[:, 0].any() or b[:, last:].any():
                continue
            rows = np.nonzero(b.any(axis=1))[0]
            if len(rows) and (rows.min() < ref[0] or rows.max() > ref[1]):
                continue
            err = np.abs(b - c).sum() + 0.02 * (abs(k) + abs(j)) + 0.6 * stem_dev(b, t)
            if best is None or err < best[0]:
                best = (err, b)
        if best is not None:
            return best[1][:, 1:w + 1]
    return None                          # caller renders it plainly
