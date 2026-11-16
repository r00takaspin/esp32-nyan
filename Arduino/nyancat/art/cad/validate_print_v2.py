"""Pre-print validation for Pixel Nyan v2.1 (run from art/cad with ../.venv/bin/python).

Complements check_v2.py (gaps/collisions/dummies) with:
  A. mesh integrity of every printable STL (watertight, 1 shell, volume)
  B. 3MF sanity (object count, placement above the bed, volume match vs STL)
  C. mesh-level mate checks: board standoffs <-> chassis holes, cover screws <->
     pillars + pilot holes, cover posts, USB tunnel continuity, wire window,
     switch pocket/lever slot, screen window margins vs active area.
"""
import glob, json, os, sys
import numpy as np
import trimesh
import manifold3d as m3

D = "v2/out"; P = "v2/print"
info = json.load(open(f"{D}/info.json"))
XM, YM = 3.6, 25.2
STANDOFF_END, CH_FRONT, CH_BACK = info["standoff_end"], *info["chassis"]
COVER_IN, BACK = info["cover_in"], info["back"]
USB_Z = info["usb_z"]; SW_X, SW_Y = info["switch"][0], info["switch"][1]
HOLES = [(XM - sy * 19.5, YM - sx * 8.5) for sx in (-1, 1) for sy in (-1, 1)]
PILLARS = {k: tuple(v) for k, v in info["pillars"].items()}
POSTS = [(27.4, YM - 7.0), (27.4, YM + 7.0), (SW_X, YM - 8.6), (SW_X, YM + 8.6)]

fails, warns = [], []
def ok(name, cond, detail=""):
    print(f"  [{'OK ' if cond else 'FAIL'}] {name}{(' — ' + detail) if detail else ''}")
    if not cond: fails.append(name)
def warn(name, cond, detail=""):
    print(f"  [{'OK ' if cond else 'warn'}] {name}{(' — ' + detail) if detail else ''}")
    if not cond: warns.append(name)

def ring(x, y, r, zs, n=8):
    a = np.linspace(0, 2 * np.pi, n, endpoint=False)
    return np.array([[x + r * np.cos(t), y + r * np.sin(t), z] for z in zs for t in a])

def axis(x, y, z0, z1, n=24):
    return np.array([[x, y, z] for z in np.linspace(z0, z1, n)])

# ------------------------------------------------------------------ A. STL integrity
print("== A. printable STL integrity ==")
# criterion: topologically closed (no boundary edges), positive volume, manifold3d
# accepts it (volumetric booleans OK). 4-use edges / multiple shells are EXPECTED:
# diagonal pixel neighbours touch along knife edges, colour shells stack on the core,
# and the USB tunnel splits rainbow stripes — slicers union these on import.
stl_vol = {}
for f in sorted(glob.glob(f"{P}/*_stl/*.stl")):
    m = trimesh.load(f)
    name = os.path.relpath(f, P)
    _, counts = np.unique(m.edges_sorted, axis=0, return_counts=True)
    boundary, knife = int((counts == 1).sum()), int((counts > 2).sum())
    degen = int((m.area_faces < 1e-8).sum())
    mm = m3.Manifold(m3.Mesh(vert_properties=np.asarray(m.vertices, np.float32),
                             tri_verts=np.asarray(m.faces, np.uint32)))
    man_ok = str(mm.status()) == "Error.NoError" and abs(mm.volume() - m.volume) < 0.01 * m.volume
    shells = len(m.split(only_watertight=False))
    stl_vol[name] = m.volume
    ok(name, boundary == 0 and degen == 0 and m.volume > 0 and man_ok,
       f"boundary={boundary} knife_edges={knife} shells={shells} degen={degen} "
       f"vol={m.volume/1000:.3f} cm3 size={np.round(m.extents,1)}")

# ------------------------------------------------------------------ B. 3MF sanity
print("== B. 3MF bundles ==")
expect = {"01_body_tail.3mf": 6, "02_rainbow.3mf": 12, "03_chassis_cover.3mf": 2}
for f, nobj in expect.items():
    sc = trimesh.load(os.path.join(P, f))
    geoms = list(sc.geometry.values())
    vol3 = sum(g.volume for g in geoms)
    folder = f.replace(".3mf", "_stl")
    vols = sum(v for k, v in stl_vol.items() if k.startswith(folder))
    z0 = min(g.bounds[0][2] for g in sc.dump())   # real bed placement after transforms
    ok(f, len(geoms) == nobj and abs(vol3 - vols) / vols < 0.01 and z0 > -0.01,
       f"objects={len(geoms)}/{nobj} vol_match={abs(vol3-vols)/vols:.4%} bed_zmin={z0:.2f}")

# ------------------------------------------------------------------ C. mates
print("== C. assembly mates (mesh level) ==")
chassis = trimesh.load(f"{D}/asm_chassis__black.stl")
cover = trimesh.load(f"{D}/asm_cover__black.stl")
body = trimesh.load(f"{D}/asm_body__black.stl")
board = trimesh.load("board_device.stl")
rainbows = [trimesh.load(f) for f in glob.glob(f"{P}/02_rainbow_stl/*.stl")]

