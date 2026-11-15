#include "buddy_ui.h"
#include <Arduino_GFX_Library.h>
#include "battery.h"

#define C565(r, g, b) ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))
static const uint16_t K_BG     = C565(18, 20, 34);
static const uint16_t K_HDR    = C565(34, 38, 66);
static const uint16_t K_WHITE  = C565(255, 255, 255);
static const uint16_t K_TEXT   = C565(214, 220, 236);
static const uint16_t K_DIM    = C565(130, 138, 160);
static const uint16_t K_BLACK  = C565(0, 0, 0);
static const uint16_t K_GREEN  = C565(60, 220, 90);
static const uint16_t K_RED    = C565(240, 60, 60);
static const uint16_t K_AMBER  = C565(255, 190, 40);
static const uint16_t K_CYAN   = C565(80, 200, 255);
static const uint16_t K_PINK   = C565(255, 120, 200);
static const uint16_t K_BTN    = C565(58, 64, 100);
static const uint16_t K_BTN_OFF = C565(40, 44, 64);

#define W 320
#define H 172
#define BTN_Y 134
#define BTN_H 32

// ------------------------------------------------------------ text helpers
static void txt(Arduino_GFX *c, int x, int y, const char *s, uint16_t col, int sz = 1) {
  c->setTextSize(sz);
  c->setTextColor(col);
  c->setCursor(x, y);
  c->print(s);
}

static int txtW(const char *s, int sz) { return (int)strlen(s) * 6 * sz; }

static void txtC(Arduino_GFX *c, int cx, int y, const char *s, uint16_t col, int sz) {
  txt(c, cx - txtW(s, sz) / 2, y, s, col, sz);
}

static void header(Arduino_GFX *c, const char *title, const UiModel &m, const char *right = nullptr) {
  c->fillRect(0, 0, W, 22, K_HDR);
  txt(c, 8, 4, title, K_WHITE, 2);
  int bx = W - 34;
  if (right) txt(c, bx - 6 - txtW(right, 1), 8, right, K_DIM, 1);
  uiBatteryIcon(c, bx, 6, m.batPct, m.power, m.blink);
}

static void dot(Arduino_GFX *c, int x, int y, uint16_t col) {
  c->fillRect(x, y + 1, 5, 5, col);
}

// ------------------------------------------------------------ battery glyph
void uiBatteryIcon(Arduino_GFX *c, int x, int y, int pct, uint8_t power, bool blink) {
  const int bw = 24, bh = 11;
  uint16_t frame = K_TEXT;
  c->drawRect(x, y, bw, bh, frame);
  c->fillRect(x + bw, y + 3, 2, bh - 6, frame);
  int p = pct < 0 ? 0 : pct;
  uint16_t fill = p <= 15 ? K_RED : p <= 35 ? K_AMBER : K_GREEN;
  if (power == PWR_CHARGING) {
    fill = K_CYAN;
    if (blink) p = p + 15 > 100 ? 100 : p + 15;     // "filling up" pulse
  }
  int fw = (bw - 4) * p / 100;
  c->fillRect(x + 2, y + 2, bw - 4, bh - 4, K_BLACK);
  if (fw > 0) c->fillRect(x + 2, y + 2, fw, bh - 4, fill);
  if (power == PWR_CHARGING || power == PWR_FULL) {
    // tiny lightning bolt in the middle
    int mx = x + bw / 2;
    c->drawLine(mx + 1, y + 1, mx - 2, y + 5, K_WHITE);
    c->drawLine(mx - 2, y + 5, mx + 2, y + 5, K_WHITE);
    c->drawLine(mx + 2, y + 5, mx - 1, y + 9, K_WHITE);
  } else if (pct < 0) {
    txt(c, x + 9, y + 2, "?", K_DIM, 1);
  }
}

