"""Pixel Nyan enclosure v2 for Waveshare ESP32-S3-Touch-LCD-1.47 (board with pin headers).

Changes vs v1 (after the first print):
  * body = face + shell + head + all legs in ONE multicolour part (printed face up),
    so the head cannot come off and the screen is aligned by construction;
  * 45 deg self-centring pocket behind the window (also makes the ceiling self-supporting);
  * no BOOT/RST plungers: 1.5 mm paper-clip holes instead;
  * big window in the chassis for female Dupont connectors on VBAT/GND, battery 602535 moved down;
  * snap-in holder for the SS12D00G5 slide switch on the back cover;
  * rainbow flush with the top of the body and as deep as the body.

v2.1 (mirror fix): the old coordinate convention ("x right, y up, front faces -z") is
LEFT-handed, so the printed case came out mirrored: head physically on the LEFT, USB-C
on the RIGHT. The model is now mirrored in x (and the board placement rotated 180 deg
in plane to match) so the physical print matches the docs: head RIGHT, USB-C LEFT.
Note: +x still points to the viewer's LEFT in the physical front view; the pixel grid
is unchanged, only cell() maps it to the other side.

Device coordinates (mm): x = -right (sic, see above), y up (desk at y=0), z backwards (front faces -z).
Pixel grid: cell (c, r) spans x in [(20.5-c)S, (21.5-c)S], y in [(17-r)S, (18-r)S].
"""
import json, os, sys
import cadquery as cq
from shapely.geometry import box, Point, Polygon
from shapely.ops import unary_union

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "v2", "out")
os.makedirs(OUT, exist_ok=True)

S = 2.4
def cell(c, r):
    return box((20.5 - c) * S, (17 - r) * S, (21.5 - c) * S, (18 - r) * S)

# ---------------------------------------------------------------- board (from the Waveshare STEP)
XM, YM = 3.6, 25.2            # module centre (mirrored in v2.1)
GAP = 0.3                     # glass-to-lip gap
FP_BACK = 2.0                 # back plane of the front wall (lip)
ZG = FP_BACK + GAP            # glass front
def bz(zb): return ZG + 3.26 - zb
def bxy(xb, yb): return XM - yb, YM - xb    # board rotated 180 deg in plane vs v2.0
STANDOFF_END = bz(-7.39)
PIN_TIP = bz(-12.35)
USB_Z = 0.5 * (bz(-2.44) + bz(-6.60))
ACT_Z = bz(-5.0)
ACT_X, ACT_YTOP = bxy(11.56, -14.5)
_, ACT_YBOT = bxy(-11.56, -14.5)
HOLES = [bxy(sx * 8.5, sy * 19.5) for sx in (-1, 1) for sy in (-1, 1)]
VBAT_PIN = bxy(8.87, -9.65)
GND_PIN = bxy(8.87, -7.11)
ACTIVE_CX = XM + 44.5 / 2 - 7.03 - 32.93 / 2     # USB end margin 7.03, far end 4.54 (USB end now at +x)
MOD_W, MOD_H, MOD_R = 44.5, 24.55, 5.75

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
# v2: square top-left corner, so the top of the body and the rainbow form one line
pop[(8, 0)] = 'K'; pop[(9, 0)] = 'K'; pop[(8, 1)] = 'K'; pop[(9, 1)] = 'C'
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
RB_SHIFT = -1                 # v2: rainbow one pixel higher -> top flush with the body
rain = {}
for c in range(8):
    off = 1 if c < 4 else 0
    for k in range(6):
        for d in range(2): rain[(c, 1 + off + 2 * k + d + RB_SHIFT)] = k

def U(cells):
    return unary_union([cell(c, r) for (c, r) in cells]) if cells else Polygon()

S_pop = U(pop)
S_head = U(head)
S_body = unary_union([S_pop, S_head, U(front_legs), U(back_legs)]).buffer(0)
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
    out = None
    for p in polys(g.simplify(1e-4)):
        f = cq.Face.makeFromWires(wire(p.exterior.coords, z0), [wire(i.coords, z0) for i in p.interiors])
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
    w = cq.Wire.makePolygon([cq.Vector(x0, y, z) for y, z in poly_yz], close=True)
    return cq.Solid.extrudeLinear(cq.Face.makeFromWires(w), cq.Vector(x1 - x0, 0, 0))
def ycyl(x, z, r, y0, y1):
    return cq.Solid.makeCylinder(r, y1 - y0, cq.Vector(x, y0, z), cq.Vector(0, 1, 0))

