// DSH Agent Buddy for Waveshare ESP32-S3-Touch-LCD-1.47
// ------------------------------------------------------
// The device is a desk-side "buddy" for the DeepSeek Harness:
//   * per-state cat animations (idle / thinking / approved / rejected)
//   * a pending tool approval with big touch YES / NO buttons
//   * a tiny line protocol over USB CDC (115200):
//       Host -> Dev:  S|idle  S|think  S|tool|<text>  S|approve|<id>|<text>  S|error|<text>
//       Dev -> Host:  E|hello  E|approve|<id>|<1|0>  A|<state>
//   * a pluggable keyboard backend (BLE HID by default, USB HID with -DUSB_HID_KB).

#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include "esp_lcd_touch_axs5106l.h"
#include "nyan_anims.h"
#include "nyancat_frames.h"

#if defined(USB_HID_KB)
#include <USB.h>
#include <USBHIDKeyboard.h>
USBHIDKeyboard usbKeyboard;
#else
#include "ble_kb.h"
#endif

// ---- pins ----
#define GFX_BL    46
#define LCD_RST   40
#define TOUCH_SDA 42
#define TOUCH_SCL 41
#define TOUCH_RST 47
#define TOUCH_INT 48

// ---- display ----
Arduino_DataBus *bus = new Arduino_ESP32SPI(45 /* DC */, 21 /* CS */, 38 /* SCK */, 39 /* MOSI */);
Arduino_GFX *gfx = new Arduino_ST7789(
    bus, LCD_RST, 0 /* rotation */, false /* IPS */,
    172 /* width */, 320 /* height */,
    34 /* col_offset1 */, 0 /* row_offset1 */,
    34 /* col_offset2 */, 0 /* row_offset2 */);
Arduino_Canvas *canvas = new Arduino_Canvas(ANIM_W, ANIM_H, gfx, 0, 0, 0);

#define C565(r,g,b) ((((r)&0xF8)<<8) | (((g)&0xFC)<<3) | ((b)>>3))
#define C_BLACK  C565(0, 0, 0)
#define C_WHITE  C565(255, 255, 255)
#define C_GREEN  C565(60, 220, 90)
#define C_RED    C565(240, 60, 60)
#define C_AMBER  C565(255, 190, 40)
#define C_CYAN   C565(80, 200, 255)
#define C_PANEL  C565(18, 20, 34)

enum St { ST_IDLE, ST_THINK, ST_TOOL, ST_APPROVE, ST_ERROR, ST_APPROVED, ST_REJECTED };
St st = ST_IDLE;
char stateText[180] = "";
char approveId[48] = "";

#define BL_LEVEL 460            // ~45% backlight duty (0..1023)
#define IDLE_SLEEP_MS 10000UL   // screen off after 10 s in idle

unsigned long lastActivity = 0;
bool screenOn = true;
bool blinkOn = false;
unsigned long lastBlink = 0;

void wakeScreen() {
  if (!screenOn) {
    ledcWrite(GFX_BL, BL_LEVEL);
    screenOn = true;
  }
  lastActivity = millis();
}

void sleepScreen() {
  if (screenOn) {
    ledcWrite(GFX_BL, 0);
    screenOn = false;
  }
}

int animIdx = 0;
uint32_t lastFrame = 0;
uint32_t reactionUntil = 0;
bool dirty = true;
uint32_t lastTouch = 0;

touch_data_t td;

// ---- keyboard backend ----
void kbRaw(uint8_t key) {
#if defined(USB_HID_KB)
  usbKeyboard.write(key);
#else
  bleKbKey(key);
#endif
}

