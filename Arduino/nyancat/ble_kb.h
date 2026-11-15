// Minimal BLE HID keyboard backend for the Agent Buddy.
//
// v2: bonding ("Just Works", like regular BLE keyboards) so the computer
// remembers the pairing, bond inspection / forgetting for the re-pair flow,
// a HID battery level (macOS shows it in the Bluetooth menu) and string typing.
#pragma once
#include <Arduino.h>

void bleKbBegin(const char *name);
bool bleKbConnected();
void bleKbKey(uint8_t ascii);                     // press + release one ASCII key
void bleKbReport(uint8_t modifier, uint8_t key);  // raw HID report
void bleKbType(const char *s);                    // type a short ASCII string

// ---- pairing / bonding ----
int  bleKbBondCount();                       // computers that remember us
bool bleKbBondAddr(char *out, size_t n);     // "AA:BB:CC:DD:EE:FF" of the first bond
bool bleKbPeerAddr(char *out, size_t n);     // address of the connected peer
void bleKbForgetAll();                       // drop bonds, disconnect, advertise again
uint32_t bleKbPasskey();                     // passkey to show (0 = none)
int  bleKbLastAuth();                        // -1 unknown, 0 failed, 1 ok

void bleKbSetBattery(uint8_t pct);           // HID battery service value
void bleKbShutdown();

// ---- agent link over BLE (same text protocol as USB CDC) ----
bool bleLinkReadLine(char *out, size_t n);   // next complete "S|..." line from the host
void bleLinkSend(const char *line);          // notify one line (no-op without a subscriber)                        // radio off for standby (restart to bring it back)
