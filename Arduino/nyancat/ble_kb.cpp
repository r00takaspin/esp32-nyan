#include "ble_kb.h"

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEHIDDevice.h>
#include <BLE2902.h>
#include <BLESecurity.h>
#include <esp_gap_ble_api.h>
#include <esp_bt.h>
#include <esp_bt_main.h>

static BLEServer *s_srv = nullptr;
static BLEHIDDevice *s_hid = nullptr;
static BLECharacteristic *s_input = nullptr;
static volatile bool s_connected = false;
static bool s_inited = false;
static uint8_t s_peer[6] = {0};
static volatile uint32_t s_passkey = 0;
static volatile int s_lastAuth = -1;
static uint8_t s_battery = 100;

// Agent link over BLE: a Nordic-UART-style service carrying the same text
// lines as USB CDC (host writes "S|..." to RX, board notifies TX lines).
#define LINK_SVC_UUID "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define LINK_RX_UUID  "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define LINK_TX_UUID  "6e400003-b5a3-f393-e0a9-e50e24dcca9e"
static BLECharacteristic *s_linkTx = nullptr;
static portMUX_TYPE s_rxMux = portMUX_INITIALIZER_UNLOCKED;
static char s_rx[512];
static volatile uint16_t s_rxLen = 0;

// Standard boot-keyboard HID report descriptor (report id 1).
static const uint8_t REPORT_MAP[] = {
  0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01,
  0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
  0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
  0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65, 0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00,
  0xC0
};

// macOS Bluetooth Settings only lists new BLE HID devices stronger than
// -60 dBm RSSI, so advertise at +9 dBm until a computer has bonded; a bonded
// Mac reconnects by itself and +3 dBm is plenty.
static void advPowerForBonds() {
  int n = esp_ble_get_bond_device_num();
  BLEDevice::setPower(n > 0 ? ESP_PWR_LVL_P3 : ESP_PWR_LVL_P9, ESP_BLE_PWR_TYPE_ADV);
}

// Runs in the BT task: only buffer here, lines are parsed in loop().
class LinkRxCb : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) override {
    std::string v = c->getValue();
    portENTER_CRITICAL(&s_rxMux);
    for (char ch : v) {
      if (s_rxLen < sizeof(s_rx) - 1) s_rx[s_rxLen++] = ch;
    }
    portEXIT_CRITICAL(&s_rxMux);
  }
};

class SrvCb : public BLEServerCallbacks {
  void onConnect(BLEServer *, esp_ble_gatts_cb_param_t *param) override {
    s_connected = true;
    if (param) memcpy(s_peer, param->connect.remote_bda, 6);
  }
  void onDisconnect(BLEServer *s) override {
    s_connected = false;
    s_passkey = 0;
    if (s) s->startAdvertising();
  }
};

#if ESP_ARDUINO_VERSION < ESP_ARDUINO_VERSION_VAL(3, 0, 0)
// "Just Works" bonding: no code to type, the host stores the keys and
// reconnects by itself after power cycles (same as ESP32-BLE-Keyboard).
class SecCb : public BLESecurityCallbacks {
  uint32_t onPassKeyRequest() override { return 0; }
  void onPassKeyNotify(uint32_t pass_key) override { s_passkey = pass_key; }
  bool onSecurityRequest() override { return true; }
  bool onConfirmPIN(uint32_t pin) override { s_passkey = pin; return true; }
  void onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl) override {
    s_lastAuth = cmpl.success ? 1 : 0;
    s_passkey = 0;
    if (cmpl.success) advPowerForBonds();
  }
};
#endif

static uint8_t asciiToHid(uint8_t c, uint8_t *mod) {
  *mod = 0;
  if (c >= 'a' && c <= 'z') return 0x04 + (c - 'a');
  if (c >= 'A' && c <= 'Z') { *mod = 0x02; return 0x04 + (c - 'A'); }
  if (c >= '1' && c <= '9') return 0x1E + (c - '1');
  if (c == '0') return 0x27;
  if (c == '\n' || c == '\r') return 0x28;
  if (c == '\b') return 0x2A;
  if (c == '\t') return 0x2B;
  if (c == ' ') return 0x2C;
  if (c == 0x1B) return 0x29;
  if (c == '-') return 0x2D;
  if (c == '.') return 0x37;
  if (c == '!') { *mod = 0x02; return 0x1E; }
  return 0;
}

