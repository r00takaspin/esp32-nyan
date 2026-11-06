"""Pixel Nyan enclosure for Waveshare ESP32-S3-Touch-LCD-1.47 (with pin headers).

Device coordinates (mm): x right, y up (desk at y=0), z backwards (front faces -z).
Pixel grid: cell (c, r) spans x in [(c-21.5)S, (c-20.5)S], y in [(17-r)S, (18-r)S].
"""
import json, math, os, sys
import cadquery as cq
from shapely.geometry import box, Point, Polygon, MultiPolygon
from shapely.ops import unary_union

OUT = os.path.join(os.path.dirname(__file__), "out")
os.makedirs(OUT, exist_ok=True)

S = 2.4
def cell(c, r):
    return box((c - 21.5) * S, (17 - r) * S, (c - 20.5) * S, (18 - r) * S)

# ---------------------------------------------------------------- board (from STEP)
XM, YM = -3.6, 25.2           # module centre
GAP = 0.3                     # glass-to-lip gap (0.5 mm foam tape compresses to it)
FP_BACK = 2.0                 # face plate back plane
ZG = FP_BACK + GAP            # glass front
def bz(zb):                   # board z -> device z
    return ZG + 3.26 - zb
def bxy(xb, yb):              # board xy -> device xy
    return XM + yb, YM + xb
PCB_BACK = bz(-3.35)
STANDOFF_END = bz(-7.39)
PIN_TIP = bz(-12.35)
USB_Z = 0.5 * (bz(-2.44) + bz(-6.60))
ACT_Z = bz(-5.0)                                  # side switch actuator centre
ACT_X, ACT_YTOP = bxy(11.56, -14.5)
_, ACT_YBOT = bxy(-11.56, -14.5)
HOLES = [bxy(sx * 8.5, sy * 19.5) for sx in (-1, 1) for sy in (-1, 1)]
VBAT_PIN = bxy(8.87, -9.65)
GND_PIN = bxy(8.87, -7.11)
ACTIVE_CX = XM + (44.5 / 2 - 7.03 - 32.93 / 2)   # active area is offset away from USB end

# ---------------------------------------------------------------- pixel art
pop = {}
P0 = 8
for r in range(15):
    for c in range(P0, P0 + 24):
        x = c - P0; y = r
        ch = 'C'
        if y in (0, 14) or x in (0, 23): ch = 'K'
        if (y in (0, 14) and x in (0, 1, 22, 23)) or (y in (1, 13) and x in (0, 23)): ch = None
        if y in (1, 13) and x in (1, 22): ch = 'K'
        if 2 <= y <= 12 and 3 <= x <= 20: ch = 'P'
        if y in (2, 12) and x in (3, 20): ch = 'C'
        if ch: pop[(c, r)] = ch
for x, y in [(3, 4), (5, 2), (9, 2), (14, 2), (18, 3), (20, 6), (4, 9), (3, 11), (7, 12), (12, 12),
             (17, 12), (19, 10), (11, 3), (16, 11), (2, 7), (21, 9), (6, 12), (15, 2)]:
    if pop.get((P0 + x, y)) == 'P': pop[(P0 + x, y)] = 'M'

head_rows = [".kk........kk.",
             "kGGk......kGGk",
             "kGGGk....kGGGk",
             "kGGGGkkkkGGGGk",
             "kGGGGGGGGGGGGk",
             "kGGWkGGGGGWkGk",
             "kGGkkGGGGGkkGk",
             "kRRGGGGGGGGRRk",
             "kRRGkGGkGGkRRk",
             ".kGGkkkkkkkGk.",
             "..kkkkkkkkkk.."]
head = {}
for r, s in enumerate(head_rows):
    for c, ch in enumerate(s):
        if ch != '.': head[(29 + c, 5 + r)] = ch
def legs(c0, rows):
    d = {}
    for r, s in enumerate(rows):
        for c, ch in enumerate(s):
            if ch != '.': d[(c0 + c, 15 + r)] = 'L' if ch == 'k' else 'g'
    return d