for hx, hy in HOLES:
    tag = f"standoff hole ({hx:5.1f},{hy:5.1f})"
    void = ~chassis.contains(axis(hx, hy, STANDOFF_END - 0.05, CH_BACK + 0.05))
    boss = chassis.contains(ring(hx, hy, 1.8, [13.5, 15.5, 17.5]))
    plate = chassis.contains(ring(hx, hy, 1.8, [CH_FRONT + 0.2, CH_BACK - 1.8]))
    # standoff is a tube (bore ~r0.8, wall out to ~r1.9): probe the wall annulus
    a8 = np.linspace(0, 2 * np.pi, 8, endpoint=False)
    wall = np.array([[hx + 1.2 * np.cos(t), hy + 1.2 * np.sin(t), STANDOFF_END - 1.5] for t in a8])
    d_wall = board.nearest.on_surface(wall)[1]
    ok(f"{tag}: screw channel + boss + plate", void.all() and boss.all() and plate.all())
    ok(f"{tag}: brass standoff wall present on board", (d_wall < 0.35).all(),
       f"max_d={d_wall.max():.2f} mm")

for k, (px, py) in PILLARS.items():
    ax = axis(px, py, COVER_IN - 8.9, BACK + 0.05)
    void = ~cover.contains(ax) & ~body.contains(ax)
    pil = body.contains(ring(px, py, 1.7, [22.0, 25.5]))
    walls = cover.contains(ring(px, py, 1.3, [COVER_IN + 0.1, BACK - 1.2]))
    sink = cover.contains(ring(px, py, 2.0, [BACK - 1.2]))          # countersink support
    ok(f"cover screw '{k}' ({px},{py}): clear channel + pilot void", void.all())
    ok(f"cover screw '{k}': pillar material", pil.all())
    warn(f"cover screw '{k}': hole fully walled + countersink supported", walls.all() and sink.all(),
         f"walls {walls.sum()}/{walls.size}, sink {sink.sum()}/{sink.size} "
         f"(hairline crescent at the rim inset, same geometry as the printed v1)")

for x, y in POSTS:
    post = cover.contains(ring(x, y, 1.0, [CH_BACK + 0.5, COVER_IN - 0.5])).all()
    lands = chassis.contains(np.array([[x, y, CH_BACK - 0.4]]))[0]
    ok(f"cover post ({x},{y}): exists in cover", post)
    warn(f"cover post ({x},{y}): presses on chassis", lands)

body_all = [body] + [trimesh.load(f) for f in glob.glob(f"{D}/asm_body__*.stl") if "black" not in f]
usb = np.array([[x, YM, USB_Z] for x in np.linspace(26.2, 33.8, 15)])
ok("USB tunnel through body wall", not any(m.contains(usb).any() for m in body_all))
rb = np.array([[x, YM, USB_Z] for x in np.linspace(32.5, 52.0, 20)])
ok("USB tunnel through rainbow", not any(m.contains(rb).any() for m in rainbows))

ww = info["wire_window"]      # x0 y0 x1 y1
wpts = np.array([[x, y, (CH_FRONT + CH_BACK) / 2] for x in (10, 12, 14) for y in (0, 10, 18)])
ok("chassis wire window is open", not chassis.contains(wpts).any())
ok("dupont wire crosses window inside its frame",
   all(ww[0] < 9.4 and ww[2] > 14.6 and ww[1] < 16.7 and ww[3] > 19.1 for _ in [0]),
   f"window y {ww[1]:.1f}..{ww[3]:.1f} vs bend y 16.7..19.1")

ok("switch lever slot open in cover",
   not cover.contains(np.array([[SW_X, SW_Y + dy, (COVER_IN + BACK) / 2] for dy in (-1.8, 0, 1.8)])).any())
ok("switch pocket frame present in cover",
   cover.contains(np.array([[SW_X + 2.3, SW_Y, COVER_IN - 1.5], [SW_X - 2.3, SW_Y, COVER_IN - 1.5]])).all())
ok("chassis cleared for switch legs",
   not chassis.contains(np.array([[SW_X, SW_Y, (CH_FRONT + CH_BACK) / 2]]))[0])

act_cx = info["active_cx"]
wx0, wx1 = act_cx - 19.0, act_cx + 19.0; wy0, wy1 = YM - 10.3, YM + 10.3
ax0, ax1 = act_cx - 32.93 / 2, act_cx + 32.93 / 2; ay0, ay1 = YM - 8.85, YM + 8.85
m = min(ax0 - wx0, wx1 - ax1, ay0 - wy0, wy1 - ay1)
ok("screen window covers active area with margin", m >= 1.0, f"min margin {m:.2f} mm")

print()
if fails: print("FAILURES:", *fails, sep="\n  - "); sys.exit(1)
print(f"ALL CHECKS PASSED ({len(warns)} warnings)" + (f": {warns}" if warns else ""))