// ------------------------------------------------------------ buttons
static int addBtn(UiButton *out, int n, int max, int x, int y, int w, int h, UiAction a,
                  const char *label, uint16_t bg, uint16_t fg = K_BLACK, bool en = true) {
  if (n >= max) return n;
  out[n] = {(int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h, a, label, bg, fg, en};
  return n + 1;
}

static const char *kbModeName(uint8_t k) {
  return k == KB_OFF ? "OFF" : k == KB_ALWAYS ? "ALWAYS" : "AUTO";
}

int uiButtons(UiScreen s, const UiModel &m, UiButton *o, int max) {
  int n = 0;
  switch (s) {
    case UI_WIZARD:
      if (m.wizPage == 0) {
        n = addBtn(o, n, max, 10, BTN_Y, 120, BTN_H, ACT_SKIP, "SKIP SETUP", K_BTN, K_TEXT);
        n = addBtn(o, n, max, 190, BTN_Y, 120, BTN_H, ACT_NEXT, "START >", K_GREEN);
      } else if (m.wizPage == 1 || m.wizPage == 2) {
        bool ok = (m.wizPage == 1) ? m.bleConnected : m.hostLinked;
        n = addBtn(o, n, max, 10, BTN_Y, 90, BTN_H, ACT_BACK, "< BACK", K_BTN, K_TEXT);
        n = addBtn(o, n, max, 115, BTN_Y, 90, BTN_H, ACT_SKIP, "SKIP", K_BTN, K_TEXT);
        n = addBtn(o, n, max, 220, BTN_Y, 90, BTN_H, ACT_NEXT, "NEXT >",
                   ok ? K_GREEN : K_BTN_OFF, ok ? K_BLACK : K_DIM, ok);
      } else {
        n = addBtn(o, n, max, 10, BTN_Y, 90, BTN_H, ACT_BACK, "< BACK", K_BTN, K_TEXT);
        n = addBtn(o, n, max, 110, BTN_Y, 100, BTN_H, ACT_KB_TEST, "TEST KEYS",
                   m.bleConnected ? K_CYAN : K_BTN_OFF, m.bleConnected ? K_BLACK : K_DIM, m.bleConnected);
        n = addBtn(o, n, max, 220, BTN_Y, 90, BTN_H, ACT_FINISH, "DONE", K_GREEN);
      }
      break;
    case UI_STATUS:
      n = addBtn(o, n, max, 10, BTN_Y, 140, BTN_H, ACT_CLOSE, "< BACK", K_BTN, K_TEXT);
      n = addBtn(o, n, max, 170, BTN_Y, 140, BTN_H, ACT_SETTINGS, "SETTINGS", K_AMBER);
      break;
    case UI_SETTINGS:
      n = addBtn(o, n, max, 168, 28, 34, 26, ACT_BRI_DN, "-", K_BTN, K_WHITE);
      n = addBtn(o, n, max, 276, 28, 34, 26, ACT_BRI_UP, "+", K_BTN, K_WHITE);
      n = addBtn(o, n, max, 168, 62, 34, 26, ACT_SLEEP_DN, "-", K_BTN, K_WHITE);
      n = addBtn(o, n, max, 276, 62, 34, 26, ACT_SLEEP_UP, "+", K_BTN, K_WHITE);
      n = addBtn(o, n, max, 168, 96, 142, 26, ACT_KB_CYCLE, kbModeName(m.kbMode), K_BTN, K_WHITE);
      n = addBtn(o, n, max, 10, BTN_Y, 90, BTN_H, ACT_BACK, "< BACK", K_BTN, K_TEXT);
      n = addBtn(o, n, max, 110, BTN_Y, 100, BTN_H, ACT_REPAIR, "RE-PAIR", K_PINK);
      n = addBtn(o, n, max, 220, BTN_Y, 90, BTN_H, ACT_WIZARD, "SETUP", K_CYAN);
      break;
    case UI_CONFIRM:
      n = addBtn(o, n, max, 10, BTN_Y, 140, BTN_H, ACT_CONFIRM_NO, "CANCEL", K_BTN, K_TEXT);
      n = addBtn(o, n, max, 170, BTN_Y, 140, BTN_H, ACT_CONFIRM_YES, "FORGET", K_RED);
      break;
    default:
      break;
  }
  return n;
}

static void drawButtons(Arduino_GFX *c, UiScreen s, const UiModel &m) {
  UiButton b[10];
  int n = uiButtons(s, m, b, 10);
  for (int i = 0; i < n; i++) {
    c->fillRoundRect(b[i].x, b[i].y, b[i].w, b[i].h, 8, b[i].bg);
    int sz = (b[i].h >= 30 && txtW(b[i].label, 2) <= b[i].w - 8) ? 2 : 1;
    int ty = b[i].y + (b[i].h - 8 * sz) / 2;
    txtC(c, b[i].x + b[i].w / 2, ty, b[i].label, b[i].fg, sz);
  }
}

UiAction uiHit(UiScreen s, const UiModel &m, int x, int y) {
  UiButton b[10];
  int n = uiButtons(s, m, b, 10);
  for (int i = 0; i < n; i++) {
    // 4 px of slack around every button: fingers are bigger than pixels.
    if (x >= b[i].x - 4 && x < b[i].x + b[i].w + 4 && y >= b[i].y - 4 && y < b[i].y + b[i].h + 4)
      return b[i].enabled ? b[i].act : ACT_NONE;
  }
  return ACT_NONE;
}

// ------------------------------------------------------------ screens
#define LH 11   // body line height (5x7 font, size 1)

static void wizWelcome(Arduino_GFX *c, const UiModel &m) {
  header(c, "HELLO!", m, "setup");
  int y = 30;
  txt(c, 10, y, "I'm Agent Buddy - a desk pal for your AI agent.", K_WHITE); y += LH;
  txt(c, 10, y, "I show what the agent is doing and let you", K_TEXT); y += LH;
  txt(c, 10, y, "approve its actions with a tap: YES / NO.", K_TEXT); y += LH + 6;
  txt(c, 10, y, "3 quick steps:", K_AMBER); y += LH + 2;
  txt(c, 16, y, "1. Pair me as a Bluetooth keyboard", K_TEXT); y += LH;
  txt(c, 16, y, "2. Link me to your agent over USB", K_TEXT); y += LH;
  txt(c, 16, y, "3. Test the buttons", K_TEXT);
}

static void wizBluetooth(Arduino_GFX *c, const UiModel &m) {
  header(c, "1/3 BLUETOOTH", m);
  int y = 28;
  txt(c, 10, y, "On the computer open Bluetooth settings:", K_WHITE); y += LH;
  txt(c, 16, y, "Mac: System Settings > Bluetooth", K_TEXT); y += LH;
  txt(c, 16, y, "Win: Settings > Bluetooth > Add device", K_TEXT); y += LH;
  txt(c, 10, y, "Find me in the list and press Connect:", K_WHITE); y += LH + 3;
  txtC(c, W / 2, y, m.bleName, K_AMBER, 2); y += 20;

  // live status line
  if (m.passkey) {
    char b[40];
    snprintf(b, sizeof(b), "Code: %06lu", (unsigned long)m.passkey);
    txt(c, 10, y + 2, "Confirm this code:", K_WHITE);
    txt(c, 130, y - 2, b + 6, K_CYAN, 2);
  } else if (m.bleConnected) {
    dot(c, 10, y + 2, K_GREEN);
    char b[48];
    snprintf(b, sizeof(b), "Connected!  %s", m.bondAddr[0] ? m.bondAddr : "");
    txt(c, 22, y + 2, b, K_GREEN);
  } else if (m.bondCount > 0) {
    dot(c, 10, y + 2, m.blink ? K_AMBER : K_BG);
    txt(c, 22, y + 2, "Paired before - waiting for it to reconnect", K_AMBER);
  } else {
    dot(c, 10, y + 2, m.blink ? K_AMBER : K_BG);
    txt(c, 22, y + 2, "Waiting for a computer to connect...", K_AMBER);
  }
}

static void wizAgent(Arduino_GFX *c, const UiModel &m) {
  header(c, "2/3 AGENT LINK", m);
  int y = 28;
  txt(c, 10, y, "1. Plug my USB-C cable into the computer", K_WHITE); y += LH;
  txt(c, 10, y, "2. Install the plugin:", K_WHITE); y += LH;
  txt(c, 16, y, "Claude Code:", K_CYAN);
  txt(c, 94, y, "/plugin install agent-buddy", K_TEXT); y += LH;
  txt(c, 16, y, "DeepSeek:", K_CYAN);
  txt(c, 94, y, "Settings > Plugins > buddy", K_TEXT); y += LH;
  txt(c, 10, y, "3. Start an agent session - it says hello", K_WHITE); y += LH + 6;

  bool usb = m.power == PWR_CHARGING || m.power == PWR_FULL;
  dot(c, 10, y, usb ? K_GREEN : K_DIM);
  txt(c, 22, y, usb ? "USB cable: connected" : "USB cable: not detected", usb ? K_GREEN : K_DIM);
  y += LH + 1;
  if (m.hostLinked) {
    dot(c, 10, y, K_GREEN);
    char b[48];
    snprintf(b, sizeof(b), "Agent: %s linked", m.hostName);
    txt(c, 22, y, b, K_GREEN);
  } else {
    dot(c, 10, y, m.blink ? K_AMBER : K_BG);
    txt(c, 22, y, "Agent: waiting for the plugin...", K_AMBER);
  }
}

static void wizTest(Arduino_GFX *c, const UiModel &m) {
  header(c, "3/3 TEST", m);
  int y = 28;
  txt(c, 10, y, "When the agent needs permission I light up", K_WHITE); y += LH;
  txt(c, 10, y, "and show the command with YES / NO.", K_WHITE); y += LH + 4;
  txt(c, 10, y, "Keyboard check: open any text field", K_TEXT); y += LH;
  txt(c, 10, y, "(e.g. Notes) and tap TEST KEYS -", K_TEXT); y += LH;
  txt(c, 10, y, "I will type \"buddy ok\" over Bluetooth.", K_TEXT); y += LH + 6;
  if (m.kbTest == 1) {
    dot(c, 10, y, K_GREEN);
    txt(c, 22, y, "Sent! Did the text appear? Then tap DONE.", K_GREEN);
  } else if (!m.bleConnected) {
    dot(c, 10, y, K_DIM);
    txt(c, 22, y, "Bluetooth not connected - test skipped", K_DIM);
  }
}

static void statusScreen(Arduino_GFX *c, const UiModel &m) {
  header(c, "STATUS", m, "fw " FW_VERSION);
  char b[64];

  // --- battery ---
  int y = 30;
  txt(c, 10, y, "BATTERY", K_DIM);
  // big battery
  int bx = 10, by = y + 11, bw = 46, bh = 20;
  c->drawRect(bx, by, bw, bh, K_TEXT);
  c->fillRect(bx + bw, by + 6, 3, bh - 12, K_TEXT);
  int p = m.batPct < 0 ? 0 : m.batPct;
  uint16_t fill = m.power == PWR_CHARGING ? K_CYAN : p <= 15 ? K_RED : p <= 35 ? K_AMBER : K_GREEN;
  c->fillRect(bx + 3, by + 3, (bw - 6) * p / 100, bh - 6, fill);
  if (m.batPct >= 0) snprintf(b, sizeof(b), "%d%%", m.batPct);
  else snprintf(b, sizeof(b), "--");
  txt(c, 66, y + 13, b, K_WHITE, 2);
  if (m.batMv > 0) {
    snprintf(b, sizeof(b), "%d.%02d V", m.batMv / 1000, (m.batMv % 1000) / 10);
    txt(c, 130, y + 11, b, K_TEXT);
  }
  const char *pw = "measuring...";
  uint16_t pc = K_DIM;
  switch (m.power) {
    case PWR_CHARGING: pw = "CHARGING (USB)"; pc = K_CYAN; break;
    case PWR_FULL:     pw = "FULL (on USB)"; pc = K_GREEN; break;
    case PWR_BATTERY:  pw = m.batPct >= 0 && m.batPct <= 15 ? "LOW - plug in USB" : "ON BATTERY";
                       pc = m.batPct >= 0 && m.batPct <= 15 ? K_RED : K_TEXT; break;
    default: break;
  }
  txt(c, 130, y + 23, pw, pc);

  // --- bluetooth ---
  y = 66;
  txt(c, 10, y, "BLUETOOTH", K_DIM);
  if (m.bleConnected) { dot(c, 72, y, K_GREEN); txt(c, 84, y, "PAIRED + CONNECTED", K_GREEN); }
  else if (m.bondCount > 0) { dot(c, 72, y, K_AMBER); txt(c, 84, y, "PAIRED, NOT CONNECTED", K_AMBER); }
  else { dot(c, 72, y, K_RED); txt(c, 84, y, "NOT PAIRED", K_RED); }
  snprintf(b, sizeof(b), "%s%s%s", m.bleName, m.bondAddr[0] ? "  <->  " : "", m.bondAddr);
  txt(c, 10, y + LH + 1, b, K_TEXT);

  // --- agent ---
  y = 98;
  txt(c, 10, y, "AGENT", K_DIM);
  if (m.hostLinked) {
    dot(c, 72, y, K_GREEN);
    snprintf(b, sizeof(b), "%s - %s", m.hostName, m.agentState ? m.agentState : "?");
    txt(c, 84, y, b, K_GREEN);
  } else {
    dot(c, 72, y, K_DIM);
    txt(c, 84, y, "NO LINK", K_DIM);
    txt(c, 10, y + LH + 1, "Plug in USB and start Claude Code / DeepSeek", K_DIM);
  }
}

static void settingsScreen(Arduino_GFX *c, const UiModel &m) {
  header(c, "SETTINGS", m);
  char b[24];
  // brightness
  txt(c, 10, 37, "Brightness", K_WHITE);
  for (int i = 0; i < BRI_COUNT; i++) {
    int x = 208 + i * 13;
    uint16_t col = i <= m.brightness ? K_AMBER : K_BTN_OFF;
    c->fillRect(x, 46 - (i + 1) * 3, 10, (i + 1) * 3 + 2, col);
  }
  // sleep
  txt(c, 10, 71, "Screen sleep", K_WHITE);
  uint16_t sv = SLEEP_OPTIONS[m.sleepIdx < SLEEP_COUNT ? m.sleepIdx : 1];
  if (sv == 0) snprintf(b, sizeof(b), "never");
  else snprintf(b, sizeof(b), "%us", sv);
  txtC(c, 239, 71, b, K_WHITE, 1);
  // keyboard mode
  txt(c, 10, 100, "YES/NO also types", K_WHITE);
  txt(c, 10, 111, "y / n on Bluetooth:", K_WHITE);
  const char *hint = m.kbMode == KB_AUTO ? "only when no agent link" :
                     m.kbMode == KB_ALWAYS ? "every tap" : "never";
  txtC(c, 239, 124, hint, K_DIM, 1);
}

static void confirmScreen(Arduino_GFX *c, const UiModel &m) {
  header(c, "RE-PAIR?", m);
  int y = 30;
  char b[64];
  txt(c, 10, y, "I will forget the paired computer", K_WHITE); y += LH;
  if (m.bondAddr[0]) { snprintf(b, sizeof(b), "(%s)", m.bondAddr); txt(c, 10, y, b, K_DIM); }
  y += LH + 4;
  txt(c, 10, y, "and wait for a new pairing.", K_WHITE); y += LH + 6;
  txt(c, 10, y, "IMPORTANT: on the computer also remove", K_AMBER); y += LH;
  snprintf(b, sizeof(b), "\"%s\" from Bluetooth settings", m.bleName);
  txt(c, 10, y, b, K_AMBER); y += LH;
  txt(c, 10, y, "(Forget This Device), then pair again.", K_AMBER);
}

void uiDraw(Arduino_GFX *c, UiScreen s, const UiModel &m) {
  c->fillScreen(K_BG);
  switch (s) {
    case UI_WIZARD:
      switch (m.wizPage) {
        case 0: wizWelcome(c, m); break;
        case 1: wizBluetooth(c, m); break;
        case 2: wizAgent(c, m); break;
        default: wizTest(c, m); break;
      }
      break;
    case UI_STATUS:   statusScreen(c, m); break;
    case UI_SETTINGS: settingsScreen(c, m); break;
    case UI_CONFIRM:  confirmScreen(c, m); break;
    default: return;
  }
  drawButtons(c, s, m);
}
