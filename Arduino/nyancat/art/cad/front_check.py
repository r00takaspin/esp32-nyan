"""Definitive front view of the v2 assembly STLs: orthographic, viewer at -z
looking towards +z, up = +y. In right-handed coords +x appears to the RIGHT."""
import sys, glob, os
sys.path.insert(0, "/Users/voldemar/Documents/deepseek-harness/default-workspace/pylibs")
import numpy as np, trimesh
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

D = os.path.join(os.path.dirname(os.path.abspath(__file__)), "v2", "out")
colors = {"body__gray": "gray", "body__black": "black", "body__beige": "wheat",
          "body__pink": "pink", "rainbowA__red": "red", "rainbowA__orange": "orange",
          "rainbowA__yellow": "gold", "rainbowA__green": "green", "rainbowA__blue": "blue",
          "rainbowA__violet": "violet", "rainbowB__red": "red", "rainbowB__orange": "orange",
          "rainbowB__yellow": "gold", "rainbowB__green": "green", "rainbowB__blue": "blue",
          "rainbowB__violet": "violet", "tail__black": "black", "tail__gray": "gray",
          "chassis__black": "dimgray", "cover__black": "dimgray"}
fig, ax = plt.subplots(figsize=(14, 7))
for key, col in colors.items():
    hits = glob.glob(os.path.join(D, f"asm_{key}.stl"))
    if not hits:
        continue
    m = trimesh.load(hits[0])
    v = m.sample(60000)
    ax.scatter(v[:, 0], v[:, 1], s=1, c=col, label=key, alpha=0.4, edgecolors="none")
ax.set_aspect("equal"); ax.legend(markerscale=5, fontsize=7)
ax.invert_xaxis()   # viewer at -z looking to +z: physical right = -x
ax.set_xlabel("x (INVERTED: physical front view, right side = -x)")
ax.set_ylabel("y")
ax.set_title("v2.1 assembly, TRUE physical front view (screen side)")
plt.tight_layout()
out = os.path.join(D, "..", "views", "front_check.png")
plt.savefig(out, dpi=110)
print("saved", os.path.abspath(out))
# numeric summary: where is the head (gray body pixels) vs window centre
g = trimesh.load(os.path.join(D, "asm_body__gray.stl"))
print("gray part x range: %.1f .. %.1f" % (g.vertices[:, 0].min(), g.vertices[:, 0].max()))
print("gray part z range: %.1f .. %.1f (face side is negative z)" % (g.vertices[:, 2].min(), g.vertices[:, 2].max()))
