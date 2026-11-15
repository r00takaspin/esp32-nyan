// Battery / power monitor for the Waveshare ESP32-S3-Touch-LCD-1.47.
//
// Hardware facts (schematic, "BAT" block):
//   * VBAT -> R19 200k / R21 100k -> BAT_ADC = GPIO12 (ADC2_CH1): V_bat = ADC * 3
//   * the ETA6098 charger STAT pin only drives the red LED, it is NOT wired to a
//     GPIO, and VBUS is not sensed either.
// So "charging" is derived: a USB host is present when the USB-Serial-JTAG
// peripheral receives SOF frames, and the battery voltage tells charging vs full.
#pragma once
#include <Arduino.h>

#ifndef FEATURE_BATTERY
#define FEATURE_BATTERY 1     // set to 0 to rule the ADC out while debugging
#endif

enum PowerState : uint8_t {
  PWR_UNKNOWN = 0,   // not measured yet
  PWR_BATTERY,       // running from the cell
  PWR_CHARGING,      // USB host present, cell below ~4.15 V
  PWR_FULL,          // USB host present, cell full (or no cell fitted)
};

void batBegin();
// Call often; it samples at most every BAT_POLL_MS. Returns true when the
// displayed values changed.
bool batPoll();
int  batMillivolts();     // smoothed, 0 = unknown
int  batPercent();        // 0..100, -1 = unknown
bool batUsbHost();        // USB data host seen (SOF frames)
PowerState batPower();
const char *batPowerName(PowerState p);   // "battery" / "charging" / "full" / "unknown"
