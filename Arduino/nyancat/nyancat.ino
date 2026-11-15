// DSH / Claude Agent Buddy for Waveshare ESP32-S3-Touch-LCD-1.47
// ---------------------------------------------------------------
// The device is a desk-side "buddy" for an AI agent (DeepSeek Harness or
// Claude Code):
//   * per-state cat animations (idle / thinking / approved / rejected)
//   * a pending tool approval with big touch YES / NO buttons
//   * first-run pairing wizard, status screen and settings (tap the cat)
//   * a tiny line protocol over USB CDC (115200):
//       Host -> Dev:  S|idle  S|think  S|tool|<text>  S|approve|<id>|<text>  S|error|<text>
//                     S|ping  S|info  S|host|<name>  S|cfg|<key>|<value>
//                     S|unpair  S|wizard  S|cancel|<id>  S|reboot
//       Dev -> Host:  E|hello  E|approve|<id>|<1|0>  A|<state>  T|<x>|<y>
//                     I|fw=..|ble=..|bond=..|bat=..|pct=..|pwr=..|...   (reply to ping/info/cfg)
//   * a pluggable keyboard backend (BLE HID by default, USB HID with -DUSB_HID_KB).

#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <Preferences.h>
#include "esp_lcd_touch_axs5106l.h"
#include "nyan_anims.h"
#include "nyancat_frames.h"
#include "battery.h"
#include "buddy_ui.h"

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

// LEDC backlight: core 3.x uses pin-based ledcAttach/ledcWrite, core 2.x uses
// channel-based ledcSetup/ledcAttachPin/ledcWrite. This shim keeps both working.
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
#define BL_CH       GFX_BL
#define BL_BEGIN()  do { ledcAttach(GFX_BL, 5000, 10); } while (0)
#else
#define BL_CH       0
#define BL_BEGIN()  do { ledcSetup(0, 5000, 10); ledcAttachPin(GFX_BL, 0); } while (0)
#endif

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
St afterReaction = ST_THINK;    // where APPROVED / REJECTED go when the reaction ends
char stateText[180] = "";
char approveId[48] = "";

// ---- persistent settings (NVS "buddy") ----
static const uint16_t BRI_LEVELS[BRI_COUNT] = {60, 150, 280, 450, 650};  // 280 = old BL_LEVEL
Preferences prefs;
uint8_t cfgBri = 2;           // index into BRI_LEVELS
uint8_t cfgSleep = 1;         // index into SLEEP_OPTIONS (10 s)
uint8_t cfgKb = KB_AUTO;      // y/n keystrokes on approve taps
bool cfgSetupDone = false;

void loadSettings() {
  prefs.begin("buddy", true);
  cfgBri = prefs.getUChar("bri", 2);
  cfgSleep = prefs.getUChar("sleep", 1);
  cfgKb = prefs.getUChar("kb", KB_AUTO);
  cfgSetupDone = prefs.getBool("setup", false);
  prefs.end();
  if (cfgBri >= BRI_COUNT) cfgBri = 2;
  if (cfgSleep >= SLEEP_COUNT) cfgSleep = 1;
  if (cfgKb > KB_ALWAYS) cfgKb = KB_AUTO;
}

void saveSettings() {
  prefs.begin("buddy", false);
  prefs.putUChar("bri", cfgBri);
  prefs.putUChar("sleep", cfgSleep);
  prefs.putUChar("kb", cfgKb);
  prefs.putBool("setup", cfgSetupDone);
  prefs.end();
}

inline uint16_t blLevel() { return BRI_LEVELS[cfgBri]; }

// ---- screen power ----
uint32_t lastFrame = 0;
unsigned long lastActivity = 0;
bool screenOn = true;
bool blinkOn = false;
unsigned long lastBlink = 0;

void wakeScreen() {
  if (!screenOn) {
    gfx->displayOn();          // SLPOUT; GRAM survives sleep, so the last frame is back at once
    ledcWrite(BL_CH, blLevel());
    screenOn = true;
    lastFrame = 0;             // scene rendering was paused while dark
  }
  lastActivity = millis();
}

