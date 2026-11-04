#include "ble_kb.h"

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEHIDDevice.h>
#include <BLE2902.h>

static BLEServer *s_srv = nullptr;
static BLEHIDDevice *s_hid = nullptr;
static BLECharacteristic *s_input = nullptr;
static volatile bool s_connected = false;
static bool s_inited = false;

// Standard boot-keyboard HID report descriptor (report id 1).
static const uint8_t REPORT_MAP[] = {
  0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01,
  0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
  0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
  0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65, 0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00,
  0xC0
};

class SrvCb : public BLEServerCallbacks {
  void onConnect(BLEServer *) override { s_connected = true; }
  void onDisconnect(BLEServer *s) override {
    s_connected = false;
    if (s) s->startAdvertising();
  }
};

static uint8_t asciiToHid(uint8_t c) {
  if (c >= 'a' && c <= 'z') return 0x04 + (c - 'a');
  if (c >= 'A' && c <= 'Z') return 0x04 + (c - 'A');
  if (c >= '1' && c <= '9') return 0x1E + (c - '1');
  if (c == '0') return 0x27;
  if (c == '\n' || c == '\r') return 0x28;
  if (c == '\b') return 0x2A;
  if (c == '\t') return 0x2B;
  if (c == ' ') return 0x2C;
  if (c == 0x1B) return 0x29;
  return 0;
}

void bleKbBegin(const char *name) {
  if (s_inited) return;
  BLEDevice::init(name);
  s_srv = BLEDevice::createServer();
  s_srv->setCallbacks(new SrvCb());

  s_hid = new BLEHIDDevice(s_srv);
  s_hid->manufacturer();            // creates the 0x2a29 characteristic
  s_hid->manufacturer("DeepSeek");  // then sets its value
  s_hid->pnp(0x02, 0x1234, 0x5678, 0x0100);
  s_hid->hidInfo(0x00, 0x01);
  s_hid->reportMap((uint8_t *)REPORT_MAP, sizeof(REPORT_MAP));
  s_hid->startServices();
  s_input = s_hid->inputReport(1);
  s_hid->setBatteryLevel(100);

  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->setAppearance(HID_KEYBOARD);
  adv->addServiceUUID(s_hid->hidService()->getUUID());
  adv->setScanResponse(true);
  adv->start();

  s_inited = true;
}

bool bleKbConnected() { return s_connected; }

void bleKbReport(uint8_t modifier, uint8_t key) {
  if (!s_inited || !s_input) return;
  if (!s_connected) return;   // notify() without a peer crashes (LoadProhibited)
  uint8_t rep[8] = {modifier, 0, key, 0, 0, 0, 0, 0};
  s_input->setValue(rep, 8);
  s_input->notify();
  delay(8);
  uint8_t rel[8] = {0};
  s_input->setValue(rel, 8);
  s_input->notify();
}

void bleKbKey(uint8_t ascii) {
  uint8_t key = asciiToHid(ascii);
  if (!key) return;
  bleKbReport(0, key);
}