front_legs = {}
for c0 in (30, 36):
    front_legs.update({k: v for k, v in legs(c0, ["kggk", "kggk", ".kk."]).items() if k[1] >= 16})
back_legs = {}
for c0 in (9, 16):
    back_legs.update(legs(c0, ["kggk", "kggk", ".kk."]))
tail_rows = ["kkk.....", "kggkk...", ".kkggkkk", "...kkggk", ".....kkk"]
tail = {}
for r, s in enumerate(tail_rows):
    for c, ch in enumerate(s):
        if ch != '.' and 2 + c <= 7: tail[(2 + c, 7 + r)] = 'L' if ch == 'k' else 'g'
rain = {}
for c in range(8):
    off = 1 if c < 4 else 0
    for k in range(6):
        for d in range(2): rain[(c, 1 + off + 2 * k + d)] = k

def U(cells):
    return unary_union([cell(c, r) for (c, r) in cells]) if cells else Polygon()

S_pop = U(pop)
S_head = U(head)
S_shell = S_pop.difference(S_head).buffer(0)

# ---------------------------------------------------------------- cad helpers
def polys(g):
    if g.is_empty: return []
    if isinstance(g, Polygon): return [g]
    return [p for p in getattr(g, "geoms", []) if isinstance(p, Polygon) and p.area > 1e-6]

def wire(coords, z):
    pts = [cq.Vector(x, y, z) for x, y in list(coords)[:-1]]
    return cq.Wire.makePolygon(pts, close=True)

def prism(g, z0, z1):
    """Extrude shapely geometry from z0 to z1."""
    out = None
    for p in polys(g.simplify(1e-4)):
        outer = wire(p.exterior.coords, z0)
        inner = [wire(i.coords, z0) for i in p.interiors]
        f = cq.Face.makeFromWires(outer, inner)
        s = cq.Solid.extrudeLinear(f, cq.Vector(0, 0, z1 - z0))
        out = s if out is None else out.fuse(s)
    return out

def fuse(*shapes):
    sh = [s for s in shapes if s is not None]
    out = sh[0]
    for s in sh[1:]: out = out.fuse(s)
    return out.clean()

def rrect(cx, cy, w, h, r, n=8):
    return box(cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2).buffer(-r).buffer(r, quad_segs=n)

def cyl(x, y, r, z0, z1):
    return prism(Point(x, y).buffer(r, quad_segs=16), z0, z1)

def loft_rrect(cx, cy, w0, h0, r0, z0, w1, h1, r1, z1):
    a = polys(rrect(cx, cy, w0, h0, r0))[0]; b = polys(rrect(cx, cy, w1, h1, r1))[0]
    return cq.Solid.makeLoft([wire(a.exterior.coords, z0), wire(b.exterior.coords, z1)], True)

def yz_prism(poly_yz, x0, x1):
    """Polygon given in (y, z), extruded along x."""
    pts = [cq.Vector(x0, y, z) for y, z in poly_yz]
    w = cq.Wire.makePolygon(pts, close=True)
    return cq.Solid.extrudeLinear(cq.Face.makeFromWires(w), cq.Vector(x1 - x0, 0, 0))

def ycyl(x, z, r, y0, y1):
    return cq.Solid.makeCylinder(r, y1 - y0, cq.Vector(x, y0, z), cq.Vector(0, 1, 0))

# ---------------------------------------------------------------- key dimensions
SHELL_BACK = 27.5
COVER_T = 2.0
BACK = SHELL_BACK + COVER_T           # 29.5: common back plane
WALL = 1.6
CH_FRONT = PIN_TIP + 0.6              # chassis keeps pin tips away from the battery
CH_T = 2.6
CH_BACK = CH_FRONT + CH_T
BAT = (35.5, 30.5, CH_BACK, SHELL_BACK)   # pocket for a 603035 LiPo (6 x 30 x 35 mm)
CAV_XMAX = 19.0
SW_X, SW_Y = 15.4, YM                # SS12D00 slide switch on the cover
WIN_W, WIN_H, WIN_R = 38.0, 20.6, 2.5
TUN_W, TUN_D, TUN_R = 13.6, 8.6, 1.2     # USB-C plug tunnel (height along y, depth along z)
DOWEL = 0.95                          # radius for 1.75 mm filament dowels

