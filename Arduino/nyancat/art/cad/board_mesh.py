# Tessellate the Waveshare STEP and place it in device coordinates:
# device x = XM + y_b, y = YM + x_b, z = ZG + 3.26 - z_b  (glass front at z = ZG)
import sys, numpy as np, trimesh
import cadquery as cq
XM, YM, ZG = float(sys.argv[1]), float(sys.argv[2]), float(sys.argv[3])
shape = cq.importers.importStep("board/2d3d/ESP32-S3-Touch-LCD-1_47_20250411.stp")
vs, fs = shape.val().tessellate(0.03, 0.3)
V = np.array([(v.x, v.y, v.z) for v in vs]); F = np.array(fs)
D = np.c_[XM + V[:, 1], YM + V[:, 0], ZG + 3.26 - V[:, 2]]
m = trimesh.Trimesh(D, F, process=True)
m.export("cad/board_device.stl")
print(m.bounds, len(m.faces))
