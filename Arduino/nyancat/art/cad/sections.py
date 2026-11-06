import glob, os, subprocess, numpy as np, trimesh
from views import COL, SHELLGRAY, render
def section(axis, val, keep_neg, out, yaw, el, dist, tgt):
    n = np.zeros(3); n[axis] = -1.0 if keep_neg else 1.0   # keep side where dot(p - o, n) >= 0
    o = np.zeros(3); o[axis] = val
    os.makedirs("/tmp/sec", exist_ok=True)
    lines = []
    files = sorted(glob.glob("out/asm_*.stl")) + ["board_device.stl"]
    for i, p in enumerate(files):
        m = trimesh.load(p)
        try:
            s = m.slice_plane(o, n, cap=m.is_watertight)
        except Exception:
            s = m.slice_plane(o, n, cap=False)
        if s is None or len(s.faces) == 0: continue
        fn = f"/tmp/sec/{i}.stl"; s.export(fn)
        if p.startswith("board"): c = (0.12, 0.5, 0.28)
        else:
            name, col = os.path.basename(p)[4:-4].split("__")
            c = SHELLGRAY.get(name, COL[col])
        lines.append(f"{fn} {c[0]} {c[1]} {c[2]} 0 0 0")
    open("/tmp/s_cut.txt", "w").write("\n".join(lines) + "\n")
    render("/tmp/s_cut.txt", out, yaw, el, dist, tgt)
section(0, -18.1, True, "views/section_buttons.png", 90, 0.01, 150, (-18.1, 24, 14))     # cut through BOOT/RST plungers
section(1, 25.2, True, "views/section_usb.png", 0, 89.99, 230, (-8, 25.2, 14))          # cut through USB-C / tunnel
section(0, -6.3, True, "views/section_battery.png", 90, 0.01, 150, (-6.3, 24, 14))       # cut through battery bay