// ---- exact vendor LCD init ----
void lcd_reg_init(void) {
  static const uint8_t init_operations[] = {
    BEGIN_WRITE,
    WRITE_COMMAND_8, 0x11, END_WRITE, DELAY, 120,
    BEGIN_WRITE,
    WRITE_C8_D16, 0xDF, 0x98, 0x53,
    WRITE_C8_D8, 0xB2, 0x23,
    WRITE_COMMAND_8, 0xB7, WRITE_BYTES, 4, 0x00, 0x47, 0x00, 0x6F,
    WRITE_COMMAND_8, 0xBB, WRITE_BYTES, 6, 0x1C, 0x1A, 0x55, 0x73, 0x63, 0xF0,
    WRITE_C8_D16, 0xC0, 0x44, 0xA4,
    WRITE_C8_D8, 0xC1, 0x16,
    WRITE_COMMAND_8, 0xC3, WRITE_BYTES, 8, 0x7D, 0x07, 0x14, 0x06, 0xCF, 0x71, 0x72, 0x77,
    WRITE_COMMAND_8, 0xC4, WRITE_BYTES, 12, 0x00, 0x00, 0xA0, 0x79, 0x0B, 0x0A, 0x16, 0x79, 0x0B, 0x0A, 0x16, 0x82,
    WRITE_COMMAND_8, 0xC8, WRITE_BYTES, 32,
      0x3F, 0x32, 0x29, 0x29, 0x27, 0x2B, 0x27, 0x28, 0x28, 0x26, 0x25, 0x17, 0x12, 0x0D, 0x04, 0x00,
      0x3F, 0x32, 0x29, 0x29, 0x27, 0x2B, 0x27, 0x28, 0x28, 0x26, 0x25, 0x17, 0x12, 0x0D, 0x04, 0x00,
    WRITE_COMMAND_8, 0xD0, WRITE_BYTES, 5, 0x04, 0x06, 0x6B, 0x0F, 0x00,
    WRITE_C8_D16, 0xD7, 0x00, 0x30,
    WRITE_C8_D8, 0xE6, 0x14,
    WRITE_C8_D8, 0xDE, 0x01,
    WRITE_COMMAND_8, 0xB7, WRITE_BYTES, 5, 0x03, 0x13, 0xEF, 0x35, 0x35,
    WRITE_COMMAND_8, 0xC1, WRITE_BYTES, 3, 0x14, 0x15, 0xC0,
    WRITE_C8_D16, 0xC2, 0x06, 0x3A,
    WRITE_C8_D16, 0xC4, 0x72, 0x12,
    WRITE_C8_D8, 0xBE, 0x00,
    WRITE_C8_D8, 0xDE, 0x02,
    WRITE_COMMAND_8, 0xE5, WRITE_BYTES, 3, 0x00, 0x02, 0x00,
    WRITE_COMMAND_8, 0xE5, WRITE_BYTES, 3, 0x01, 0x02, 0x00,
    WRITE_C8_D8, 0xDE, 0x00,
    WRITE_C8_D8, 0x35, 0x00,
    WRITE_C8_D8, 0x3A, 0x05,
    WRITE_COMMAND_8, 0x2A, WRITE_BYTES, 4, 0x00, 0x22, 0x00, 0xCD,
    WRITE_COMMAND_8, 0x2B, WRITE_BYTES, 4, 0x00, 0x00, 0x01, 0x3F,
    WRITE_C8_D8, 0xDE, 0x02,
    WRITE_COMMAND_8, 0xE5, WRITE_BYTES, 3, 0x00, 0x02, 0x00,
    WRITE_C8_D8, 0xDE, 0x00,
    WRITE_C8_D8, 0x36, 0x00,
    WRITE_COMMAND_8, 0x21,
    END_WRITE, DELAY, 10,
    BEGIN_WRITE,
    WRITE_COMMAND_8, 0x29,
    END_WRITE
  };
  bus->batchOperation(init_operations, sizeof(init_operations));
}

// ------------------------- rendering -------------------------
void drawWrapped(int x, int y, int maxW, const char *s, uint16_t col, int sz, int lh);

void drawAnimFrame(int anim, int idx) {
  uint16_t *fb = canvas->getFramebuffer();
  if (!fb) return;
  uint32_t p = ANIM_OFF[anim][idx];
  uint32_t end = ANIM_OFF[anim][idx + 1];
  int pos = 0;
  while (p < end) {
    uint8_t cnt = ANIM_RLE[p++];
    uint8_t ix = ANIM_RLE[p++];
    uint16_t col = ANIM_PAL[ix];
    for (uint8_t k = 0; k < cnt; k++) fb[pos++] = col;
  }
}

