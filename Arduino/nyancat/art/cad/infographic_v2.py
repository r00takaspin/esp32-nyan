"""Renders + an assembly infographic for Pixel Nyan v2 (uses ./raster).

v2.1: the model is mirrored in x (head physically RIGHT, USB-C LEFT), and the
renders are flopped horizontally: the raster camera is left-handed, so raw
output was a mirror image of the physical device. Labels use the same flop in
proj() so they land on the right spots.
"""
import glob, os, subprocess, numpy as np
from PIL import Image, ImageDraw, ImageFont

D = "v2/out"; V = "v2/views"; os.makedirs(V, exist_ok=True)
COL = {"black": (0.16, 0.16, 0.18), "beige": (0.95, 0.80, 0.62), "pink": (0.96, 0.55, 0.84), "magenta": (0.80, 0.15, 0.40),
       "gray": (0.56, 0.57, 0.6), "white": (0.95, 0.95, 0.93), "cheek": (0.94, 0.57, 0.57), "red": (0.89, 0.13, 0.17),
       "orange": (0.96, 0.56, 0.12), "yellow": (0.96, 0.86, 0.12), "green": (0.23, 0.78, 0.23), "blue": (0.12, 0.56, 0.9),
       "violet": (0.42, 0.25, 0.84)}
DUM = {"battery": (0.55, 0.62, 0.72), "dupont": (0.85, 0.15, 0.15), "switch": (0.12, 0.12, 0.14),
       "switch_lever": (0.95, 0.95, 0.95), "screws_chassis": (0.75, 0.75, 0.78), "screws_cover": (0.75, 0.75, 0.78),
       "screen": (0.10, 0.22, 0.80)}
TINT = {"chassis": (0.25, 0.45, 0.70), "cover": (0.38, 0.38, 0.42)}

def scene(fn, parts=None, dummies=(), board=True, off=None, tint=False, hide_cols=(), board_col=(0.13, 0.50, 0.30)):
    off = off or {}
    L = []
    for p in sorted(glob.glob(f"{D}/asm_*.stl")):
        name, col = os.path.basename(p)[4:-4].split("__")
        if parts is not None and name not in parts: continue
        c = TINT.get(name, COL[col]) if tint else COL[col]
        d = off.get(name, (0, 0, 0))
        L.append(f"{p} {c[0]} {c[1]} {c[2]} {d[0]} {d[1]} {d[2]}")
    for dn in dummies:
        c = DUM[dn]; d = off.get(dn, off.get("board", (0, 0, 0)) if dn in ("dupont", "screen") else (0, 0, 0))
        if dn.startswith("switch"): d = off.get("cover", (0, 0, 0))
        if dn == "screws_chassis": d = off.get("chassis", (0, 0, 0))
        if dn == "screws_cover": d = off.get("screws_cover", off.get("cover", (0, 0, 0)))
        if dn == "battery": d = off.get("battery", off.get("cover", (0, 0, 0)))
        L.append(f"{D}/dummy_{dn}.stl {c[0]} {c[1]} {c[2]} {d[0]} {d[1]} {d[2]}")
    if board:
        d = off.get("board", (0, 0, 0))
        L.append(f"board_device.stl {board_col[0]} {board_col[1]} {board_col[2]} {d[0]} {d[1]} {d[2]}")
    open(fn, "w").write("\n".join(L) + "\n")

def cam(yaw, el, dist, tgt, W):
    yaw, el = np.radians(yaw), np.radians(el); tg = np.array(tgt, float)
    c = tg + dist * np.array([np.cos(el) * np.sin(yaw), np.sin(el), -np.cos(el) * np.cos(yaw)])
    fw = (tg - c) / np.linalg.norm(tg - c); rt = np.cross([0, 1, 0], fw); rt /= np.linalg.norm(rt); up = np.cross(fw, rt)
    return c, fw, rt, up, W * 1.6

