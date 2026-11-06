import json, glob, os, numpy as np, trimesh, manifold3d as m3
info = json.load(open("out/info.json"))
def man(m): return m3.Manifold(m3.Mesh(vert_properties=np.asarray(m.vertices, np.float32), tri_verts=np.asarray(m.faces, np.uint32)))
def boxm(x0, x1, y0, y1, z0, z1):
    return man(trimesh.creation.box(bounds=[[x0, y0, z0], [x1, y1, z1]]))
ch0, ch1 = info["chassis"]
bx0, by0, bx1, by1 = info["bat_box"]
uz = info["usb_z"]
dummies = {
  "battery 603035": boxm(bx0 + 0.25, bx1 - 0.25, by0 + 0.25, by1 - 0.25, ch1 + 0.05, ch1 + 6.05),
  "switch body": boxm(15.4 - 1.8, 15.4 + 1.8, 25.2 - 4.3, 25.2 + 4.3, 27.5 - 3.6, 27.45),
  "switch legs": boxm(15.4 - 0.4, 15.4 + 0.4, 25.2 - 2.6, 25.2 + 2.6, 27.5 - 3.6 - 3.5, 27.5 - 3.6),
  "switch lever": boxm(15.4 - 0.75, 15.4 + 0.75, 25.2 - 0.75 - 0.75, 25.2 + 0.75 - 0.75, 27.5, 31.5),
  "usb overmold 12.5x7.5": boxm(-70, -26.3, 25.2 - 6.25, 25.2 + 6.25, uz - 3.75, uz + 3.75),
  "usb plug metal": boxm(-26.3, -19.7, 25.2 - 4.15, 25.2 + 4.15, uz - 1.25, uz + 1.25),
}
parts = {}
for f in glob.glob("out/asm_*.stl"):
    n = os.path.basename(f)[4:].split("__")[0]
    m = man(trimesh.load(f)); parts[n] = m if n not in parts else parts[n] + m
for dn, d in dummies.items():
    hits = [(n, round((d ^ p).volume(), 3)) for n, p in parts.items() if (d ^ p).volume() > 0.01]
    print(f"{dn:24s}", hits or "OK")