# ---------------------------------------------------------------- key dimensions
BACK = 29.5                    # common back plane (bed when the body is printed face up)
COVER_T = 2.0
COVER_IN = BACK - COVER_T      # 27.5: inner face of the inset back cover / pillar ends
WALL = 1.6
CH_FRONT = PIN_TIP + 0.6
CH_T = 2.6
CH_BACK = CH_FRONT + CH_T
CAV_XMAX = 19.0
# v2.2: window edges are explicit. Physical right = model -x: the printed case showed
# dead glass (black void) on that side, so the model-x_min edge moves in by 3.5 mm.
WIN_X0 = ACTIVE_CX - 19.0 + 3.5
WIN_X1 = ACTIVE_CX + 19.0
WIN_CX = 0.5 * (WIN_X0 + WIN_X1)
WIN_W, WIN_H, WIN_R = WIN_X1 - WIN_X0, 20.6, 2.5
TUN_W, TUN_D, TUN_R = 13.6, 8.6, 1.2
POCKET_CLR = 0.25              # module-to-pocket clearance per side
CHAMFER = 7.0                  # depth of the 45 deg self-centring pocket
BAT_L, BAT_W, BAT_T = 35.5, 25.5, 6.4      # 602535 (6 x 25 x 35) + clearance
BAT_CX, BAT_Y0 = 6.3, 9.0             # sits on the bottom wall (no bottom rail)
bat_box = box(BAT_CX - BAT_L / 2, BAT_Y0, BAT_CX + BAT_L / 2, BAT_Y0 + BAT_W)
SW_X, SW_Y = -15.4, YM          # SS12D00G5 on the cover
SW_BODY = (3.7, 8.7, 3.7)      # x, y, z of the switch body pocket (switch ~3.5 x 8.6 x 3.6)
WIRE_WIN = box(3.5, 2 * YM - 60.0, 20.0, 2 * YM - 28.5)       # chassis window for Dupont F-M jumpers on VBAT/GND (board rotated in v2.1: pins now on the bottom edge)

cavity = S_pop.buffer(-WALL, join_style=2).intersection(box(-CAV_XMAX, 0, 60, 60))

# pillars for the cover screws (same logic as v1)
PIL_R = 2.4
def pillar_ok(x, y, r, skin):
    return S_shell.buffer(-skin, join_style=2).contains(Point(x, y).buffer(r))
cands = {"tr": [(-17.9 + 0.1 * i, 37.0 + 0.1 * j) for i in range(30) for j in range(30)],
         "br": [(-16.4 + 0.1 * i, 10.0 + 0.1 * j) for i in range(30) for j in range(30)]}
pillars = {}
for k, cs in cands.items():
    best = None
    for (x, y) in cs:
        c = Point(x, y).buffer(PIL_R)
        if not pillar_ok(x, y, PIL_R, 0.7) or c.distance(bat_box) < 0.5: continue
        if any(Point(hx, hy).distance(Point(x, y)) < 2.5 + PIL_R + 0.4 for hx, hy in HOLES): continue
        score = -c.difference(cavity).area
        if best is None or score < best[0]: best = (score, x, y)
    if best is None: sys.exit(f"no pillar position for {k}")
    pillars[k] = (round(best[1], 2), round(best[2], 2))
pillars["lm"] = (29.3, YM)
print("pillars", pillars)

parts = {}

# ---------------------------------------------------------------- body (multicolour, printed face up)
front = {'K': 0.0, 'C': -0.8, 'P': -1.6, 'M': -2.2,
         'k': -3.0, 'G': -3.6, 'W': -4.2, 'R': -4.0, 'L': -3.0, 'g': -2.2}
core = {'C': 0.4, 'P': 0.4, 'M': 0.4, 'G': -2.4, 'W': -2.4, 'R': -2.4, 'g': -1.4}
colour = {'C': 'beige', 'P': 'pink', 'M': 'pink', 'G': 'gray', 'W': 'beige', 'R': 'pink', 'g': 'gray'}   # 4 colours
cells = {}
cells.update({k: v for k, v in pop.items() if k not in head})
cells.update(head); cells.update(front_legs); cells.update(back_legs)
vol = {}
black = []
for ch in sorted(set(cells.values())):
    poly = U([k for k, v in cells.items() if v == ch])
    if ch in core:
        black.append(prism(poly, core[ch], BACK))
        s = prism(poly, front[ch], core[ch])
        col = colour[ch]
        vol[col] = s if col not in vol else vol[col].fuse(s)
    else:
        black.append(prism(poly, front[ch], BACK))
vol["black"] = fuse(*black)