// Original 12-frame Nyan Cat, kept for the THINK / TOOL state.
void drawNyanFrame(int idx) {
  uint16_t *fb = canvas->getFramebuffer();
  if (!fb) return;
  uint32_t p = NYAN_OFF[idx];
  uint32_t end = NYAN_OFF[idx + 1];
  int pos = 0;
  while (p < end) {
    uint8_t cnt = NYAN_RLE[p++];
    uint8_t ix = NYAN_RLE[p++];
    uint16_t col = NYAN_PAL565[ix];
    for (uint8_t k = 0; k < cnt; k++) fb[pos++] = col;
  }
}

int animForState(St s) {
  switch (s) {
    case ST_IDLE:     return ANIM_IDLE;
    case ST_THINK:
    case ST_TOOL:     return ANIM_THINK;
    case ST_APPROVED: return ANIM_APPROVE;
    case ST_REJECTED:
    case ST_ERROR:    return ANIM_REJECT;
    default:          return ANIM_IDLE;
  }
}

uint32_t periodForState(St s) {
  switch (s) {
    case ST_IDLE:     return 220;
    case ST_THINK:    return 110;
    case ST_APPROVED: return 90;    // fast cool-cat loop
    default:          return 140;
  }
}

// ---- sparse starfield drawn over the flat background of the new animations ----
struct Star { int16_t x, y; uint8_t phase, type; };
static const Star STARS[28] = {
  {237,149,7,1},{266,156,3,0},{268,127,2,0},{234,83,2,0},{281,16,6,1},{86,165,0,2},
  {38,21,0,0},{129,159,0,1},{173,118,3,2},{125,81,7,0},{49,123,4,1},{288,27,4,1},
  {123,137,4,0},{41,150,1,1},{61,80,6,0},{14,6,3,0},{32,126,6,2},{209,113,1,2},
  {107,75,5,0},{165,91,0,1},{66,40,3,2},{57,8,0,1},{255,51,3,1},{266,54,2,1},
  {202,35,6,1},{114,6,4,2},{161,11,3,0},{207,160,1,0},
};

void drawStars(uint32_t f) {
  uint16_t *fb = canvas->getFramebuffer();
  if (!fb) return;
  const uint16_t bg = ANIM_PAL[0];
  // Only one star sparkles at a time, and it changes slowly: a rare twinkle.
  int hot = (f / 12) % 28;
  for (unsigned i = 0; i < sizeof(STARS) / sizeof(STARS[0]); i++) {
    int x = STARS[i].x, y = STARS[i].y;
    int idx = y * ANIM_W + x;
    if (fb[idx] != bg) continue;               // never paint over the cat
    uint16_t col = ((int)i == hot) ? C565(255, 255, 255) : C565(175, 200, 240);
    fb[idx] = col;
    if (STARS[i].type >= 1) {
      if (x > 0 && fb[idx - 1] == bg) fb[idx - 1] = col;
      if (x < ANIM_W - 1 && fb[idx + 1] == bg) fb[idx + 1] = col;
      if (y > 0 && fb[idx - ANIM_W] == bg) fb[idx - ANIM_W] = col;
      if (y < ANIM_H - 1 && fb[idx + ANIM_W] == bg) fb[idx + ANIM_W] = col;
    }
    if (STARS[i].type == 2) {
      if (x < ANIM_W - 1 && fb[idx + 1] == bg) fb[idx + 1] = col;
      if (y < ANIM_H - 1 && fb[idx + ANIM_W] == bg) fb[idx + ANIM_W] = col;
    }
  }
}

