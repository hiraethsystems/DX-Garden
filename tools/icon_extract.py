# Usage: python3 -I tools/icon_extract.py art/icon-source.jpg ART.png
# art/icon-source.jpg: the owner's icon artwork (1024 px, white brush art
# on a black rounded square). Its numbers (square at 135-888 px, centre 512)
# are for that file.
# Cuts the white brush art out of the JPEG as white-on-transparent (alpha = brightness).
import sys, numpy as np
from PIL import Image
src, out = sys.argv[1], sys.argv[2]
a = np.asarray(Image.open(src).convert("L")).astype(np.float32)
h, w = a.shape
yy, xx = np.mgrid[0:h, 0:w]
inside = (xx - 512) ** 2 + (yy - 512) ** 2 < 360 ** 2      # well inside the black square
black = 13.0
alpha = np.clip((a - black - 6) / (250 - black - 6), 0, 1)  # lift JPEG noise in the black to 0
alpha[~inside] = 0
ys, xs = np.where(alpha > 0.5)
print("art bbox", xs.min(), xs.max(), ys.min(), ys.max(), "centre", (xs.min()+xs.max())/2, (ys.min()+ys.max())/2)
x0, x1, y0, y1 = xs.min(), xs.max(), ys.min(), ys.max()
cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
half = max(x1 - x0, y1 - y0) / 2 + 8
box = tuple(int(round(v)) for v in (cx - half, cy - half, cx + half, cy + half))
rgba = np.zeros((h, w, 4), np.uint8); rgba[..., :3] = 255; rgba[..., 3] = (alpha * 255 + 0.5).astype(np.uint8)
art = Image.fromarray(rgba, "RGBA").crop(box)
art.save(out); print("art", art.size, "box", box)
