"""v2.1: regenerate v2/out/dummy_*.stl for the mirrored case.

All dummies mirror in x (winding fixed with invert()); the dupont dummy also
flips in y about YM (the VBAT/GND pins moved to the other long edge when the
board rotated 180 deg in plane).
"""
import os, sys
import numpy as np, trimesh

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "v2", "out")
BAK = os.path.join(HERE, "v2", "out_v1_dummies")
YM = 25.2

MIRROR_X = np.diag([-1.0, 1.0, 1.0, 1.0])

def xform(m, M):
    m = m.copy()
    m.apply_transform(M)
    m.invert()          # mirror flips winding
    return m

for f in sorted(os.listdir(OUT)):
    if not f.startswith("dummy_") or not f.endswith(".stl"):
        continue
    src = os.path.join(OUT, f)
    m = trimesh.load(src)
    if isinstance(m, trimesh.Scene):
        m = m.to_geometry()
    os.makedirs(BAK, exist_ok=True)
    if not os.path.exists(os.path.join(BAK, f)):
        m.export(os.path.join(BAK, f))          # keep the old one once
    M = MIRROR_X.copy()
    if f == "dummy_dupont.stl":
        m = xform(m, M)
        m.apply_translation([0, 2 * YM, 0])
        m.apply_transform(np.diag([1.0, -1.0, 1.0, 1.0]))
        m.invert()
    else:
        m = xform(m, M)
    m.export(src)
    print(f, "->", np.round(m.bounds, 1).tolist())