void drawScene() {
  if (st == ST_THINK || st == ST_TOOL) {
    drawNyanFrame(animIdx % NYAN_FRAMES);
    animIdx = (animIdx + 1) % NYAN_FRAMES;
  } else {
    int anim = animForState(st);
    int n = ANIM_COUNT[anim];
    if (animIdx >= n) animIdx = 0;
    drawAnimFrame(anim, animIdx);
    drawStars(animIdx);
    animIdx = (animIdx + 1) % n;
  }

  if (st == ST_TOOL) {
    canvas->fillRect(0, 150, ANIM_W, 22, C_BLACK);
    drawWrapped(6, 155, 308, stateText, C_AMBER, 1, 9);
  } else if (st == ST_ERROR) {
    canvas->fillRect(0, 150, ANIM_W, 22, C_BLACK);
    drawWrapped(6, 155, 308, stateText, C_RED, 1, 9);
  }
}

// word-wrapped text using the built-in 5x7 font
void drawWrapped(int x, int y, int maxW, const char *s, uint16_t col, int sz, int lh) {
  canvas->setTextColor(col);
  canvas->setTextSize(sz);
  int cw = 6 * sz;
  int cx = x, cy = y;
  const char *p = s;
  while (*p) {
    while (*p == ' ') p++;
    if (!*p) break;
    const char *q = p;
    while (*q && *q != ' ') q++;
    int len = q - p;
    if (cx > x && cx + len * cw > x + maxW) { cx = x; cy += lh; }
    canvas->setCursor(cx, cy);
    for (int i = 0; i < len; i++) canvas->write((uint8_t)p[i]);
    cx += (len + 1) * cw;
    p = q;
  }
}

void drawApprove() {
  canvas->fillScreen(C_PANEL);
  // Alternating red/green frame: the visual "LED" alert for a pending approval.
  uint16_t edge = blinkOn ? C_GREEN : C_RED;
  // Inset and thick so the panel's clipping does not eat the frame.
  canvas->fillRect(2, 2, 316, 6, edge);
  canvas->fillRect(2, 164, 316, 6, edge);
  canvas->fillRect(2, 2, 6, 168, edge);
  canvas->fillRect(312, 2, 6, 168, edge);
  canvas->setTextColor(C_AMBER);
  canvas->setTextSize(1);
  canvas->setCursor(12, 11);
  canvas->print("APPROVE TOOL CALL?");
  drawWrapped(12, 23, 296, stateText, C_WHITE, 1, 10);
  canvas->fillRoundRect(14, 100, 140, 62, 12, C_GREEN);
  canvas->fillRoundRect(166, 100, 140, 62, 12, C_RED);
  canvas->setTextColor(C_BLACK);
  canvas->setTextSize(3);
  canvas->setCursor(57, 121);
  canvas->print("YES");
  canvas->setCursor(218, 121);
  canvas->print("NO");
}

// ------------------------- protocol -------------------------
void sendLine(const char *s) {
  if (Serial.availableForWrite() < 64) return;   // never block
  Serial.println(s);
}

void sendApprove(const char *id, int ok) {
  char buf[96];
  snprintf(buf, sizeof(buf), "E|approve|%s|%d", id, ok);
  sendLine(buf);
}

void applyState(St s, const char *text) {
  St prev = st;
  st = s;
  strncpy(stateText, text ? text : "", sizeof(stateText) - 1);
  stateText[sizeof(stateText) - 1] = 0;
  animIdx = 0;
  dirty = true;
  lastFrame = 0;
  if (s == ST_APPROVED) reactionUntil = millis() + 3600;
  else if (s == ST_REJECTED) reactionUntil = millis() + 1600;
  if (s != ST_IDLE) wakeScreen();
  else if (prev != ST_IDLE) lastActivity = millis();

  const char *name = (s == ST_IDLE) ? "idle" : (s == ST_THINK) ? "think"
                   : (s == ST_TOOL) ? "tool" : (s == ST_APPROVE) ? "approve"
                   : (s == ST_APPROVED) ? "approved" : (s == ST_REJECTED) ? "rejected" : "error";
  char a[28];
  snprintf(a, sizeof(a), "A|%s", name);
  sendLine(a);
}