void sleepScreen() {
  if (screenOn) {
    ledcWrite(BL_CH, 0);
    gfx->displayOff();         // SLPIN: panel driver and its oscillator stop too
    screenOn = false;
  }
}

// ---- UI overlay state ----
UiScreen ui = UI_NONE;
UiScreen uiResume = UI_NONE;     // wizard paused by an approval
uint8_t wizPage = 0;
uint8_t kbTest = 0;
uint32_t uiLastTouch = 0;
uint32_t uiLastDraw = 0;
bool uiDirty = true;
#define UI_AUTOCLOSE_MS 30000UL  // status / settings close themselves

// ---- agent link (USB host) ----
uint32_t lastHostRx = 0;
char hostName[16] = "agent";
#define HOST_LINK_MS 15000UL
inline bool hostLinked() { return lastHostRx && millis() - lastHostRx < HOST_LINK_MS; }

char bleName[24] = "AgentBuddy";

int animIdx = 0;
uint32_t frameMs = 0;          // last scene draw + flush time, reported as fms=
uint32_t reactionUntil = 0;
bool dirty = true;
uint32_t lastTouchSeen = 0;

touch_data_t td;

// ---- keyboard backend ----
void kbRaw(uint8_t key) {
#if defined(USB_HID_KB)
  usbKeyboard.write(key);
#else
  bleKbKey(key);
#endif
}