cavity = S_pop.buffer(-WALL, join_style=2).intersection(box(-60, 0, CAV_XMAX, 60))

# pillars for the cover screws: pick positions with enough skin inside the silhouette
def pillar_ok(x, y, r, skin):
    c = Point(x, y).buffer(r)
    return S_shell.buffer(-skin, join_style=2).contains(c)
PIL_R = 2.4
cands = {
    "tr": [(15.0 + 0.1 * i, 37.0 + 0.1 * j) for i in range(30) for j in range(30)],
    "br": [(13.5 + 0.1 * i, 10.0 + 0.1 * j) for i in range(30) for j in range(30)],
}
BAT_CX = -6.3
bat_box = box(BAT_CX - BAT[0] / 2, YM - BAT[1] / 2, BAT_CX + BAT[0] / 2, YM + BAT[1] / 2)
pillars = {}
for k, cs in cands.items():
    best = None
    for (x, y) in cs:
        c = Point(x, y).buffer(PIL_R)
        if not pillar_ok(x, y, PIL_R, 0.7): continue
        if c.distance(bat_box) < 0.5: continue
        if any(Point(hx, hy).distance(Point(x, y)) < 2.5 + PIL_R + 0.4 for hx, hy in HOLES): continue
        # prefer touching the wall (less free-standing), i.e. close to the cavity boundary
        score = -c.difference(cavity).area
        if best is None or score < best[0]: best = (score, x, y)
    if best is None: sys.exit(f"no pillar position for {k}")
    pillars[k] = (round(best[1], 2), round(best[2], 2))
pillars["lm"] = (-29.3, YM)      # left wall, behind the USB tunnel (corbel)
print("pillars", pillars)

# ---------------------------------------------------------------- parts
parts = {}       # name -> {colour: shape}

# window cutter (straight through the lip, 45 deg chamfer towards the front)
WZ = 0.4
win = fuse(prism(rrect(ACTIVE_CX, YM, WIN_W, WIN_H, WIN_R), WZ - 0.01, FP_BACK + 0.1),
           loft_rrect(ACTIVE_CX, YM, WIN_W, WIN_H, WIN_R, WZ,
                      WIN_W + 2 * 3.0, WIN_H + 2 * 3.0, WIN_R + 3.0, WZ - 3.0))

# --- face plate (multicolour, printed face up)
ZC = 0.4
fp_cells = {k: v for k, v in pop.items() if k not in head}
front = {'K': 0.0, 'C': -0.8, 'P': -1.6, 'M': -2.2}
fp = {}
fp["black"] = fuse(prism(S_shell, ZC, FP_BACK),
                   prism(U([k for k, v in fp_cells.items() if v == 'K']), front['K'], ZC))
for ch, col in (('C', 'beige'), ('P', 'pink'), ('M', 'magenta')):
    fp[col] = prism(U([k for k, v in fp_cells.items() if v == ch]), front[ch], ZC)

# dowel spots on the shell rim: points with >= 1.4 mm material around them
rim = S_shell.difference(cavity)
dowels = []
for (x, y) in [(22.0, 37.0), (-29.2, 40.0), (-29.2, 10.6), (19.6, 8.6), (-31.6, 25.2 + 9.5), (-31.6, 25.2 - 9.5)]:
    if rim.buffer(-0.3).contains(Point(x, y).buffer(DOWEL + 0.35)):
        dowels.append((x, y))
