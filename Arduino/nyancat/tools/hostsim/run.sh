#!/bin/sh
# Host-side simulation of the firmware: compiles nyancat.ino + buddy_ui/battery/ble_kb
# against stub Arduino/GFX/BLE headers, runs a scripted wizard/approve/settings
# scenario with assertions and dumps every screen to out/*.ppm (+ .png if Pillow).
set -e
cd "$(dirname "$0")"
mkdir -p out
c++ -std=gnu++17 -Wall -Wno-unused-variable -Wno-unused-function -Istub -I../.. -o sim \
  main.cpp ../../buddy_ui.cpp ../../battery.cpp ../../ble_kb.cpp
./sim
python3 - <<'PY' 2>/dev/null || true
import glob
from PIL import Image
for f in glob.glob('out/*.ppm'):
    Image.open(f).resize((640, 344), Image.NEAREST).save(f[:-4] + '.png')
PY
