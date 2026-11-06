import sys, numpy as np
from PIL import Image, ImageFilter
src, W, H, out = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
a = np.fromfile(src, dtype=np.float32).reshape(H, W, 3)
a = a * 0.95
# bloom from bright (screen/glints)
br = np.clip(a - 0.75, 0, None)
bi = Image.fromarray(np.clip(br / 4 * 255, 0, 255).astype(np.uint8))
b1 = np.asarray(bi.filter(ImageFilter.GaussianBlur(W / 300)), dtype=np.float32) / 255 * 4
b2 = np.asarray(bi.filter(ImageFilter.GaussianBlur(W / 80)), dtype=np.float32) / 255 * 4
a = a + 0.25 * b1 + 0.15 * b2
# filmic (ACES approx)
x = a
a = np.clip((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0, 1)
a = np.where(a <= 0.0031308, a * 12.92, 1.055 * np.power(a, 1 / 2.4) - 0.055)
yy, xx = np.mgrid[0:H, 0:W]
r = np.hypot((xx - W / 2) / (W / 2), (yy - H / 2) / (H / 2))
a = a * (1 - 0.18 * np.clip(r - 0.55, 0, None) ** 1.6)[..., None]
a = a + np.random.default_rng(1).normal(0, 0.006, a.shape)
Image.fromarray(np.clip(a * 255, 0, 255).astype(np.uint8)).save(out)