dowels += [pillars["tr"]]
# extra pads in the left cavity corners (front 5 mm only, clear of the module and the USB plug)
mod_fp = rrect(XM, YM, 44.5, 24.55, 5.75).buffer(1.0)
pad_zone = S_shell.buffer(-1.6, join_style=2).difference(mod_fp).difference(box(-40, YM - 8, -20, YM + 8))
PADS = []
for target in ((-30.0, 11.0), (-30.0, 39.4)):
    best = min(((Point(target).distance(Point(x, y)), x, y)
                for x in [-31 + 0.1 * i for i in range(60)] for y in [8 + 0.1 * j for j in range(340)]
                if pad_zone.contains(Point(x, y))), default=None)
    if best: PADS.append((round(best[1], 2), round(best[2], 2)))
dowels += PADS
print("pads", PADS)
print("rim dowels", dowels)
for k in fp:
    s = fp[k].cut(win)
    for (x, y) in dowels:
        s = s.cut(cyl(x, y, DOWEL + 0.03, FP_BACK - 1.4, FP_BACK + 0.1))
    fp[k] = s.clean()
parts["faceplate"] = fp

# --- shell (black, printed front rim down)
shell = prism(S_shell, FP_BACK, SHELL_BACK).cut(prism(cavity, FP_BACK - 0.1, SHELL_BACK + 0.1))
pil_shapes = []
for k, (x, y) in pillars.items():
    p = cyl(x, y, PIL_R, FP_BACK, SHELL_BACK)
    if k == "lm":   # 45 deg corbel against the left wall, starts behind the USB plug
        pts = [(x - 5, 0), (x - 5, 14.4 - 3.7), (x + 10, 14.4 + 11.3), (x + 10, 0)]
        w = cq.Wire.makePolygon([cq.Vector(a, y - 5, b) for a, b in pts], close=True)
        p = p.cut(cq.Solid.extrudeLinear(cq.Face.makeFromWires(w), cq.Vector(0, 10, 0)))
    if k == "br":   # starts behind the module with a 45 deg corbel against the bottom wall
        p = p.cut(yz_prism([(5, 0), (5, 5.7), (30, 30.7), (30, 0)], x - 5, x + 5))
    p = p.intersect(prism(S_shell, FP_BACK, SHELL_BACK))
    pil_shapes.append(p)
shell = fuse(shell, *pil_shapes,
             *[cyl(x, y, 2.0, FP_BACK, FP_BACK + 5.0).intersect(prism(S_shell, FP_BACK, SHELL_BACK)) for (x, y) in PADS])
for (x, y) in pillars.values():
    shell = shell.cut(cyl(x, y, 0.85, SHELL_BACK - 9.0, SHELL_BACK + 0.1))
# side-switch plungers (top and bottom walls)
for y0, y1 in ((S * 17 - 2.5, S * 18 + 0.5), (S * 3 - 0.5, S * 3 + 2.5)):
    shell = shell.cut(ycyl(ACT_X, ACT_Z, 1.25, y0, y1))
# USB-C tunnel through the left wall
tun = rrect(YM, USB_Z, TUN_W, TUN_D, TUN_R)
tun_yz = list(tun.exterior.coords)[:-1]
shell = shell.cut(yz_prism(tun_yz, -34.0, -29.0))
for (x, y) in dowels:
    shell = shell.cut(cyl(x, y, DOWEL + 0.03, FP_BACK - 0.1, FP_BACK + 3.0))
parts["shell"] = {"black": shell.clean()}

# --- chassis (black, printed back face down)
ch_out = cavity.buffer(-0.25, join_style=2)
ch_out = ch_out.difference(S_head.difference(box(17.9, YM - 12.6, CAV_XMAX, 30.0)).buffer(0.25, join_style=2))
for (x, y) in pillars.values():
    ch_out = ch_out.difference(Point(x, y).buffer(PIL_R + 0.35))
ch_out = ch_out.difference(box(VBAT_PIN[0] - 3.5, YM + 12.0, GND_PIN[0] + 3.0, 60))   # battery wires
ch_out = ch_out.difference(box(SW_X - 2.3, SW_Y - 5.0, SW_X + 2.3, SW_Y + 5.0))        # switch legs
chassis = prism(ch_out, CH_FRONT, CH_BACK)
for (hx, hy) in HOLES:
    chassis = chassis.fuse(cyl(hx, hy, 2.5, STANDOFF_END, CH_FRONT + 0.01))
