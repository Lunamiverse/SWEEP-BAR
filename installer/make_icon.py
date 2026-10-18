"""Draws the Sweepbar icon (dark tile, orange swell curve over beat bars) as a multi-size .ico."""
import math
import sys
from PIL import Image, ImageDraw

BG = (18, 19, 23, 255)
CELL = (44, 46, 54, 255)
ACCENT = (255, 154, 60, 255)


def draw(size: int) -> Image.Image:
    s = 4  # supersample
    W = size * s
    img = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([0, 0, W - 1, W - 1], radius=int(W * 0.22), fill=BG)
    # four beat cells along the bottom, beats 2 and 4 lit
    pad, gap = W * 0.16, W * 0.05
    cw = (W - 2 * pad - 3 * gap) / 4
    top, bot = W * 0.70, W * 0.84
    for i in range(4):
        x = pad + i * (cw + gap)
        d.rounded_rectangle([x, top, x + cw, bot], radius=int(W * 0.03), fill=ACCENT if i in (1, 3) else CELL)
    # one continuous curve: flat, with a swell above beats 2 and 4 (like the plugin's curve view)
    base, height = W * 0.58, W * 0.36
    x_start, x_end = pad * 0.55, W - pad * 0.45
    centres = [pad + i * (cw + gap) + cw / 2 for i in (1, 3)]
    half = (cw + gap) * 0.78
    pts = []
    for k in range(400):
        x = x_start + (x_end - x_start) * k / 399
        y = base
        for c in centres:
            if abs(x - c) < half:
                y = base - height * math.cos(math.pi / 2 * (x - c) / half) ** 2
        pts.append((x, y))
    r = max(1.5, W * 0.028)
    for (x, y) in pts:  # stamp circles for a smooth, round-capped stroke
        d.ellipse([x - r, y - r, x + r, y + r], fill=ACCENT)
    return img.resize((size, size), Image.LANCZOS)


if __name__ == "__main__":
    out = sys.argv[1]
    sizes = [256, 128, 64, 48, 32, 24, 16]
    big = draw(256)
    big.save(out, format="ICO", sizes=[(n, n) for n in sizes],
             append_images=[draw(n) for n in sizes[1:]])
    big.save(out.replace(".ico", ".png"))