# cutters: window, cavity with the 45 deg self-centring pocket, USB tunnel, paper-clip holes
WZ = 0.4
win = fuse(prism(rrect(WIN_CX, YM, WIN_W, WIN_H, WIN_R), WZ - 0.01, FP_BACK + 0.1),
           loft_rrect(WIN_CX, YM, WIN_W, WIN_H, WIN_R, WZ,
                      WIN_W + 6.0, WIN_H + 6.0, WIN_R + 3.0, WZ - 3.0))
pw, ph, pr = MOD_W + 2 * POCKET_CLR, MOD_H + 2 * POCKET_CLR, MOD_R + POCKET_CLR
pocket = loft_rrect(XM, YM, pw, ph, pr, FP_BACK,
                    pw + 2 * CHAMFER, ph + 2 * CHAMFER, pr + CHAMFER, FP_BACK + CHAMFER)
cav_solid = fuse(prism(cavity, FP_BACK + CHAMFER - 0.01, BACK + 0.1),
                 pocket.intersect(prism(cavity, FP_BACK, FP_BACK + CHAMFER)))
tun = rrect(YM, USB_Z, TUN_W, TUN_D, TUN_R)
tun_yz = list(tun.exterior.coords)[:-1]
tunnel = yz_prism(tun_yz, XM + MOD_W / 2 + 0.1, 34.0)
clips = fuse(ycyl(ACT_X, ACT_Z, 0.75, ACT_YTOP + 1.0, S * 18 + 0.5),
             ycyl(ACT_X, ACT_Z, 0.75, S * 3 - 0.5, ACT_YBOT - 1.0))
cutters = fuse(win, cav_solid, tunnel, clips)

# cover-screw pillars (pilot holes for M2 self-tapping screws)
pil = []
for k, (x, y) in pillars.items():
    p = cyl(x, y, PIL_R, FP_BACK, COVER_IN)
    if k == "lm":
        pts = [(x + 5, 0), (x + 5, 14.4 - 3.7), (x - 10, 14.4 + 11.3), (x - 10, 0)]
        w = cq.Wire.makePolygon([cq.Vector(a, y - 5, b) for a, b in pts], close=True)
        p = p.cut(cq.Solid.extrudeLinear(cq.Face.makeFromWires(w), cq.Vector(0, 10, 0)))
    if k == "br":
        p = p.cut(yz_prism([(5, 0), (5, 5.7), (30, 30.7), (30, 0)], x - 5, x + 5))
    pil.append(p.intersect(prism(S_shell, FP_BACK, COVER_IN)))
pil_holes = fuse(*[cyl(x, y, 0.85, COVER_IN - 9.0, BACK + 0.1) for (x, y) in pillars.values()])

for k in list(vol):
    s = vol[k].cut(cutters)
    if k == "black":
        s = fuse(s, *[p.cut(tunnel) for p in pil]).cut(pil_holes)
    vol[k] = s.clean()
parts["body"] = vol

# ---------------------------------------------------------------- chassis (black, printed back face down)
ch_out = cavity.buffer(-0.25, join_style=2)
for (x, y) in pillars.values():
    ch_out = ch_out.difference(Point(x, y).buffer(PIL_R + 0.35))
ch_out = ch_out.difference(WIRE_WIN)
ch_out = ch_out.difference(box(SW_X - 2.4, SW_Y - 5.2, SW_X + 2.4, SW_Y + 5.2))      # switch legs
chassis = prism(ch_out, CH_FRONT, CH_BACK)
for (hx, hy) in HOLES:
    chassis = chassis.fuse(cyl(hx, hy, 2.5, STANDOFF_END, CH_FRONT + 0.01))
for (hx, hy) in HOLES:
    chassis = chassis.cut(cyl(hx, hy, 1.15, STANDOFF_END - 0.1, CH_BACK + 0.1))
    chassis = chassis.cut(cyl(hx, hy, 2.1, CH_BACK - 1.6, CH_BACK + 0.1))
parts["chassis"] = {"black": chassis.clean()}

# ---------------------------------------------------------------- back cover (black, inset, printed outside down)
cov_outline = cavity.buffer(-0.2, join_style=2)
cover = prism(cov_outline, COVER_IN, BACK)
bx0, by0, bx1, by1 = bat_box.bounds
feat = unary_union([box(bx0 + 4, by1, 3.0, by1 + 1.0),          # top rail only left of the jumpers
                    box(bx0 - 1.0, by0 + 6, bx0, by1 - 6), box(bx1, by0 + 6, bx1 + 1.0, by1 - 6)])