for (hx, hy) in HOLES:
    chassis = chassis.cut(cyl(hx, hy, 1.15, STANDOFF_END - 0.1, CH_BACK + 0.1))
    chassis = chassis.cut(cyl(hx, hy, 2.1, CH_BACK - 1.6, CH_BACK + 0.1))
parts["chassis"] = {"black": chassis.clean()}

# --- back cover (black, printed outside face down)
cover = prism(S_shell, SHELL_BACK, BACK)
bx0, by0, bx1, by1 = bat_box.bounds
feat = []
feat.append(box(bx0 + 4, by0 - 1.0, bx1 - 4, by0))          # battery rails
feat.append(box(bx0 + 4, by1, bx1 - 4, by1 + 1.0))
feat.append(box(bx0 - 1.0, YM - 7, bx0, YM + 7))             # end stops
feat.append(box(bx1, YM - 7, bx1 + 1.0, YM + 7))
feat = unary_union(feat).intersection(cavity.buffer(-0.3, join_style=2))
cover = cover.fuse(prism(feat, SHELL_BACK - 3.0, SHELL_BACK + 0.01))
# posts pressing the chassis forward
posts = [(-27.4, YM - 7.0), (-27.4, YM + 7.0), (SW_X, YM - 8.6), (SW_X, YM + 8.6)]
for (x, y) in posts:
    cover = cover.fuse(cyl(x, y, 1.6, CH_BACK, SHELL_BACK + 0.01))
# slide switch pocket: body 8.6 x 3.6 x 3.6, lever 1.5 x 1.5 travelling 1.5 mm along y
sw_frame = rrect(SW_X, SW_Y, 3.8 + 1.6, 8.8 + 1.6, 0.6).difference(box(SW_X - 1.9, SW_Y - 4.4, SW_X + 1.9, SW_Y + 4.4))
cover = cover.fuse(prism(sw_frame, SHELL_BACK - 2.4, SHELL_BACK + 0.01))
cover = cover.cut(prism(rrect(SW_X, SW_Y, 1.9, 3.6, 0.5), SHELL_BACK - 0.1, BACK + 0.1))
for (x, y) in pillars.values():
    cover = cover.cut(cyl(x, y, 1.2, SHELL_BACK - 0.1, BACK + 0.1))
    cover = cover.cut(cq.Solid.makeCone(1.2, 2.3, 1.1, cq.Vector(x, y, BACK - 1.1), cq.Vector(0, 0, 1)))
parts["cover"] = {"black": cover.clean()}

# --- head (+ front legs), multicolour, printed face up
hfront = {'k': -3.0, 'G': -3.6, 'W': -4.2, 'R': -4.0, 'L': -3.0, 'g': -2.2}
hcore = {'G': -2.4, 'W': -2.4, 'R': -2.4, 'g': -1.4}
hcol = {'G': 'gray', 'W': 'white', 'R': 'cheek', 'g': 'gray'}
def relief(cells, back, name):
    vol = {}
    blk = []
    for ch in set(cells.values()):
        poly = U([k for k, v in cells.items() if v == ch])
        if ch in hcore:
            blk.append(prism(poly, hcore[ch], back))
            col = hcol[ch]
            s = prism(poly, hfront[ch], hcore[ch])
            vol[col] = s if col not in vol else vol[col].fuse(s)
        else:
            blk.append(prism(poly, hfront[ch], back))
    vol["black"] = fuse(*blk)
    return vol
hd = relief({**head, **front_legs}, BACK, "head")
groove = prism(box(18.0 - 0.1, YM - 12.6, CAV_XMAX, 30.0), FP_BACK, BACK + 0.1)
hd = {k: v.cut(groove).clean() for k, v in hd.items()}
parts["head"] = hd