def render(sc, out, yaw, el, dist, tgt, W=1600, H=1100):
    subprocess.run(["./raster", sc, str(W), str(H), str(yaw), str(el), str(dist), *map(str, tgt), "/tmp/r.ppm"], check=True)
    subprocess.run(["magick", "/tmp/r.ppm", "-resize", "50%", "-flop", out], check=True)   # v2.1: un-mirror
    c, fw, rt, up, foc = cam(yaw, el, dist, tgt, W)
    def proj(p):
        q = np.array(p, float) - c; z = q @ fw
        return ((W / 2 - foc * (q @ rt) / z) / 2, (H / 2 - foc * (q @ up) / z) / 2)        # v2.1: flopped x
    return proj

FONT = "/System/Library/Fonts/Supplemental/Arial.ttf"; FB = "/System/Library/Fonts/Supplemental/Arial Bold.ttf"
f = lambda s, b=False: ImageFont.truetype(FB if b else FONT, s)
ACC = (226, 52, 120)

def label(im, proj, p3, text, dxy=(40, -40), size=22):
    d = ImageDraw.Draw(im); x, y = proj(p3); tx, ty = x + dxy[0], y + dxy[1]
    d.line([(x, y), (tx, ty)], fill=ACC, width=3); d.ellipse([x - 5, y - 5, x + 5, y + 5], fill=ACC)
    w = d.textlength(text, font=f(size, True))
    bx = tx if dxy[0] >= 0 else tx - w - 16
    d.rounded_rectangle([bx, ty - 16, bx + w + 16, ty + 16], 8, fill=(255, 255, 255), outline=ACC, width=2)
    d.text((bx + 8, ty - 13), text, font=f(size, True), fill=(30, 30, 34))

def arrow(im, p0, p1, width=6):
    d = ImageDraw.Draw(im); d.line([p0, p1], fill=ACC, width=width)
    v = np.array(p1) - np.array(p0); v = v / (np.linalg.norm(v) + 1e-9); n = np.array([-v[1], v[0]])
    tip = np.array(p1); a = tip - v * 22 + n * 12; b = tip - v * 22 - n * 12
    d.polygon([tuple(tip), tuple(a), tuple(b)], fill=ACC)

ALL = ["body", "chassis", "cover", "tail", "rainbowA", "rainbowB"]
shots = {}
# hero front / back
scene("/tmp/a.txt", ALL, ("screen", "switch_lever"), board_col=(0.07, 0.07, 0.08))
P = render("/tmp/a.txt", f"{V}/hero_front.png", -28, 16, 255, (0, 22, 12), W=2800, H=1900); shots["front"] = P
scene("/tmp/b.txt", ALL, ("switch_lever", "screws_cover"))
P = render("/tmp/b.txt", f"{V}/hero_back.png", 152, 20, 255, (0, 22, 12)); shots["back"] = P
# exploded
EX = {"chassis": (0, 0, 26), "cover": (0, 0, 56), "battery": (0, 0, 44), "screws_cover": (0, 0, 70),
      "rainbowA": (28, 0, 0), "rainbowB": (18, 0, 0), "tail": (18, 0, -22), "board": (0, 0, 13)}