bool kbLinked() {
#if defined(USB_HID_KB)
  return true;
#else
  return bleKbConnected();
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
void drawAnimFrame(int anim, int idx) {
  uint16_t *fb = canvas->getFramebuffer();
  if (!fb) return;
  uint32_t p = ANIM_OFF[anim][idx];
  uint32_t end = ANIM_OFF[anim][idx + 1];
  int pos = 0;
  const int limit = ANIM_W * ANIM_H;
  while (p < end && pos < limit) {
    uint8_t cnt = ANIM_RLE[p++];
    uint8_t ix = ANIM_RLE[p++];
    uint16_t col = ANIM_PAL[ix];
    for (uint8_t k = 0; k < cnt && pos < limit; k++) fb[pos++] = col;
  }
}

// Original 12-frame Nyan Cat, kept for the THINK / TOOL state.
void drawNyanFrame(int idx) {
  uint16_t *fb = canvas->getFramebuffer();
  if (!fb) return;
  uint32_t p = NYAN_OFF[idx];
  uint32_t end = NYAN_OFF[idx + 1];
  int pos = 0;
  const int limit = FRAME_W * FRAME_H;
  while (p < end && pos < limit) {
    uint8_t cnt = NYAN_RLE[p++];
    uint8_t ix = NYAN_RLE[p++];
    uint16_t col = NYAN_PAL565[ix];
    for (uint8_t k = 0; k < cnt && pos < limit; k++) fb[pos++] = col;
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
    case ST_REJECTED: return 70;    // fast nope-cat loop
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

  // Battery hint over the cat: only when it matters (charging or low).
  PowerState pw = batPower();
  int pct = batPercent();
  bool low = pw == PWR_BATTERY && pct >= 0 && pct <= 15;
  if (pw == PWR_CHARGING || low) {
    canvas->fillRect(284, 3, 32, 15, C_BLACK);
    uiBatteryIcon(canvas, 287, 5, pct, pw, (millis() / 600) & 1);
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
  if (hostName[0] && hostLinked()) {
    canvas->setTextColor(C565(130, 138, 160));
    canvas->setCursor(308 - 6 * (int)strlen(hostName), 11);
    canvas->print(hostName);
  }
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

// ------------------------- UI model -------------------------
const char *stateName(St s) {
  return (s == ST_IDLE) ? "idle" : (s == ST_THINK) ? "think"
       : (s == ST_TOOL) ? "tool" : (s == ST_APPROVE) ? "approve"
       : (s == ST_APPROVED) ? "approved" : (s == ST_REJECTED) ? "rejected" : "error";
}

void buildModel(UiModel &m) {
  memset(&m, 0, sizeof(m));
  m.batMv = batMillivolts();
  m.batPct = batPercent();
  m.power = batPower();
#if !defined(USB_HID_KB)
  m.bleConnected = bleKbConnected();
  m.bondCount = bleKbBondCount();
  bleKbBondAddr(m.bondAddr, sizeof(m.bondAddr));
  if (!m.bondAddr[0]) bleKbPeerAddr(m.bondAddr, sizeof(m.bondAddr));
  m.passkey = bleKbPasskey();
#else
  m.bleConnected = true;
  m.bondCount = 1;
#endif
  strncpy(m.bleName, bleName, sizeof(m.bleName) - 1);
  m.hostLinked = hostLinked();
  strncpy(m.hostName, hostName, sizeof(m.hostName) - 1);
  m.agentState = stateName(st);
  m.brightness = cfgBri;
  m.sleepIdx = cfgSleep;
  m.kbMode = cfgKb;
  m.wizPage = wizPage;
  m.kbTest = kbTest;
  m.blink = (millis() / 500) & 1;
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

void sendInfo() {
  char bond[20] = "";
  int bonds = 0, ble = 0;
#if !defined(USB_HID_KB)
  bonds = bleKbBondCount();
  ble = bleKbConnected() ? 1 : 0;
  bleKbBondAddr(bond, sizeof(bond));
#endif
  static const char *UIN[] = {"none", "wizard", "status", "settings", "confirm"};
  static const char *KBN[] = {"off", "auto", "always"};
  char buf[220];
  snprintf(buf, sizeof(buf),
           "I|fw=%s|name=%s|ble=%d|bond=%d|peer=%s|bat=%d|pct=%d|pwr=%s|usb=%d"
           "|bri=%u|sleep=%u|kb=%s|setup=%d|ui=%s|st=%s|tmp=%d|fms=%lu|scr=%d",
           FW_VERSION, bleName, ble, bonds, bond, batMillivolts(), batPercent(),
           batPowerName(batPower()), batUsbHost() ? 1 : 0, cfgBri, SLEEP_OPTIONS[cfgSleep],
           KBN[cfgKb], cfgSetupDone ? 1 : 0, UIN[ui], stateName(st),
           (int)temperatureRead(), (unsigned long)frameMs, screenOn ? 1 : 0);
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

  char a[28];
  snprintf(a, sizeof(a), "A|%s", stateName(s));
  sendLine(a);
}

void openUi(UiScreen s) {
  ui = s;
  uiDirty = true;
  uiLastTouch = millis();
  wakeScreen();
  if (s == UI_NONE) { dirty = true; lastFrame = 0; }
}

void startWizard(uint8_t page) {
  wizPage = page;
  kbTest = 0;
  openUi(UI_WIZARD);
}

void forgetPairing() {
#if !defined(USB_HID_KB)
  bleKbForgetAll();
#endif
}

void applyCfg(const String &key, const String &val) {
  if (key == "bri") {
    int v = val.toInt();
    if (v >= 0 && v < BRI_COUNT) cfgBri = v;
    if (screenOn) ledcWrite(BL_CH, blLevel());
  } else if (key == "sleep") {
    int v = val.toInt(), best = 0;
    for (int i = 0; i < SLEEP_COUNT; i++)   // nearest option, 0 = never
      if (abs((int)SLEEP_OPTIONS[i] - v) < abs((int)SLEEP_OPTIONS[best] - v)) best = i;
    cfgSleep = best;
  } else if (key == "kb") {
    cfgKb = val == "off" ? KB_OFF : val == "always" ? KB_ALWAYS : KB_AUTO;
  } else if (key == "setup") {
    cfgSetupDone = val.toInt() != 0;
    if (!cfgSetupDone) startWizard(0);
  } else {
    return;
  }
  saveSettings();
  uiDirty = true;
}

void parseLine(const String &line) {
  String tok[5];
  int n = 0, start = 0;
  for (int i = 0; i <= (int)line.length() && n < 5; i++) {
    if (i == (int)line.length() || (line[i] == '|' && n < 4)) {
      tok[n++] = line.substring(start, i);
      start = i + 1;
    }
  }
  if (n < 2 || tok[0] != "S") return;
  lastHostRx = millis();
  if (lastHostRx == 0) lastHostRx = 1;
  String cmd = tok[1];

  // ---- control commands (never change the agent state) ----
  if (cmd == "ping" || cmd == "info") { sendInfo(); return; }
  if (cmd == "host") {
    strncpy(hostName, tok[2].c_str(), sizeof(hostName) - 1);
    hostName[sizeof(hostName) - 1] = 0;
    uiDirty = true;
    sendInfo();
    return;
  }
  if (cmd == "cfg") { applyCfg(tok[2], tok[3]); sendInfo(); return; }
  if (cmd == "unpair") { forgetPairing(); startWizard(1); sendInfo(); return; }
  if (cmd == "wizard") { startWizard(0); sendInfo(); return; }
  if (cmd == "reboot") { Serial.flush(); delay(50); ESP.restart(); return; }
  if (cmd == "cancel") {
    // The host resolved the approval elsewhere (terminal dialog, timeout).
    if (st == ST_APPROVE && (tok[2].length() == 0 || tok[2] == approveId)) {
      approveId[0] = 0;
      applyState(ST_THINK, "");
      if (uiResume != UI_NONE) { openUi(uiResume); uiResume = UI_NONE; }
    }
    return;
  }

  // ---- agent state ----
  // Protect the approved/rejected reaction window from late status updates.
  if ((st == ST_APPROVED || st == ST_REJECTED) && millis() < reactionUntil
      && (cmd == "think" || cmd == "idle" || cmd == "tool")) return;
  // A state update over a pending approval means the host gave up on it
  // (the DSH plugin does this when it falls back to its GUI): drop it.
  if (st == ST_APPROVE && (cmd == "think" || cmd == "idle" || cmd == "tool")) {
    approveId[0] = 0;
    if (uiResume != UI_NONE) { openUi(uiResume); uiResume = UI_NONE; }
  }

  if (cmd == "idle")       applyState(ST_IDLE, "");
  else if (cmd == "think") applyState(ST_THINK, "");
  else if (cmd == "tool")  applyState(ST_TOOL, tok[2].c_str());
  else if (cmd == "error") applyState(ST_ERROR, tok[2].c_str());
  else if (cmd == "approve") {
    strncpy(approveId, tok[2].c_str(), sizeof(approveId) - 1);
    approveId[sizeof(approveId) - 1] = 0;
    if (ui == UI_WIZARD) uiResume = UI_WIZARD;   // come back after the answer
    if (ui != UI_NONE) ui = UI_NONE;
    afterReaction = ST_THINK;
    applyState(ST_APPROVE, tok[3].c_str());
  }
}

// ------------------------- touch -------------------------
void answerApproval(int ok) {
  bool typeKey = cfgKb == KB_ALWAYS || (cfgKb == KB_AUTO && !hostLinked());
  if (typeKey) kbRaw(ok ? 'y' : 'n');
  sendApprove(approveId, ok);
  approveId[0] = 0;
  afterReaction = ST_THINK;
  applyState(ok ? ST_APPROVED : ST_REJECTED, "");
}

void doAction(UiAction a) {
  switch (a) {
    case ACT_NONE: return;
    // ---- wizard ----
    case ACT_NEXT:
      if (ui == UI_WIZARD && wizPage < WIZ_PAGES - 1) wizPage++;
      break;
    case ACT_SKIP:
      if (ui == UI_WIZARD && wizPage > 0 && wizPage < WIZ_PAGES - 1) { wizPage++; break; }
      // "SKIP SETUP" on the welcome page: fall through to finish
    case ACT_FINISH:
      cfgSetupDone = true;
      saveSettings();
      openUi(UI_NONE);
      afterReaction = ST_IDLE;               // celebrate, then rest
      applyState(ST_APPROVED, "");
      return;
    case ACT_BACK:
      if (ui == UI_WIZARD && wizPage > 0) wizPage--;
      else if (ui == UI_SETTINGS) ui = UI_STATUS;
      break;
    case ACT_KB_TEST:
#if !defined(USB_HID_KB)
      if (bleKbConnected()) { bleKbType("buddy ok"); kbTest = 1; }
      else kbTest = 2;
#else
      usbKeyboard.print("buddy ok"); kbTest = 1;
#endif
      break;
    // ---- status / settings ----
    case ACT_CLOSE:    openUi(UI_NONE); return;
    case ACT_SETTINGS: ui = UI_SETTINGS; break;
    case ACT_BRI_DN:   if (cfgBri > 0) cfgBri--; ledcWrite(BL_CH, blLevel()); saveSettings(); break;
    case ACT_BRI_UP:   if (cfgBri < BRI_COUNT - 1) cfgBri++; ledcWrite(BL_CH, blLevel()); saveSettings(); break;
    case ACT_SLEEP_DN: if (cfgSleep > 0) cfgSleep--; saveSettings(); break;
    case ACT_SLEEP_UP: if (cfgSleep < SLEEP_COUNT - 1) cfgSleep++; saveSettings(); break;
    case ACT_KB_CYCLE: cfgKb = (cfgKb + 1) % 3; saveSettings(); break;
    case ACT_REPAIR:   ui = UI_CONFIRM; break;
    case ACT_WIZARD:   startWizard(0); break;
    case ACT_CONFIRM_NO:  ui = UI_SETTINGS; break;
    case ACT_CONFIRM_YES: forgetPairing(); startWizard(1); break;
  }
  uiDirty = true;
}

void onTap(int x, int y) {
  {
    char dbg[40];
    snprintf(dbg, sizeof(dbg), "T|%d|%d", x, y);
    sendLine(dbg);
  }
  if (!screenOn) { wakeScreen(); return; }   // first tap on a dark screen only wakes it
  wakeScreen();

  if (st == ST_APPROVE) {
    if (y < 100 || y > 162) return;
    answerApproval(x < 160 ? 1 : 0);
    return;
  }
  if (ui == UI_NONE) {
    if (st != ST_APPROVED && st != ST_REJECTED) openUi(UI_STATUS);
    return;
  }
  uiLastTouch = millis();
  UiModel m;
  buildModel(m);
  doAction(uiHit(ui, m, x, y));
}

// ------------------------- setup / loop -------------------------
void setup() {
  // 160 MHz: a full frame takes ~45 ms instead of ~30 ms, still under the
  // fastest animation period (70 ms), and the chip runs noticeably cooler.
  setCpuFrequencyMhz(160);
  // Keep the backlight off during boot: a floating pin lets the panel show
  // uninitialised GRAM noise before the first clean frame.
  pinMode(GFX_BL, OUTPUT);
  digitalWrite(GFX_BL, LOW);

  pinMode(LCD_RST, OUTPUT);
  digitalWrite(LCD_RST, 0);
  delay(10);
  digitalWrite(LCD_RST, 1);

  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);
  delay(200);
  Serial.println("AgentBuddy boot");

  loadSettings();

  if (!gfx->begin()) Serial.println("gfx FAILED");
  lcd_reg_init();
  gfx->setRotation(1);        // landscape 320x172, flipped 180° vs. rotation 3: USB-C on the right

  BL_BEGIN();
  ledcWrite(BL_CH, 0);          // stay dark until the first clean frame

  if (!canvas->begin(GFX_SKIP_OUTPUT_BEGIN)) Serial.println("canvas FAILED");
  // Wipe the panel once so uninitialised GRAM noise never shows at boot.
  canvas->fillScreen(C565(0, 0, 0));
  canvas->flush();
  ledcWrite(BL_CH, blLevel());

  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  bsp_touch_init(&Wire, TOUCH_RST, TOUCH_INT, gfx->getRotation(), gfx->width(), gfx->height());

  // Unique, readable BLE name so the right board is easy to find in the list.
  {
    uint64_t mac = ESP.getEfuseMac();
    snprintf(bleName, sizeof(bleName), "AgentBuddy-%02X%02X",
             (unsigned)((mac >> 32) & 0xFF), (unsigned)((mac >> 40) & 0xFF));
  }

#if defined(USB_HID_KB)
  USB.begin();
  usbKeyboard.begin();
  Serial.println("USB HID keyboard ready");
#else
  bleKbBegin(bleName);
  Serial.println("BLE keyboard ready");
#endif

  batBegin();
  batPoll();

  sendLine("E|hello");
  Serial.println("AgentBuddy ready");

  // Boot splash: the keyboard cat, played twice.
  {
    int n = ANIM_COUNT[ANIM_BOOT];
    for (int loop = 0; loop < 2; loop++) {
      for (int i = 0; i < n; i++) {
        drawAnimFrame(ANIM_BOOT, i);
        canvas->flush();
        delay(60);
      }
    }
  }

  applyState(ST_IDLE, "");
  if (!cfgSetupDone) startWizard(0);
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

  // Tap = first touch report after >180 ms of silence (the AXS5106L reports
  // continuously while a finger is down), so holding a finger never repeats.
  bsp_touch_read();
  if (bsp_touch_get_coordinates(&td)) {
    bool newTap = millis() - lastTouchSeen > 180;
    lastTouchSeen = millis();
    if (newTap) onTap(td.coords[0].x, td.coords[0].y);
    else lastActivity = millis();
  }

  // Battery: sampled every 2 s, never while an approval is on screen.
  if (st != ST_APPROVE && batPoll()) {
    uiDirty = true;
#if !defined(USB_HID_KB)
    if (batPercent() >= 0) bleKbSetBattery(batPercent());
#endif
  }

  // Screen sleep: only when resting on the idle cat.
  uint16_t sleepS = SLEEP_OPTIONS[cfgSleep];
  if (st == ST_IDLE && ui == UI_NONE && screenOn && sleepS &&
      millis() - lastActivity >= sleepS * 1000UL) {
    sleepScreen();
  }

  // Status / settings close by themselves; the wizard waits for the user.
  if ((ui == UI_STATUS || ui == UI_SETTINGS || ui == UI_CONFIRM) &&
      millis() - uiLastTouch > UI_AUTOCLOSE_MS) {
    openUi(UI_NONE);
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
  } else if (ui != UI_NONE) {
    // Live screens (pairing / link status, blinking dots): ~4 fps is plenty.
    if (uiDirty || millis() - uiLastDraw >= 250) {
      uiLastDraw = millis();
      uiDirty = false;
      UiModel m;
      buildModel(m);
      uiDraw(canvas, ui, m);
      canvas->flush();
    }
    // Keep reactions moving underneath so the state machine stays consistent.
    if ((st == ST_APPROVED || st == ST_REJECTED) && millis() >= reactionUntil) {
      applyState(afterReaction, "");
    }
  } else {
    if ((st == ST_APPROVED || st == ST_REJECTED) && millis() >= reactionUntil) {
      applyState(afterReaction, "");
      if (uiResume != UI_NONE) { openUi(uiResume); uiResume = UI_NONE; }
    }
    // Dark screen (idle only): no point pushing 110 KB frames into a sleeping panel.
    if (screenOn && millis() - lastFrame >= periodForState(st)) {
      lastFrame = millis();
      drawScene();
      canvas->flush();
      frameMs = millis() - lastFrame;
    }
  }
  delay(screenOn ? 2 : 20);    // dark: 50 Hz touch polling is plenty to catch a wake tap
}
