import glob, os, numpy as np, trimesh, itertools, manifold3d as m3
from scipy.spatial import cKDTree
board = trimesh.load("board_device.stl")
bpts = np.vstack([board.vertices, board.sample(150000)])
btree = cKDTree(bpts)
parts = {}
for f in glob.glob("out/asm_*.stl"):
    name = os.path.basename(f)[4:].split("__")[0]
    parts.setdefault(name, []).append(trimesh.load(f))
def to_man(ms):
    out = None
    for m in ms:
        mm = m3.Manifold(m3.Mesh(vert_properties=np.asarray(m.vertices, np.float32), tri_verts=np.asarray(m.faces, np.uint32)))
        out = mm if out is None else out + mm
    return out
man = {k: to_man(v) for k, v in parts.items()}
print("== board vs parts: points inside / min gap ==")
for k, ms in parts.items():
    inside = sum(int(m.contains(bpts).sum()) for m in ms)
    sp = np.vstack([m.sample(60000) for m in ms])
    d, i = btree.query(sp)
    j = d.argmin()
    print(f"  {k:12s} inside={inside:6d}  min_gap={d.min():.3f} at part pt {np.round(sp[j],2)}")
print("== part/part overlap volume (mm3) ==")
for a, b in itertools.combinations(man, 2):
    v = (man[a] ^ man[b]).volume()
    if v > 0.01: print(f"  {a} x {b}: {v:.3f}")
