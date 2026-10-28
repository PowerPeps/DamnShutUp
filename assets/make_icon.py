import sys
from pathlib import Path

import numpy as np
from PIL import Image

OUT = Path(__file__).parent
SIZE = 1024
SS = 2
N = SIZE * SS
SCALE = 445
CX, CY = 512, 481

rng = np.random.default_rng(7)


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)


def box_blur(a, r, axis):
    if r < 1:
        return a
    pad = [(0, 0)] * a.ndim
    pad[axis] = (r + 1, r)
    c = np.cumsum(np.pad(a, pad, mode="edge"), axis=axis)
    n = a.shape[axis]
    hi = np.take(c, np.arange(2 * r + 1, 2 * r + 1 + n), axis=axis)
    lo = np.take(c, np.arange(0, n), axis=axis)
    return (hi - lo) / (2 * r + 1)


def blur(a, r):
    for _ in range(3):
        a = box_blur(box_blur(a, r, 0), r, 1)
    return a


def lowres_noise(w, h):
    img = Image.fromarray(rng.standard_normal((h, w)).astype(np.float32), "F")
    return np.asarray(img.resize((N, N), Image.Resampling.BICUBIC))


def normalize(v):
    return v / np.linalg.norm(v, axis=-1, keepdims=True)


def top(x):
    t = np.abs(x)
    e = np.clip(1 - t**2, 0, 1)
    return 0.05 * t**2 + 0.40 * e**0.6 + 0.06 * np.exp(-(((t - 0.24) / 0.13) ** 2)) * e - 0.07 * np.exp(-((t / 0.09) ** 2))


def mid(x):
    t = np.abs(x)
    e = np.clip(1 - t**2, 0, 1)
    return 0.05 * t**2 - 0.045 * e - 0.04 * np.exp(-((t / 0.12) ** 2)) * e


def bot(x):
    t = np.abs(x)
    e = np.clip(1 - t**2, 0, 1)
    return 0.05 * t**2 - 0.045 * e - 0.56 * e**0.55


px = (np.arange(N) + 0.5) / SS
X, Y = np.meshgrid(px, px)
lx = (X - CX) / SCALE
ly = -(Y - CY) / SCALE
t = np.clip(np.abs(lx), 0, 1)
env = 1 - t**2
yt, ym, yb = top(lx), mid(lx), bot(lx)
inside = np.abs(lx) < 1
aa = 1.2 / (SCALE * SS)

alpha_lips = smoothstep(0, aa, yt - ly) * smoothstep(0, aa, ly - yb) * inside

v = np.clip((ly - ym) / np.maximum(yt - ym, 1e-4), 0, 1)
w = np.clip((ym - ly) / np.maximum(ym - yb, 1e-4), 0, 1)
prof_up = np.sin(np.pi * v) ** 0.7
prof_lo = np.sin(np.pi * w**0.75) ** 0.6
is_up = ly >= ym
prof = np.where(is_up, prof_up, prof_lo) * inside
thick = np.where(is_up, yt - ym, ym - yb)
height = 0.38 * thick * prof - 0.22 * lx**2 * inside

u = lx / (1 + 0.9 * np.abs(ly - ym))
grid = np.linspace(-1.6, 1.6, 4000)
n1 = rng.standard_normal(grid.size)
for _ in range(3):
    n1 = np.convolve(n1, np.ones(9) / 9, mode="same")
n1 /= n1.std()
wrinkle = np.interp(u, grid, n1) * (0.6 + 0.4 * np.tanh(lowres_noise(40, 18)))
height += 0.0009 * wrinkle * prof**0.5

height = blur(height, 2)
dx = 1 / (SCALE * SS)
g_row, g_col = np.gradient(height, dx)
normal = normalize(np.stack([-g_col, g_row, np.ones_like(height)], axis=-1))

light = normalize(np.array([-0.35, 0.55, 0.75]))
view = np.array([0.0, 0.0, 1.0])
half = normalize(light + view)
ndl = np.clip(normal @ light, 0, 1)
ndh = np.clip(normal @ half, 0, 1)

albedo = np.array([0.60, 0.006, 0.022])
shadow_tint = np.array([0.20, 0.0, 0.012])
diff = ndl[..., None]
col = shadow_tint * (1 - diff) * 0.6 + albedo * (0.18 + 0.95 * diff)
col += np.array([0.08, 0.0, 0.01]) * prof[..., None] ** 2

