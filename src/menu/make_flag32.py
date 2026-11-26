# Chequered-flag main menu icon for Single Race in NFS Hot Pursuit (2010): replaces the Wall icon (raster 0x20b90b5b,
# 64x64 A8R8G8B8 in MAIN_MENU.BNDL). HP2010's menu icons are a white disc (alpha edge + glow) with the symbol drawn in
# black (opaque), unlike Remastered's cut-outs. The flag geometry is Remastered's make_flag.py, scaled from its 128 px
# disc (centre 49.5/48.5, r 45.5) to this one (centre 31.5/31.5, r 29.5).
# Usage: make_flag32.py <original BGRA bytes file> <out BGRA bytes file> [preview png]
import sys, math
import numpy as np
from PIL import Image

src, out = sys.argv[1:3]
orig = np.frombuffer(open(src, 'rb').read(), np.uint8).reshape(64, 64, 4).astype(np.float64)
S = 16
N = 64 * S
yy, xx = (np.mgrid[0:N, 0:N] + 0.5) / S
# texture coordinates of the Remastered design
sc = 45.5 / 29.5
X = 49.5 + (xx - 31.5) * sc
Y = 48.5 + (yy - 31.5) * sc
pole_x0, pole_x1, pole_top, pole_bot = 25.5, 30.5, 24.0, 77.0
pole = (X >= pole_x0) & (X <= pole_x1) & (Y >= pole_top) & (Y <= pole_bot)
pole |= (np.hypot(X - (pole_x0 + pole_x1) / 2, Y - (pole_top - 1.0)) <= 4.0)
fx0, fw, fy0, fh, amp = 30.5, 45.0, 27.0, 30.0, 3.5
def top(x):
    u = (x - fx0) / fw
    return fy0 + amp * np.sin(u * math.pi * 1.7 + 0.25) - amp * math.sin(0.25)
u = (X - fx0) / fw
v = (Y - top(X)) / fh
outline = 3.2
du, dv = outline / fw, outline / fh
outer = (u >= 0) & (u <= 1 + du) & (v >= -dv) & (v <= 1 + dv)
cols, rows = 4, 3
cell_u = np.clip(np.floor(u * cols), 0, cols - 1)
cell_v = np.clip(np.floor(v * rows), 0, rows - 1)
dark = ((cell_u + cell_v) % 2) == 0
inside = (u > 0) & (u < 1) & (v > 0) & (v < 1)
ink = pole | (outer & ~inside) | (inside & dark)
cov = ink.reshape(64, S, 64, S).mean(axis=(1, 3))
ty, tx = np.mgrid[0:64, 0:64]
r = np.hypot(tx - 31.5, ty - 31.5)
alpha = np.where(r > 27, orig[:, :, 3], 255.0)   # the original disc edge and glow
lum = 255.0 * (1.0 - cov)
bgra = np.zeros((64, 64, 4), np.uint8)
bgra[:, :, 0] = bgra[:, :, 1] = bgra[:, :, 2] = np.clip(np.round(lum), 0, 255).astype(np.uint8)
bgra[:, :, 3] = np.clip(np.round(alpha), 0, 255).astype(np.uint8)
open(out, 'wb').write(bgra.tobytes())
if len(sys.argv) > 3:
    im = Image.fromarray(bgra[:, :, [2, 1, 0, 3]], 'RGBA')
    bg = Image.new('RGBA', (64, 64), (30, 60, 140, 255)); bg.alpha_composite(im)
    bg.resize((256, 256), Image.NEAREST).save(sys.argv[3])
print('ok', out)