void parseLine(const String &line) {
  String tok[5];
  int n = 0, start = 0;
  for (int i = 0; i <= (int)line.length() && n < 5; i++) {
    if (i == (int)line.length() || line[i] == '|') {
      tok[n++] = line.substring(start, i);
      start = i + 1;
    }
  }
  if (n < 2 || tok[0] != "S") return;
  String cmd = tok[1];
  // Protect the approved/rejected reaction window from late status updates.
  if ((st == ST_APPROVED || st == ST_REJECTED) && millis() < reactionUntil
      && (cmd == "think" || cmd == "idle" || cmd == "tool")) return;
  if (cmd == "idle")       applyState(ST_IDLE, "");
  else if (cmd == "think") applyState(ST_THINK, "");
  else if (cmd == "tool")  applyState(ST_TOOL, tok[2].c_str());
  else if (cmd == "error") applyState(ST_ERROR, tok[2].c_str());
  else if (cmd == "approved") applyState(ST_APPROVED, "");   // debug: play approve reaction
  else if (cmd == "rejected") applyState(ST_REJECTED, "");   // debug: play reject reaction
  else if (cmd == "kb") kbRaw((uint8_t)tok[2][0]);           // debug: exercise BLE HID path
  else if (cmd == "approve") {
    strncpy(approveId, tok[2].c_str(), sizeof(approveId) - 1);
    approveId[sizeof(approveId) - 1] = 0;
    applyState(ST_APPROVE, tok[3].c_str());
  }
}

// ------------------------- touch -------------------------
void handleTouch() {
  if (st != ST_APPROVE) return;
  if (millis() - lastTouch < 220) return;   // debounce
  lastTouch = millis();
  int x = td.coords[0].x;
  int y = td.coords[0].y;
  {
    char dbg[40];
    snprintf(dbg, sizeof(dbg), "T|%d|%d", x, y);
    sendLine(dbg);
  }
  if (y < 100 || y > 162) return;
  int ok = (x < 160) ? 1 : 0;
  kbRaw(ok ? 'y' : 'n');
  sendApprove(approveId, ok);
  applyState(ok ? ST_APPROVED : ST_REJECTED, "");
}

// ------------------------- setup / loop -------------------------
void setup() {
  pinMode(LCD_RST, OUTPUT);
  digitalWrite(LCD_RST, 0);
  delay(10);
  digitalWrite(LCD_RST, 1);

  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);
  delay(200);
  Serial.println("AgentBuddy boot");

  if (!gfx->begin()) Serial.println("gfx FAILED");
  lcd_reg_init();
  gfx->setRotation(1);        // landscape 320x172

  ledcAttach(GFX_BL, 5000, 10);
  ledcWrite(GFX_BL, BL_LEVEL);

  if (!canvas->begin(GFX_SKIP_OUTPUT_BEGIN)) Serial.println("canvas FAILED");

  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  bsp_touch_init(&Wire, TOUCH_RST, TOUCH_INT, gfx->getRotation(), gfx->width(), gfx->height());

#if defined(USB_HID_KB)
  USB.begin();
  usbKeyboard.begin();
  Serial.println("USB HID keyboard ready");
#else
  bleKbBegin("AgentBuddy");
  Serial.println("BLE keyboard ready");
#endif

  sendLine("E|hello");
  Serial.println("AgentBuddy ready");
  applyState(ST_IDLE, "");
}

String rxLine;

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      parseLine(rxLine);
      rxLine = "";
    } else if (c != '\r') {
      if (rxLine.length() < 220) rxLine += c;
    }
  }

  bsp_touch_read();
  if (bsp_touch_get_coordinates(&td)) {
    wakeScreen();
    handleTouch();
  }

  if (st == ST_IDLE && screenOn && millis() - lastActivity >= IDLE_SLEEP_MS) {
    sleepScreen();
  }

  if (st == ST_APPROVE) {
    if (millis() - lastBlink >= 400) {
      lastBlink = millis();
      blinkOn = !blinkOn;
      dirty = true;
    }
    if (dirty) {
      drawApprove();
      canvas->flush();
      dirty = false;
    }
  } else {
    if ((st == ST_APPROVED || st == ST_REJECTED) && millis() >= reactionUntil) {
      applyState(ST_THINK, "");
    }
    if (millis() - lastFrame >= periodForState(st)) {
      lastFrame = millis();
      drawScene();
      canvas->flush();
    }
  }
  delay(2);
}
