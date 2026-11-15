#include "battery.h"

#if !defined(USB_HID_KB)
#include "soc/soc.h"
#include "soc/usb_serial_jtag_reg.h"
#define HAVE_SOF 1
#else
#define HAVE_SOF 0
#endif

#define BAT_PIN      12
#define BAT_POLL_MS  2000
#define BAT_SAMPLES  8

static int s_mv = 0;
static int s_pct = -1;
static bool s_usb = false;
static PowerState s_pwr = PWR_UNKNOWN;
static uint32_t s_lastPoll = 0;
static uint32_t s_lastSof = 0xFFFFFFFF;

// Typical 1-cell LiPo resting curve (mV -> %).
static const int16_t CURVE[][2] = {
  {4150, 100}, {4100, 94}, {4000, 83}, {3900, 71}, {3800, 56}, {3750, 46},
  {3700, 35}, {3650, 24}, {3600, 15}, {3500, 7}, {3400, 3}, {3300, 0},
};

static int mvToPct(int mv) {
  const int n = sizeof(CURVE) / sizeof(CURVE[0]);
  if (mv >= CURVE[0][0]) return 100;
  if (mv <= CURVE[n - 1][0]) return 0;
  for (int i = 1; i < n; i++) {
    if (mv >= CURVE[i][0]) {
      int v0 = CURVE[i][0], v1 = CURVE[i - 1][0];
      int p0 = CURVE[i][1], p1 = CURVE[i - 1][1];
      return p0 + (mv - v0) * (p1 - p0) / (v1 - v0);
    }
  }
  return 0;
}

void batBegin() {
#if FEATURE_BATTERY
  analogReadResolution(12);
  analogSetPinAttenuation(BAT_PIN, ADC_11db);
#endif
}

static bool usbHostPresent() {
#if HAVE_SOF
  // The 11-bit frame counter advances every 1 ms while a host is attached.
  uint32_t sof = REG_READ(USB_SERIAL_JTAG_FRAM_NUM_REG) & 0x7FF;
  bool present = (s_lastSof != 0xFFFFFFFF) && (sof != s_lastSof);
  s_lastSof = sof;
  return present;
#else
  return false;
#endif
}

bool batPoll() {
  uint32_t now = millis();
  if (s_lastPoll && now - s_lastPoll < BAT_POLL_MS) return false;
  s_lastPoll = now;

  int oldPct = s_pct;
  PowerState oldPwr = s_pwr;
  s_usb = usbHostPresent();

#if FEATURE_BATTERY
  uint32_t acc = 0;
  for (int i = 0; i < BAT_SAMPLES; i++) acc += analogReadMilliVolts(BAT_PIN);
  int mv = (int)(acc / BAT_SAMPLES) * 3;
  if (mv > 500) {                           // ignore obviously broken reads
    s_mv = s_mv ? (s_mv * 3 + mv) / 4 : mv;  // light smoothing
    s_pct = mvToPct(s_mv);
  }
#endif

  if (s_mv == 0)            s_pwr = s_usb ? PWR_CHARGING : PWR_UNKNOWN;
  else if (!s_usb)          s_pwr = PWR_BATTERY;
  else if (s_mv >= 4150)    s_pwr = PWR_FULL;
  else                      s_pwr = PWR_CHARGING;

  return oldPct != s_pct || oldPwr != s_pwr;
}

int batMillivolts() { return s_mv; }
int batPercent() { return s_pct; }
bool batUsbHost() { return s_usb; }
PowerState batPower() { return s_pwr; }

const char *batPowerName(PowerState p) {
  switch (p) {
    case PWR_BATTERY:  return "battery";
    case PWR_CHARGING: return "charging";
    case PWR_FULL:     return "full";
    default:           return "unknown";
  }
}

bool batUsbProbe(uint32_t ms) {
#if HAVE_SOF
  uint32_t a = REG_READ(USB_SERIAL_JTAG_FRAM_NUM_REG) & 0x7FF;
  delay(ms);
  return (REG_READ(USB_SERIAL_JTAG_FRAM_NUM_REG) & 0x7FF) != a;
#else
  return false;
#endif
}

// ---------------- discharge log ----------------
#define BATLOG_MAGIC 0xBA7105A1u
struct BatLog {
  uint32_t magic;
  uint16_t head, count;
  uint16_t s[BATLOG_N];
};
RTC_NOINIT_ATTR static BatLog s_log;
static uint32_t s_logLast = 0;
static bool s_logChecked = false;

static void logCheck() {
  if (s_logChecked) return;
  s_logChecked = true;
  if (s_log.magic != BATLOG_MAGIC || s_log.head >= BATLOG_N || s_log.count > BATLOG_N) batLogClear();
}

void batLogClear() {
  s_log.magic = BATLOG_MAGIC;
  s_log.head = 0;
  s_log.count = 0;
  s_logChecked = true;
}

void batLogTick(uint16_t flags) {
  logCheck();
  uint32_t now = millis();
  if (s_logLast && now - s_logLast < BATLOG_EVERY_MS) return;
  if (s_mv == 0) return;                     // nothing measured yet
  s_logLast = now ? now : 1;
  uint16_t v = (uint16_t)(s_mv > 0x1FFF ? 0x1FFF : s_mv) | flags | (s_usb ? BATLOG_USB : 0);
  s_log.s[s_log.head] = v;
  s_log.head = (s_log.head + 1) % BATLOG_N;
  if (s_log.count < BATLOG_N) s_log.count++;
}

uint16_t batLogCount() { logCheck(); return s_log.count; }

uint16_t batLogAt(uint16_t i) {
  logCheck();
  if (i >= s_log.count) return 0;
  return s_log.s[(s_log.head + BATLOG_N - s_log.count + i) % BATLOG_N];
}