# --- tail (glued on the rainbow) and back legs (glued under the pop-tart)
RB_FRONT, RB_BACK = 3.0, 16.0
parts["tail"] = relief(tail, RB_FRONT, "tail")
parts["backlegs"] = relief(back_legs, BACK, "legs")

# --- rainbow: two segments, every stripe solid colour (printed standing -> colour by height)
rb_cols = ["red", "orange", "yellow", "green", "blue", "violet"]
tunnel_rb = yz_prism([(y, z) for y, z in tun_yz], -53.0, -32.3)
for seg, cols in (("rainbowA", range(0, 4)), ("rainbowB", range(4, 8))):
    vol = {}
    for k in range(6):
        poly = U([key for key, v in rain.items() if v == k and key[0] in cols])
        vol[rb_cols[k]] = prism(poly, RB_FRONT, RB_BACK).cut(tunnel_rb).clean()
    parts[seg] = vol

# --- plungers for BOOT / RST (black, printed cap down). Axis along y, at (ACT_X, ACT_Z).
TOP_OUT, BOT_OUT = S * 18, S * 3          # outer surfaces of top / bottom walls
def ysolid_cyl(r, y0, y1):
    return ycyl(ACT_X, ACT_Z, r, y0, y1)
def ycone(r0, r1, y0, y1):
    return cq.Solid.makeCone(r0, r1, y1 - y0, cq.Vector(ACT_X, y0, ACT_Z), cq.Vector(0, 1, 0))
def capbox(y0, y1):
    return cq.Solid.makeBox(2.3, y1 - y0, 2.3, cq.Vector(ACT_X - 1.15, y0, ACT_Z - 1.15))
# top: rests on the actuator by gravity, cap 0.8 mm above the surface
tip = ACT_YTOP
pt = fuse(capbox(TOP_OUT + 0.8, TOP_OUT + 1.8), ysolid_cyl(1.0, tip, TOP_OUT + 0.81),
          ysolid_cyl(2.0, TOP_OUT - WALL - 2.2, TOP_OUT - WALL - 1.4),
          ycone(2.0, 1.0, TOP_OUT - WALL - 1.4, TOP_OUT - WALL - 0.4))
# bottom: collar cone seats in the hole edge, tip 0.35 mm below the actuator
yk = BOT_OUT + WALL - 0.25
pb = fuse(capbox(BOT_OUT - 1.8, BOT_OUT - 0.8), ysolid_cyl(1.0, BOT_OUT - 0.81, ACT_YBOT - 0.35),
          ycone(1.0, 2.0, yk, yk + 1.0), ysolid_cyl(2.0, yk + 1.0, yk + 1.8))
parts["plunger_top"] = {"black": pt}
parts["plunger_bot"] = {"black": pb}

# ---------------------------------------------------------------- export (assembly position)
info = {"pillars": pillars, "dowels": dowels, "ZG": ZG, "chassis": [CH_FRONT, CH_BACK],
        "pin_tip": PIN_TIP, "standoff_end": STANDOFF_END, "usb_z": USB_Z, "act": [ACT_X, ACT_YTOP, ACT_YBOT, ACT_Z],
        "active_cx": ACTIVE_CX, "bat_box": list(bat_box.bounds)}
for name, vol in parts.items():
    for col, s in vol.items():
        cq.exporters.export(cq.Workplane().add(s), os.path.join(OUT, f"asm_{name}__{col}.stl"),
                            tolerance=0.01, angularTolerance=0.15)
        info.setdefault("volumes", {})[f"{name}/{col}"] = round(s.Volume() / 1000, 3)
asm = cq.Assembly()
for name, vol in parts.items():
    for col, s in vol.items():
        asm.add(s, name=f"{name}_{col}")
asm.save(os.path.join(OUT, "pixel_nyan_assembly.step"))
json.dump(info, open(os.path.join(OUT, "info.json"), "w"), indent=1)
print(json.dumps(info["volumes"], indent=1))
