// Agent Buddy on-device UI: first-run pairing wizard, status and settings.
//
// Pure drawing + hit testing. nyancat.ino owns the state, builds a UiModel
// snapshot and calls uiDraw()/uiHit(). Button geometry lives in one table
// (uiButtons) so drawing and touch can never disagree.
#pragma once
#include <Arduino.h>

class Arduino_GFX;

#define FW_VERSION "2.0"

enum UiScreen : uint8_t { UI_NONE = 0, UI_WIZARD, UI_STATUS, UI_SETTINGS, UI_CONFIRM };

enum UiAction : uint8_t {
  ACT_NONE = 0,
  ACT_NEXT, ACT_BACK, ACT_SKIP, ACT_CLOSE,
  ACT_SETTINGS,
  ACT_BRI_DN, ACT_BRI_UP, ACT_SLEEP_DN, ACT_SLEEP_UP, ACT_KB_CYCLE,
  ACT_REPAIR, ACT_WIZARD,
  ACT_CONFIRM_YES, ACT_CONFIRM_NO,
  ACT_KB_TEST, ACT_FINISH,
};

enum KbMode : uint8_t { KB_OFF = 0, KB_AUTO = 1, KB_ALWAYS = 2 };

#define WIZ_PAGES 4   // 0 welcome, 1 bluetooth, 2 agent link, 3 test

struct UiModel {
  // power
  int batMv;            // 0 = unknown
  int batPct;           // -1 = unknown
  uint8_t power;        // PowerState
  // bluetooth
  bool bleConnected;
  int bondCount;
  char bleName[24];
  char bondAddr[20];
  uint32_t passkey;
  // agent link over USB
  bool hostLinked;
  char hostName[16];
  const char *agentState;
  // settings
  uint8_t brightness;   // 0..4
  uint8_t sleepIdx;     // index into SLEEP_OPTIONS
  uint8_t kbMode;       // KbMode
  // wizard
  uint8_t wizPage;
  uint8_t kbTest;       // 0 not run, 1 typed, 2 no keyboard link
  bool blink;
};

static const uint16_t SLEEP_OPTIONS[] = {5, 10, 30, 60, 0};   // seconds, 0 = never
#define SLEEP_COUNT 5
#define BRI_COUNT 5

struct UiButton {
  int16_t x, y, w, h;
  UiAction act;
  const char *label;
  uint16_t bg, fg;
  bool enabled;
};

int uiButtons(UiScreen s, const UiModel &m, UiButton *out, int max);
void uiDraw(Arduino_GFX *c, UiScreen s, const UiModel &m);
UiAction uiHit(UiScreen s, const UiModel &m, int x, int y);

// Small battery glyph, reused as an overlay on the cat animations.
void uiBatteryIcon(Arduino_GFX *c, int x, int y, int pct, uint8_t power, bool blink);
