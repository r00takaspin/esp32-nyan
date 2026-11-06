"""Place every part in its print orientation and write per-job 3MF (+ per-colour STL)."""
import glob, os, zipfile, json, numpy as np, trimesh

OUT = "print"
os.makedirs(OUT, exist_ok=True)
R_FACEUP = np.array([[1, 0, 0], [0, -1, 0], [0, 0, -1]])   # front (-z) up        (Z = -z)
R_FRONTDOWN = np.eye(3)                                    # front rim on the bed (Z = z)
R_YDOWN_UP = np.array([[1, 0, 0], [0, 0, 1], [0, -1, 0]])  # device top on the bed (Z = -y)
R_YUP = np.array([[1, 0, 0], [0, 0, -1], [0, 1, 0]])       # standing as on the desk (Z = y)
ORIENT = {"faceplate": R_FACEUP, "head": R_FACEUP, "tail": R_FACEUP, "chassis": R_FACEUP, "cover": R_FACEUP,
          "shell": R_FRONTDOWN, "backlegs": R_YDOWN_UP, "rainbowA": R_YUP, "rainbowB": R_YUP,
          "plunger_top": R_YDOWN_UP, "plunger_bot": R_YUP}
HEX = {"black": "#202024", "beige": "#F0BF8E", "pink": "#F58AD6", "magenta": "#E02E86", "gray": "#8C8F96",
       "white": "#F1F1EC", "cheek": "#F0908F", "red": "#E3202B", "orange": "#F5901E", "yellow": "#F6DC1E",
       "green": "#3AC63A", "blue": "#1E8EE6", "violet": "#6A3ED6"}
JOBS = [
    ("01_faceplate", [("faceplate", 1)], ["black", "beige", "pink", "magenta"]),
    ("02_head", [("head", 1)], ["black", "gray", "white", "cheek"]),
    ("03_tail_backlegs", [("tail", 1), ("backlegs", 1)], ["black", "gray"]),
    ("04_rainbow", [("rainbowA", 1), ("rainbowB", 1)], ["violet", "blue", "green", "yellow", "orange", "red"]),
    ("05_black_parts", [("shell", 1), ("chassis", 1), ("cover", 1), ("plunger_top", 2), ("plunger_bot", 2)], ["black"]),
]

def load_part(name):
    vols = {}
    for f in glob.glob(f"out/asm_{name}__*.stl"):
        col = os.path.basename(f)[:-4].split("__")[1]
        vols[col] = trimesh.load(f)
    return vols

def orient(vols, R):
    out = {}
    for c, m in vols.items():
        m = m.copy(); T = np.eye(4); T[:3, :3] = R; m.apply_transform(T); out[c] = m
    lo = np.min([m.bounds[0] for m in out.values()], axis=0)
    for m in out.values(): m.apply_translation([-lo[0], -lo[1], -lo[2]])
    return out

def layout(items, width=170.0, gap=8.0):
    x = y = row_h = 0.0
    placed = []
    for name, vols in items:
        hi = np.max([m.bounds[1] for m in vols.values()], axis=0)
        if x > 0 and x + hi[0] > width: x = 0; y += row_h + gap; row_h = 0
        for m in vols.values(): m.apply_translation([x, y, 0])
        placed.append((name, vols)); x += hi[0] + gap; row_h = max(row_h, hi[1])
    lo = np.min([m.bounds[0] for _, v in placed for m in v.values()], axis=0)
    hi = np.max([m.bounds[1] for _, v in placed for m in v.values()], axis=0)
    c = (lo + hi) / 2
    for _, v in placed:
        for m in v.values(): m.apply_translation([128 - c[0], 128 - c[1], 0])
    return placed, hi - lo

def write_3mf(path, placed, colours):
    ext = {c: i + 1 for i, c in enumerate(colours)}
    res, comps, build, cfg = [], [], [], []
    oid = 1
    for pname, vols in placed:
        parts = []
        for col in [c for c in colours if c in vols]:
            m = vols[col]
            vs = "".join(f'<vertex x="{a:.4f}" y="{b:.4f}" z="{c:.4f}"/>' for a, b, c in m.vertices)
            ts = "".join(f'<triangle v1="{a}" v2="{b}" v3="{c}"/>' for a, b, c in m.faces)
            res.append(f'<object id="{oid}" name="{pname}_{col}" type="model"><mesh><vertices>{vs}</vertices>'
                       f'<triangles>{ts}</triangles></mesh></object>')
            parts.append((oid, col)); oid += 1
        cid = oid; oid += 1
        cs = "".join(f'<component objectid="{i}"/>' for i, _ in parts)
        res.append(f'<object id="{cid}" name="{pname}" type="model"><components>{cs}</components></object>')
        build.append(f'<item objectid="{cid}"/>')
        ps = "".join(f'<part id="{i}" subtype="normal_part"><metadata key="name" value="{col}"/>'
                     f'<metadata key="extruder" value="{ext[col]}"/></part>' for i, col in parts)
        cfg.append(f'<object id="{cid}"><metadata key="name" value="{pname}"/>'
                   f'<metadata key="extruder" value="{ext[parts[0][1]]}"/>{ps}</object>')
    model = ('<?xml version="1.0" encoding="UTF-8"?>\n<model unit="millimeter" xml:lang="en-US" '
             'xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02">'
             '<metadata name="Title">Pixel Nyan Agent Buddy</metadata>'
             f'<resources>{"".join(res)}</resources><build>{"".join(build)}</build></model>')
    ct = ('<?xml version="1.0" encoding="UTF-8"?>\n<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
          '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>'
          '<Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/>'
          '<Default Extension="config" ContentType="text/xml"/></Types>')
    rels = ('<?xml version="1.0" encoding="UTF-8"?>\n<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
            '<Relationship Target="/3D/3dmodel.model" Id="rel0" '
            'Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/></Relationships>')
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("[Content_Types].xml", ct)
        z.writestr("_rels/.rels", rels)
        z.writestr("3D/3dmodel.model", model)
        z.writestr("Metadata/model_settings.config",
                   '<?xml version="1.0" encoding="UTF-8"?>\n<config>' + "".join(cfg) + '</config>')

summary = {}
for job, plist, colours in JOBS:
    items = []
    for pname, n in plist:
        for k in range(n):
            items.append((pname if n == 1 else f"{pname}_{k + 1}", orient(load_part(pname), ORIENT[pname])))
    placed, size = layout(items)
    write_3mf(os.path.join(OUT, f"{job}.3mf"), placed, colours)
    d = os.path.join(OUT, job + "_stl"); os.makedirs(d, exist_ok=True)
    vol = {}
    for pname, vols in placed:
        for col, m in vols.items():
            m.export(os.path.join(d, f"{pname}__{col}.stl"))
            vol[col] = vol.get(col, 0) + m.volume / 1000
    summary[job] = {"plate_mm": [round(float(s), 1) for s in size], "cm3": {k: round(v, 2) for k, v in vol.items()},
                    "filaments": {i + 1: (c, HEX[c]) for i, c in enumerate(colours)}}
json.dump(summary, open(os.path.join(OUT, "jobs.json"), "w"), indent=1, ensure_ascii=False)
print(json.dumps(summary, indent=1, ensure_ascii=False))
