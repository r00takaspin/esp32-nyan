from PIL import Image, ImageDraw, ImageFont

FONT = "/System/Library/Fonts/Supplemental/Arial.ttf"
FONTB = "/System/Library/Fonts/Supplemental/Arial Bold.ttf"
f = lambda s, b=False: ImageFont.truetype(FONTB if b else FONT, s)

C = {
    "A": ("A · «Pixel Nyan»", "воксельный корпус по пиксель-арту",
          ["Габарит ≈ 103 × 43 × 18 мм (+ откидная ножка)",
           "Пиксель 2.4 мм, рельеф 0.6–4 мм по цветам",
           "Печать лицом вверх, без поддержек, слой 0.2",
           "Мультиколор AMS/MMU: 12 цветов",
           "  (или цветные вставки на клей)",
           "Экран в «поп-тарте», окно 40 × 21.5 + фаска 45°",
           "USB-C влево → setRotation(3) или угловой кабель",
           "Задняя крышка на 4 × M2 (шаг 39 × 17 как у платы)"]),
    "B": ("B · «CatMac»", "ретро-терминал с ушками",
          ["Габарит ≈ 64 × 77 × 54 мм",
           "Печать стоя, PLA ivory, слой 0.2",
           "Рамка экрана — отдельная деталь (тон темнее)",
           "Наклон лица 6°, щель «дисковода», бейдж-радуга",
           "Лапки и уши — отдельно на шипах Ø3",
           "Хвост-кабель — декоративный тубус под USB",
           "USB-C в правом боку, вентпазы сзади"]),
    "C": ("C · «Mochi Cat»", "пухлый кот, экран — лицо",
          ["Габарит ≈ 74 × 78 × 54 мм (+ хвост)",
           "Печать стоя; на макушке видны террасы слоёв",
           "→ слой 0.12 или fuzzy skin для «плюша»",
           "Чёрный ободок экрана — отдельная деталь",
           "Щёки, полоски, уши — MMU или покраска",
           "Лапки/живот — белый PLA, вклеиваются",
           "USB-C сзади внизу, хвост — отдельно на магнитах"]),
    "D": ("D · «Star Dock»", "клин-подставка galaxy",
          ["Габарит ≈ 70 × 51 × 44 мм",
           "Экран наклонён ~30° — удобно жать YES/NO",
           "Galaxy-black PLA с блёстками",
           "Звёзды и радуга — инлеи (мультиколор)",
           "Самый простой в печати: 1 деталь + крышка",
           "Поддержки не нужны, ушки — свободный выступ",
           "USB-C сзади, резиновые ножки снизу"]),
}


def sheet(k):
    W, H = 2400, 1350
    im = Image.new("RGB", (W, H), (246, 245, 242))
    d = ImageDraw.Draw(im)
    hero = Image.open(f"sketches/{k}_hero.png").resize((1650, 1100), Image.LANCZOS)
    back = Image.open(f"sketches/{k}_back.png").resize((690, 460), Image.LANCZOS)
    im.paste(hero, (30, 210))
    im.paste(back, (1690, 860))
    t, sub, notes = C[k]
    d.text((40, 40), t, font=f(78, True), fill=(30, 30, 34))
    d.text((44, 135), sub, font=f(40), fill=(110, 108, 104))
    y = 230
    for n in notes:
        d.text((1700, y), "•", font=f(30, True), fill=(225, 60, 120))
        d.text((1728, y), n, font=f(27), fill=(40, 40, 44))
        y += 52
    d.text((1700, 820), "вид сзади", font=f(26), fill=(120, 118, 114))
    d.text((40, H - 40), "Waveshare ESP32-S3-Touch-LCD-1.47 · модуль 44.5 × 24.55 × 10.6 мм · "
           "сетка слоёв 0.2 мм · экран — реальные кадры прошивки",
           font=f(22), fill=(140, 138, 134))
    im.save(f"sketches/sheet_{k}.png")


def overview():
    W, H = 2400, 1700
    im = Image.new("RGB", (W, H), (246, 245, 242))
    d = ImageDraw.Draw(im)
    d.text((40, 30), "Agent Buddy — концепты корпуса для 3D-печати", font=f(64, True), fill=(30, 30, 34))
    for i, k in enumerate("ABCD"):
        x, y = 40 + (i % 2) * 1180, 140 + (i // 2) * 780
        img = Image.open(f"sketches/{k}_hero.png").resize((1140, 760), Image.LANCZOS)
        im.paste(img, (x, y))
        d.rounded_rectangle([x + 16, y + 16, x + 560, y + 76], 14, fill=(255, 255, 255))
        d.text((x + 32, y + 24), C[k][0], font=f(38, True), fill=(30, 30, 34))
    im.save("sketches/overview.png")


for k in "ABCD":
    sheet(k)
overview()
print("ok")