feat = feat.intersection(cavity.buffer(-0.3, join_style=2))
cover = cover.fuse(prism(feat, COVER_IN - 3.0, COVER_IN + 0.01))
posts = [(27.4, YM - 7.0), (27.4, YM + 7.0), (SW_X, YM - 8.6), (SW_X, YM + 8.6)]
for (x, y) in posts:
    cover = cover.fuse(cyl(x, y, 1.6, CH_BACK, COVER_IN + 0.01))
# snap-in holder: pocket walls + two lips that click over the switch body
bw, bl, bh = SW_BODY
frame = rrect(SW_X, SW_Y, bw + 1.8, bl + 1.8, 0.6).difference(box(SW_X - bw / 2, SW_Y - bl / 2, SW_X + bw / 2, SW_Y + bl / 2))
cover = cover.fuse(prism(frame, COVER_IN - bh - 0.6, COVER_IN + 0.01))
for sx in (-1, 1):                                   # lips on the long walls, 0.35 mm, 45 deg lead-in
    x0 = SW_X + sx * bw / 2
    tri = cq.Wire.makePolygon([cq.Vector(x0, SW_Y - 2.5, COVER_IN - bh - 0.6),
                               cq.Vector(x0 - sx * 0.35, SW_Y - 2.5, COVER_IN - bh),
                               cq.Vector(x0, SW_Y - 2.5, COVER_IN - bh)], close=True)
    cover = cover.fuse(cq.Solid.extrudeLinear(cq.Face.makeFromWires(tri), cq.Vector(0, 5.0, 0)))
cover = cover.cut(prism(rrect(SW_X, SW_Y, 2.0, 4.6, 0.5), COVER_IN - 0.1, BACK + 0.1))   # lever slot
for (x, y) in pillars.values():
    cover = cover.cut(cyl(x, y, 1.2, COVER_IN - 0.1, BACK + 0.1))
    cover = cover.cut(cq.Solid.makeCone(1.2, 2.3, 1.1, cq.Vector(x, y, BACK - 1.1), cq.Vector(0, 0, 1)))
parts["cover"] = {"black": cover.clean()}

# ---------------------------------------------------------------- tail (black + grey, glued on the rainbow)
RB_FRONT, RB_BACK = 0.0, BACK
tv = {}
blk = []
for ch in set(tail.values()):
    poly = U([k for k, v in tail.items() if v == ch])
    if ch == 'g':
        blk.append(prism(poly, -1.4, RB_FRONT)); tv["gray"] = prism(poly, front['g'], -1.4)
    else:
        blk.append(prism(poly, front['L'], RB_FRONT))
tv["black"] = fuse(*blk)
parts["tail"] = tv

# ---------------------------------------------------------------- rainbow: two halves, one colour per stripe
rb_cols = ["red", "orange", "yellow", "green", "blue", "violet"]
tunnel_rb = yz_prism(tun_yz, 32.3, 53.0)
for seg, cols in (("rainbowA", range(0, 4)), ("rainbowB", range(4, 8))):
    v = {}
    for k in range(6):
        poly = U([key for key, val in rain.items() if val == k and key[0] in cols])
        v[rb_cols[k]] = prism(poly, RB_FRONT, RB_BACK).cut(tunnel_rb).clean()
    parts[seg] = v

# ---------------------------------------------------------------- export
info = {"pillars": pillars, "ZG": ZG, "chassis": [CH_FRONT, CH_BACK], "pin_tip": PIN_TIP,
        "standoff_end": STANDOFF_END, "usb_z": USB_Z, "act": [ACT_X, ACT_YTOP, ACT_YBOT, ACT_Z],
        "active_cx": ACTIVE_CX, "bat_box": list(bat_box.bounds), "cover_in": COVER_IN, "back": BACK,
        "switch": [SW_X, SW_Y, *SW_BODY], "wire_window": list(WIRE_WIN.bounds), "volumes": {}}
for name, v in parts.items():
    for col, s in v.items():
        cq.exporters.export(cq.Workplane().add(s), os.path.join(OUT, f"asm_{name}__{col}.stl"),
                            tolerance=0.01, angularTolerance=0.15)
        info["volumes"][f"{name}/{col}"] = round(s.Volume() / 1000, 3)
asm = cq.Assembly()
for name, v in parts.items():
    for col, s in v.items():
        asm.add(s, name=f"{name}_{col}")
asm.export(os.path.join(OUT, "pixel_nyan_v2_assembly.step"))
json.dump(info, open(os.path.join(OUT, "info.json"), "w"), indent=1)
print(json.dumps(info["volumes"], indent=1))
