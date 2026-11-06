import glob, os, subprocess, sys
COL = {"black": (0.16, 0.16, 0.18), "beige": (0.95, 0.75, 0.56), "pink": (0.96, 0.55, 0.84), "magenta": (0.88, 0.18, 0.53),
       "gray": (0.56, 0.57, 0.6), "white": (0.95, 0.95, 0.93), "cheek": (0.94, 0.57, 0.57), "red": (0.89, 0.13, 0.17),
       "orange": (0.96, 0.56, 0.12), "yellow": (0.96, 0.86, 0.12), "green": (0.23, 0.78, 0.23), "blue": (0.12, 0.56, 0.9),
       "violet": (0.42, 0.25, 0.84)}
SHELLGRAY = {"shell": (0.30, 0.30, 0.33), "chassis": (0.25, 0.45, 0.70), "cover": (0.36, 0.36, 0.40)}
EXPL = {"faceplate": (0, 0, -22), "head": (26, 0, -10), "rainbowA": (-34, 0, -4), "rainbowB": (-22, 0, -4),
        "tail": (-24, 0, -18), "backlegs": (0, -16, 0), "chassis": (0, 0, 30), "cover": (0, 0, 58),
        "plunger_top": (0, 12, 0), "plunger_bot": (0, -10, 0), "shell": (0, 0, 0)}
def scene(fn, explode=False, board=True, hide=(), tint=False):
    with open(fn, "w") as f:
        for p in sorted(glob.glob("out/asm_*.stl")):
            name, col = os.path.basename(p)[4:-4].split("__")
            if name in hide: continue
            c = SHELLGRAY.get(name, COL[col]) if tint else COL[col]
            d = EXPL.get(name, (0, 0, 0)) if explode else (0, 0, 0)
            f.write(f"{p} {c[0]} {c[1]} {c[2]} {d[0]} {d[1]} {d[2]}\n")
        if board:
            d = (0, 0, 14) if explode else (0, 0, 0)
            f.write(f"board_device.stl 0.12 0.45 0.25 {d[0]} {d[1]} {d[2]}\n")
            if explode:
                pass
def render(sc, out, yaw, el, dist, tgt, clip=None, W=1800, H=1200):
    args = ["./raster", sc, str(W), str(H), str(yaw), str(el), str(dist), *map(str, tgt), "/tmp/r.ppm"]
    if clip: args += [str(clip[0]), str(clip[1])]
    subprocess.run(args, check=True)
    subprocess.run(["magick", "/tmp/r.ppm", "-resize", "50%", out], check=True)
if __name__ == "__main__":
  os.makedirs("views", exist_ok=True)
  scene("/tmp/s_asm.txt"); render("/tmp/s_asm.txt", "views/assembled_front.png", -28, 18, 260, (-2, 22, 12))
  render("/tmp/s_asm.txt", "views/assembled_back.png", 150, 20, 260, (-2, 22, 12))
  scene("/tmp/s_exp.txt", explode=True, tint=True); render("/tmp/s_exp.txt", "views/exploded.png", -35, 22, 400, (-2, 22, 18))
  scene("/tmp/s_sec.txt", tint=True, hide=("tail",))
  render("/tmp/s_sec.txt", "views/section_y25.png", 0, 90 - 0.01, 260, (-6, 25.2, 14), clip=(1, 25.2))   # top-down cut at USB height
  render("/tmp/s_sec.txt", "views/section_x-18.png", -90, 0, 220, (-18.1, 25, 14), clip=(0, -18.1))     # side cut through buttons
