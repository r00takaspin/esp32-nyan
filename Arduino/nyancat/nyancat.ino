// DSH Agent Buddy for Waveshare ESP32-S3-Touch-LCD-1.47
// ------------------------------------------------------
// The device is a desk-side "buddy" for the DeepSeek Harness:
//   * shows what the agent is doing (idle / thinking / tool / error)
//   * shows a pending tool approval with touch YES / NO buttons
//   * speaks a tiny line protocol over USB CDC (115200):
//       Host -> Dev:  S|idle
//                     S|think
//                     S|tool|<text>
//                     S|approve|<id>|<text>
//                     S|error|<text>
//       Dev -> Host:  E|hello
//                     E|approve|<id>|<1|0>
//   * has a pluggable keyboard backend: raw key output goes through kbRaw(),
//     which is a no-op here and gets a BLE HID / USB HID implementation later.

#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include "esp_lcd_touch_axs5106l.h"
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
Arduino_Canvas *canvas = new Arduino_Canvas(FRAME_W, FRAME_H, gfx, 0, 0, 0);

#define C565(r,g,b) ((((r)&0xF8)<<8) | (((g)&0xFC)<<3) | ((b)>>3))
#define C_BLACK  C565(0, 0, 0)
#define C_WHITE  C565(255, 255, 255)
#define C_GREEN  C565(60, 220, 90)
#define C_RED    C565(240, 60, 60)
#define C_AMBER  C565(255, 190, 40)
#define C_CYAN   C565(80, 200, 255)
#define C_PANEL  C565(18, 20, 34)
#define C_DIM    C565(120, 140, 170)

enum St { ST_IDLE, ST_THINK, ST_TOOL, ST_APPROVE, ST_ERROR };
St st = ST_IDLE;
char stateText[180] = "";
char approveId[48] = "";

int frameIdx = 0;
uint32_t lastFrame = 0;
bool dirty = true;
uint32_t lastTouch = 0;

touch_data_t td;

// ---- keyboard backend: BLE HID by default, USB HID with -DUSB_HID_KB ----
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
void decodeFrame(int idx) {
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

void drawBadge(const char *label, uint16_t col) {
  canvas->fillRect(0, 0, 320, 18, C_BLACK);
  canvas->fillRect(4, 5, 8, 8, col);
  canvas->setTextColor(col);
  canvas->setTextSize(1);
  canvas->setCursor(18, 5);
  canvas->print(label);
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
  canvas->setTextColor(C_AMBER);
  canvas->setTextSize(1);
  canvas->setCursor(6, 4);
  canvas->print("APPROVE TOOL CALL?");
  drawWrapped(6, 16, 308, stateText, C_WHITE, 1, 10);

  // Big thumb-friendly buttons.
  canvas->fillRoundRect(6, 96, 152, 70, 12, C_GREEN);
  canvas->fillRoundRect(162, 96, 152, 70, 12, C_RED);
  canvas->setTextColor(C_BLACK);
  canvas->setTextSize(3);
  canvas->setCursor(55, 119);
  canvas->print("YES");
  canvas->setCursor(220, 119);
  canvas->print("NO");
}

void drawError() {
  canvas->fillScreen(C565(60, 0, 0));
  canvas->setTextColor(C_WHITE);
  canvas->setTextSize(2);
  canvas->setCursor(10, 10);
  canvas->print("ERROR");
  drawWrapped(10, 44, 300, stateText, C_WHITE, 1, 12);
}

void drawPet() {
  decodeFrame(frameIdx);
  frameIdx = (frameIdx + 1) % NYAN_FRAMES;
  if (st == ST_IDLE) {
    drawBadge("IDLE", C_GREEN);
  } else if (st == ST_THINK) {
    drawBadge("THINKING", C_CYAN);
  } else if (st == ST_TOOL) {
    drawBadge("TOOL", C_AMBER);
    canvas->fillRect(0, 150, 320, 22, C_BLACK);
    drawWrapped(6, 155, 308, stateText, C_AMBER, 1, 9);
  }
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
  st = s;
  strncpy(stateText, text ? text : "", sizeof(stateText) - 1);
  stateText[sizeof(stateText) - 1] = 0;
  dirty = true;
  lastFrame = 0;
  const char *name = (s == ST_IDLE) ? "idle" : (s == ST_THINK) ? "think"
                   : (s == ST_TOOL) ? "tool" : (s == ST_APPROVE) ? "approve" : "error";
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
  if (cmd == "idle")       applyState(ST_IDLE, "");
  else if (cmd == "think") applyState(ST_THINK, "");
  else if (cmd == "tool")  applyState(ST_TOOL, tok[2].c_str());
  else if (cmd == "error") applyState(ST_ERROR, tok[2].c_str());
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
  if (y < 96 || y > 166) return;
  if (x < 160) {
    kbRaw('y');
    sendApprove(approveId, 1);
  } else {
    kbRaw('n');
    sendApprove(approveId, 0);
  }
  applyState(ST_THINK, "");
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

  pinMode(GFX_BL, OUTPUT);
  digitalWrite(GFX_BL, HIGH);

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
  // serial input
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      parseLine(rxLine);
      rxLine = "";
    } else if (c != '\r') {
      if (rxLine.length() < 220) rxLine += c;
    }
  }

  // touch
  bsp_touch_read();
  if (bsp_touch_get_coordinates(&td)) handleTouch();

  // render
  if (st == ST_APPROVE || st == ST_ERROR) {
    if (dirty) {
      if (st == ST_APPROVE) drawApprove(); else drawError();
      canvas->flush();
      dirty = false;
    }
  } else {
    uint32_t period = (st == ST_IDLE) ? 140 : (st == ST_THINK ? 60 : 90);
    if (millis() - lastFrame >= period) {
      lastFrame = millis();
      drawPet();
      canvas->flush();
    }
  }
  delay(2);
}