wet = smoothstep(-0.4, 1.2, lowres_noise(26, 12))
spec = (ndh**90 * 1.4 + ndh**18 * 0.10) * (0.35 + 0.65 * wet)
spec *= smoothstep(0.02, 0.25, prof)
col += spec[..., None]

rim = np.exp(-(((1 - v) / 0.05) ** 2)) * is_up * smoothstep(0.0, 0.3, thick)
col *= 1 + 0.25 * rim[..., None]

tip = np.exp(-(((1 - t) / 0.06) ** 2))
line_w = 0.008 + 0.016 * env + 0.014 * tip
dline = np.exp(-(((ly - ym) / line_w) ** 2)) * inside
col *= (1 - 0.9 * dline)[..., None]
near_line = np.exp(-np.abs(ly - ym) / 0.07)
recess = smoothstep(0.72, 1.0, t) * (0.35 + 0.65 * near_line)
col *= (1 - 0.6 * recess)[..., None]

# bar
p0 = np.array([170.0, 170.0])
p1 = np.array([854.0, 854.0])
r_bar, gap = 54.0, 0.0
d = p1 - p0
P = np.stack([X, Y], axis=-1)
s = np.clip(((P - p0) @ d) / (d @ d), 0, 1)
closest = p0 + s[..., None] * d
rel = P - closest
dist = np.linalg.norm(rel, axis=-1)
aa_px = 1.2 / SS
alpha_bar = smoothstep(r_bar, r_bar - aa_px, dist)
alpha_lips *= smoothstep(r_bar + gap - aa_px, r_bar + gap, dist)

q = np.clip(dist / r_bar, 0, 0.999)
dir_in_plane = rel / np.maximum(dist, 1e-6)[..., None]
bn = np.concatenate([dir_in_plane * q[..., None] * np.array([1, -1]), np.sqrt(1 - q**2)[..., None]], axis=-1)
bn = normalize(np.concatenate([bn[..., :2] * 1.0, bn[..., 2:]], axis=-1))
b_ndl = np.clip(bn @ light, 0, 1)
b_ndh = np.clip(bn @ half, 0, 1)
bar_col = np.array([0.012, 0.012, 0.016]) * (0.4 + 1.4 * b_ndl[..., None])
bar_col += (b_ndh**60 * 0.9 + b_ndh**8 * 0.06)[..., None]
edge = smoothstep(r_bar - 4.0, r_bar - 1.0, dist)
bar_col = bar_col * (1 - 0.6 * edge[..., None]) + 0.6 * edge[..., None] * 0.55

shadow_a = blur(np.maximum(alpha_lips, alpha_bar), 12)
shadow_a = np.roll(shadow_a, 10 * SS, axis=0) * 0.35

to_srgb = lambda c: np.clip(c, 0, 1) ** (1 / 2.2)
lips_rgb = to_srgb(col)
bar_rgb = to_srgb(bar_col)

rgb = np.zeros((N, N, 3))
a = shadow_a.copy()
rgb = rgb * (1 - alpha_lips[..., None]) + lips_rgb * alpha_lips[..., None]
a = a * (1 - alpha_lips) + alpha_lips
rgb = rgb * (1 - alpha_bar[..., None]) + bar_rgb * alpha_bar[..., None]
a = a * (1 - alpha_bar) + alpha_bar

rgba = np.dstack([rgb, a])
img = Image.fromarray((np.clip(rgba, 0, 1) * 255 + 0.5).astype(np.uint8), "RGBa")
img = img.resize((SIZE, SIZE), Image.Resampling.LANCZOS).convert("RGBA")

img.save(OUT / "icon.png")
img.save(OUT / "icon.ico", sizes=[(s, s) for s in (16, 20, 24, 32, 40, 48, 64, 128, 256)])

if len(sys.argv) > 1:
    prev = Path(sys.argv[1])
    sheet = Image.new("RGB", (2 * SIZE, SIZE + 300), (240, 240, 240))
    dark = Image.new("RGB", (SIZE, SIZE + 300), (32, 32, 32))
    sheet.paste(dark, (SIZE, 0))
    sheet.paste(img, (0, 0), img)
    sheet.paste(img, (SIZE, 0), img)
    x = 20
    for sz in (16, 24, 32, 48, 64, 128, 256):
        small = img.resize((sz, sz), Image.Resampling.LANCZOS)
        for ox in (0, SIZE):
            sheet.paste(small, (ox + x, SIZE + 20), small)
        x += sz + 20
    sheet.save(prev)