scene("/tmp/e.txt", ALL, ("screen", "battery", "switch", "switch_lever", "screws_chassis", "dupont", "screws_cover"), off=EX, tint=True)
shots["exploded"] = render("/tmp/e.txt", f"{V}/exploded.png", -38, 22, 400, (-6, 22, 26))
# step 1: connectors on VBAT/GND (module from the back)
scene("/tmp/s1.txt", [], ("dupont",))
shots["s1"] = render("/tmp/s1.txt", f"{V}/step1.png", 120, 30, 150, (-6, 24, 15))
# step 2: chassis onto the standoffs
scene("/tmp/s2.txt", ["chassis"], ("dupont", "screws_chassis"), off={"chassis": (0, 0, 9)}, tint=True)
shots["s2"] = render("/tmp/s2.txt", f"{V}/step2.png", 150, 25, 170, (-6, 25, 16))
# step 3: module + chassis slide into the body from the back
scene("/tmp/s3.txt", ["body", "chassis"], ("dupont", "screws_chassis"), off={"chassis": (0, 0, 30), "board": (0, 0, 30)}, tint=True)
shots["s3"] = render("/tmp/s3.txt", f"{V}/step3.png", 145, 22, 270, (8, 22, 24))
# step 4: battery + switch on the cover (cover inside up)
scene("/tmp/s4.txt", ["cover"], ("battery", "switch", "switch_lever"), board=False, tint=True)
shots["s4"] = render("/tmp/s4.txt", f"{V}/step4.png", -10, 55, 150, (-6, 25, 26))
# step 5: cover + 3 screws
scene("/tmp/s5.txt", ["body", "cover"], ("switch_lever", "screws_cover"), off={"screws_cover": (0, 0, 10)}, tint=True)
shots["s5"] = render("/tmp/s5.txt", f"{V}/step5.png", 160, 22, 250, (4, 22, 20))
# step 6: rainbow + tail
scene("/tmp/s6.txt", ["body", "rainbowA", "rainbowB", "tail"], ("screen",), off={"rainbowA": (22, 0, 0), "rainbowB": (14, 0, 0), "tail": (14, 0, -20)})
shots["s6"] = render("/tmp/s6.txt", f"{V}/step6.png", -40, 15, 260, (-14, 22, 10))

# ---------------------------------------------------------------- sheet 1: sketch
W, H = 2400, 1500
im = Image.new("RGB", (W, H), (246, 245, 242)); d = ImageDraw.Draw(im)
d.text((40, 28), "Pixel Nyan v2 — скетч", font=f(64, True), fill=(30, 30, 34))
d.text((44, 108), "корпус + лицо + голова + лапки — одна деталь в 4 цвета; радуга вровень с корпусом", font=f(32), fill=(110, 108, 104))
a = Image.open(f"{V}/hero_front.png"); b = Image.open(f"{V}/hero_back.png")
im.paste(a, (20, 170)); im.paste(b.resize((640, 440)), (W - 680, 170))
P = shots["front"]
label(im, lambda p: (P(p)[0] + 20, P(p)[1] + 170), (2.35, 14.0, 0.0), "окно по центру экрана (исправлено)", (-60, 230))
label(im, lambda p: (P(p)[0] + 20, P(p)[1] + 170), (-40.0, 30.0, -3.0), "голова — часть корпуса", (-60, -110))
label(im, lambda p: (P(p)[0] + 20, P(p)[1] + 170), (46.0, 43.0, 6.0), "верх радуги = верх корпуса", (30, -60))
label(im, lambda p: (P(p)[0] + 20, P(p)[1] + 170), (51.6, 25.2, 9.7), "USB-C через радугу", (40, 70))
label(im, lambda p: (P(p)[0] + 20, P(p)[1] + 170), (18.1, 43.2, 10.6), "Ø1.5 под скрепку (RST/BOOT)", (-30, -95))
Pb = shots["back"]
sc_ = 640 / 800
pb = lambda p: (Pb(p)[0] * sc_ + W - 680, Pb(p)[1] * sc_ + 170)
label(im, pb, (-15.4, 25.2, 32.0), "тумблер SS12D00G5", (60, 80), 20)
label(im, pb, (-17.3, 39.9, 29.6), "3 × M2×8", (-40, -50), 20)
y = 650
d.text((W - 680, y), "Что изменилось", font=f(36, True), fill=(30, 30, 34)); y += 56
for t in ["1  Голова справа, USB слева (setRotation(3))",
          "2  Окно в шасси под разъёмы VBAT/GND",
          "3  Окно точно по экрану + карман-фаска",
          "4  Гнездо-защёлка под тумблер на крышке",
          "5  Лицо, корпус и голова — одна деталь, 4 цвета",
          "6  Без толкателей: отверстия под скрепку",
          "7  Радуга вровень с верхом, угол поп-тарта прямой",
          "8  Голова больше не отходит — она монолитна"]:
    d.text((W - 680, y), t, font=f(25), fill=(40, 40, 44)); y += 44
