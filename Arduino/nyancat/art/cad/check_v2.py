import glob, os, json, itertools, numpy as np, trimesh, manifold3d as m3
from scipy.spatial import cKDTree
D = "v2/out"
info = json.load(open(f"{D}/info.json"))
board = trimesh.load("board_device.stl")
bpts = np.vstack([board.vertices, board.sample(200000)])
def man(m): return m3.Manifold(m3.Mesh(vert_properties=np.asarray(m.vertices, np.float32), tri_verts=np.asarray(m.faces, np.uint32)))
parts, meshes = {}, {}
for f in glob.glob(f"{D}/asm_*.stl"):
    n = os.path.basename(f)[4:].split("__")[0]; m = trimesh.load(f)
    meshes.setdefault(n, []).append(m)
    parts[n] = man(m) if n not in parts else parts[n] + man(m)
print("== board vs parts (final position) ==")
bt = cKDTree(bpts)
for n, ms in meshes.items():
    inside = sum(int(m.contains(bpts).sum()) for m in ms)
    d, _ = bt.query(np.vstack([m.sample(80000) for m in ms]))
    print(f"  {n:10s} inside={inside:5d} min_gap={d.min():.3f}")
print("== part/part overlaps ==")
for a, b in itertools.combinations(parts, 2):
    v = (parts[a] ^ parts[b]).volume()
    if v > 0.01: print(f"  {a} x {b}: {v:.3f}")
# module inserted from the BACK: body material behind any module point in its xy shadow
G = 0.1; hmin = {}
for x, y, z in bpts:
    k = (int(np.floor(x / G)), int(np.floor(y / G)))
    if z < hmin.get(k, 1e9): hmin[k] = z
q = np.vstack([m.sample(300000) for m in meshes["body"]])
bad = 0
for x, y, z in q:
    k = (int(np.floor(x / G)), int(np.floor(y / G)))
    hz = min(hmin.get((k[0] + i, k[1] + j), 1e9) for i in (-1, 0, 1) for j in (-1, 0, 1))
    if hz < 1e8 and z > hz + 0.02: bad += 1
print("== module slid in from the back: body collisions:", bad)
def boxm(x0, x1, y0, y1, z0, z1): return man(trimesh.creation.box(bounds=[[x0, y0, z0], [x1, y1, z1]]))
bx0, by0, bx1, by1 = info["bat_box"]; cb = info["chassis"][1]; cin = info["cover_in"]
sx, sy, bw, bl, bh = info["switch"]; uz = info["usb_z"]
dummies = {
  "battery 602535": boxm(bx0 + 0.25, bx1 - 0.25, by0 + 0.25, by1 - 0.25, cb + 0.05, cb + 6.05),
  "dupont F-M on pins": boxm(-14.6, -9.4, 32.8, 35.3, 11.5, 18.4),
  "dupont bent ~1.5 mm up": boxm(-14.6, -9.4, 34.4, 38.6, 18.4, 26.0),
  "jumper wire bend": boxm(-14.6, -9.4, 35.0, 41.2, 26.0, 27.4),
  "switch body": boxm(sx - 1.75, sx + 1.75, sy - 4.3, sy + 4.3, cin - 3.6, cin - 0.02),
  "switch legs": boxm(sx - 0.4, sx + 0.4, sy - 2.6, sy + 2.6, cin - 3.6 - 3.5, cin - 3.6),
  "switch lever (travel)": boxm(sx - 0.75, sx + 0.75, sy - 2.0, sy + 2.0, cin, cin + 5.0),
  "usb overmold 12.5x7.5": boxm(-70, -26.3, 25.2 - 6.25, 25.2 + 6.25, uz - 3.75, uz + 3.75),
  "paper clip top": man(trimesh.creation.cylinder(0.5, 12, sections=16, transform=trimesh.transformations.rotation_matrix(np.pi/2, [1,0,0], [0,0,0]) @ np.eye(4))).translate([info["act"][0], info["act"][1] + 6.05, info["act"][3]]),
}
print("== dummies ==")
for dn, d in dummies.items():
    hits = [(n, round((d ^ p).volume(), 3)) for n, p in parts.items() if (d ^ p).volume() > 0.01]
    print(f"  {dn:26s}", hits or "OK")