void bleKbBegin(const char *name) {
  if (s_inited) return;
  BLEDevice::init(name);
  // The connection itself runs at the default +3 dBm (the Mac sits a desk away).
  BLEDevice::setPower(ESP_PWR_LVL_P3, ESP_BLE_PWR_TYPE_DEFAULT);
  advPowerForBonds();
  s_srv = BLEDevice::createServer();
  s_srv->setCallbacks(new SrvCb());

  s_hid = new BLEHIDDevice(s_srv);
  s_hid->manufacturer();            // creates the 0x2a29 characteristic
  s_hid->manufacturer("DeepSeek");  // then sets its value
  s_hid->pnp(0x02, 0x1234, 0x5678, 0x0100);
  s_hid->hidInfo(0x00, 0x01);
  s_hid->reportMap((uint8_t *)REPORT_MAP, sizeof(REPORT_MAP));
  // Must exist before startServices(): the core registers characteristics
  // with the stack only when the service starts. Created after it, the input
  // report had no service, the Mac never saw it, and notify() crashed
  // (LoadProhibited in BLEService::getServer) on the first key press.
  s_input = s_hid->inputReport(1);
  s_hid->startServices();
  s_hid->setBatteryLevel(s_battery);

  BLEService *link = s_srv->createService(LINK_SVC_UUID);
  BLECharacteristic *rx = link->createCharacteristic(
      LINK_RX_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  rx->setCallbacks(new LinkRxCb());
  s_linkTx = link->createCharacteristic(LINK_TX_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  s_linkTx->addDescriptor(new BLE2902());
  link->start();

#if ESP_ARDUINO_VERSION < ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  BLEDevice::setSecurityCallbacks(new SecCb());
  BLESecurity *sec = new BLESecurity();
  sec->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_BOND);
  sec->setCapability(ESP_IO_CAP_NONE);
  sec->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  sec->setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
#else
#warning "BLE bonding setup is written for the ESP32 Arduino core 2.0.x BLE API"
#endif

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
  delay(8);
}

void bleKbKey(uint8_t ascii) {
  uint8_t mod;
  uint8_t key = asciiToHid(ascii, &mod);
  if (!key) return;
  bleKbReport(mod, key);
}

void bleKbType(const char *s) {
  if (!s) return;
  for (; *s; s++) bleKbKey((uint8_t)*s);
}

// ---------------- bonding ----------------
static void fmtAddr(const uint8_t *a, char *out, size_t n) {
  snprintf(out, n, "%02X:%02X:%02X:%02X:%02X:%02X", a[0], a[1], a[2], a[3], a[4], a[5]);
}

int bleKbBondCount() {
  if (!s_inited) return 0;
  int n = esp_ble_get_bond_device_num();
  return n < 0 ? 0 : n;
}

bool bleKbBondAddr(char *out, size_t n) {
  if (!out || !n) return false;
  out[0] = 0;
  int cnt = bleKbBondCount();
  if (cnt <= 0) return false;
  esp_ble_bond_dev_t *list = (esp_ble_bond_dev_t *)malloc(sizeof(esp_ble_bond_dev_t) * cnt);
  if (!list) return false;
  bool ok = false;
  if (esp_ble_get_bond_device_list(&cnt, list) == ESP_OK && cnt > 0) {
    fmtAddr(list[0].bd_addr, out, n);
    ok = true;
  }
  free(list);
  return ok;
}

bool bleKbPeerAddr(char *out, size_t n) {
  if (!out || !n) return false;
  out[0] = 0;
  if (!s_connected) return false;
  fmtAddr(s_peer, out, n);
  return true;
}

void bleKbForgetAll() {
  if (!s_inited) return;
  int cnt = bleKbBondCount();
  if (cnt > 0) {
    esp_ble_bond_dev_t *list = (esp_ble_bond_dev_t *)malloc(sizeof(esp_ble_bond_dev_t) * cnt);
    if (list) {
      if (esp_ble_get_bond_device_list(&cnt, list) == ESP_OK) {
        for (int i = 0; i < cnt; i++) esp_ble_remove_bond_device(list[i].bd_addr);
      }
      free(list);
    }
  }
  s_lastAuth = -1;
  s_passkey = 0;
  advPowerForBonds();
  if (s_connected && s_srv) s_srv->disconnect(s_srv->getConnId());  // onDisconnect re-advertises
  else BLEDevice::startAdvertising();
}

uint32_t bleKbPasskey() { return s_passkey; }
int bleKbLastAuth() { return s_lastAuth; }

void bleKbSetBattery(uint8_t pct) {
  if (pct > 100) pct = 100;
  s_battery = pct;
  // In core 2.0.14 this also notify()s; that is a no-op without a peer
  // (BLECharacteristic::notify checks getConnectedCount()).
  if (s_inited && s_hid) s_hid->setBatteryLevel(pct);
}

void bleKbShutdown() {
  if (!s_inited) return;
  // The radio must be off before light sleep: the 2.0.14 core is built
  // without BT modem sleep, so the controller cannot sleep on its own.
  // There is no way back short of a restart, which is how standby wakes.
  s_inited = false;
  s_connected = false;
  esp_bluedroid_disable();
  esp_bluedroid_deinit();
  esp_bt_controller_disable();
}

// ---------------- agent link ----------------
bool bleLinkReadLine(char *out, size_t n) {
  if (!out || n < 2) return false;
  bool got = false;
  portENTER_CRITICAL(&s_rxMux);
  for (uint16_t i = 0; i < s_rxLen; i++) {
    if (s_rx[i] != '\n') continue;
    size_t len = i < n - 1 ? i : n - 1;
    memcpy(out, s_rx, len);
    out[len] = 0;
    if (len && out[len - 1] == '\r') out[len - 1] = 0;
    memmove(s_rx, s_rx + i + 1, s_rxLen - i - 1);
    s_rxLen -= i + 1;
    got = true;
    break;
  }
  if (!got && s_rxLen == sizeof(s_rx) - 1) s_rxLen = 0;   // no newline in a full buffer: drop junk
  portEXIT_CRITICAL(&s_rxMux);
  return got;
}

void bleLinkSend(const char *line) {
  if (!s_inited || !s_connected || !s_linkTx || !line) return;
  BLE2902 *cccd = (BLE2902 *)s_linkTx->getDescriptorByUUID((uint16_t)0x2902);
  if (!cccd || !cccd->getNotifications()) return;     // nobody listening
  uint16_t mtu = s_srv->getPeerMTU(s_srv->getConnId());
  size_t chunk = (mtu > 23 ? mtu : 23) - 3;
  if (chunk > 180) chunk = 180;
  size_t len = strlen(line);
  char buf[184];
  // The line plus its newline, cut into notification-sized pieces.
  for (size_t off = 0; off <= len;) {
    size_t k = 0;
    while (k < chunk && off <= len) buf[k++] = off < len ? line[off++] : (off++, '\n');
    s_linkTx->setValue((uint8_t *)buf, k);
    s_linkTx->notify();
  }
}
