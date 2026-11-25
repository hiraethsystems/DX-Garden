# Usage: python3 -I tools/icon_extract.py art/icon-source.jpg ART.png
#        python3 -I tools/make_icons.py ART.png OUTDIR
# Makes every platform's icon from art.png (white brush art on transparent).
import sys, numpy as np
from PIL import Image, ImageDraw
art = Image.open(sys.argv[1]).convert("RGBA")
out = sys.argv[2]
BLACK = (13, 13, 13, 255)   # the artwork's own black

def scaled(px):
    return art.resize((px, px), Image.LANCZOS)

def centred(canvas_px, art_px, bg=None):
    c = Image.new("RGBA", (canvas_px, canvas_px), bg or (0, 0, 0, 0))
    a = scaled(art_px); o = (canvas_px - art_px) // 2
    c.alpha_composite(a, (o, o)); return c

def rounded_square(size, margin, radius_frac, art_frac, ss=4):
    S = size * ss; m = margin * ss; side = S - 2 * m
    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).rounded_rectangle((m, m, S - m - 1, S - m - 1), radius=side * radius_frac, fill=255)
    mask = mask.resize((size, size), Image.LANCZOS)
    c = Image.new("RGBA", (size, size), BLACK[:3] + (0,))
    sq = Image.new("RGBA", (size, size), BLACK); c.paste(sq, (0, 0), mask)
    art_px = round((size - 2 * margin) * art_frac)
    a = scaled(art_px); o = (size - art_px) // 2
    c.alpha_composite(a, (o, o)); return c

# Android adaptive icon: 108 dp canvas at xxxhdpi (4 px/dp). The art spans 64 dp,
# inside the 66 dp circle every launcher mask keeps.
centred(432, 240).save(f"{out}/android-foreground.png")
centred(432, 240).save(f"{out}/android-monochrome.png")
# Notification: 24 dp at xxxhdpi, art filling 22 dp (the status bar keeps 1 dp padding).
centred(96, 88).save(f"{out}/android-notification.png")
# Linux: 128 px, the square as in the artwork (corner radius 19 % of the side,
# art 77 % of the square), 4 px margin.
rounded_square(128, 4, 0.19, 0.77).save(f"{out}/linux-128.png")
rounded_square(512, 16, 0.19, 0.77).save(f"{out}/linux-512-preview.png")
# Vita bubble: the system crops to a circle; black to the edge, art 80 %.
v = centred(128, 102, BLACK).convert("RGB").quantize(colors=256, method=Image.MEDIANCUT, dither=Image.NONE)
v.save(f"{out}/vita-icon0.png", optimize=True)
print("done")