d.text((40, H - 50), "Габарит ≈ 103 × 45 × 34 мм  ·  все размеры сняты со STEP-модели Waveshare, пересечения проверены",
       font=f(24), fill=(140, 138, 134))
im.save(f"{V}/sketch_v2.png")

# ---------------------------------------------------------------- sheet 2: assembly infographic
W, H = 2400, 2050
im = Image.new("RGB", (W, H), (246, 245, 242)); d = ImageDraw.Draw(im)
d.text((40, 28), "Сборка Pixel Nyan v2", font=f(64, True), fill=(30, 30, 34))
d.text((44, 108), "6 шагов · клей нужен только для радуги и хвоста", font=f(32), fill=(110, 108, 104))
steps = [("s1", "Провода «мама-папа» на VBAT и GND", ["первые 2 штыря у USB-C (на фото — нижний ряд)",
                                         "конец «мамы» слегка отогнуть вверх, к батарее (~1.5 мм)", "«+» батареи → тумблер → VBAT, «−» → GND"]),
         ("s2", "Шасси на плату", ["4 × M2×10 в латунные стойки", "головки утапливаются в шасси", "провода — в окно шасси"]),
         ("s3", "Модуль в корпус сзади", ["экраном вперёд, USB-C влево", "сам встаёт в карман с фаской", "до упора стеклом в окно"]),
         ("s4", "Батарея и тумблер в крышку", ["602535 в карман вплотную к нижней стенке", "тумблер защёлкнуть рычажком наружу",
                                              "запаять: + → тумблер → провод VBAT"]),
         ("s5", "Закрыть крышку", ["3 × саморез M2×8 потай", "стойки крышки прижимают шасси", "проверить ход тумблера"]),
         ("s6", "Радуга и хвост", ["половинки A+B склеить по туннелю", "приклеить к левому боку корпуса", "хвост — на радугу спереди"])]
cw, chh = 760, 520
for i, (key, title, lines) in enumerate(steps):
    x = 40 + (i % 3) * (cw + 20); y0 = 180 + (i // 3) * (chh + 400)
    img = Image.open(f"{V}/step{i + 1}.png").resize((cw, chh))
    im.paste(img, (x, y0))
    d.ellipse([x + 14, y0 + 14, x + 84, y0 + 84], fill=ACC)
    d.text((x + 37, y0 + 22), str(i + 1), font=f(44, True), fill=(255, 255, 255))
    d.text((x, y0 + chh + 16), title, font=f(36, True), fill=(30, 30, 34))
    for j, t in enumerate(lines):
        d.text((x, y0 + chh + 70 + j * 42), "• " + t, font=f(27), fill=(50, 50, 54))
# arrows / callouts on some steps
def sp(i, P, p):
    x = 40 + (i % 3) * (cw + 20); y0 = 180 + (i // 3) * (chh + 400); s = cw / 800
    return (P(p)[0] * s + x, P(p)[1] * s + y0)
label(im, lambda p: sp(0, shots["s1"], p), (12.0, 17.6, 20.0), "VBAT / GND", (40, -40), 22)
arrow(im, sp(2, shots["s3"], (3.6, 25.2, 62.0)), sp(2, shots["s3"], (3.6, 25.2, 36.0)))
arrow(im, sp(4, shots["s5"], (0.0, 30.0, 48.0)), sp(4, shots["s5"], (0.0, 30.0, 33.0)))
d.rounded_rectangle([40, H - 190, W - 40, H - 40], 18, fill=(255, 255, 255))
d.text((70, H - 170), "Покупное:", font=f(30, True), fill=(30, 30, 34))
d.text((70, H - 122), "LiPo 602535 · тумблер SS12D00G5 · 2 провода Dupont «мама-папа» · 4 × M2×10 · 3 × саморез M2×8 потай · "
       "двусторонний скотч · клей", font=f(27), fill=(50, 50, 54))
d.text((70, H - 80), "Прошивка: gfx->setRotation(3) — USB-C слева, голова справа (тач подстраивается сам)",
       font=f(27), fill=(50, 50, 54))
im.save(f"{V}/assembly_v2.png")
print("ok")
