// Minimal BLE HID keyboard backend for the Agent Buddy.
#pragma once
#include <Arduino.h>

void bleKbBegin(const char *name);
bool bleKbConnected();
void bleKbKey(uint8_t ascii);            // press + release one ASCII key
void bleKbReport(uint8_t modifier, uint8_t key);  // raw HID report
