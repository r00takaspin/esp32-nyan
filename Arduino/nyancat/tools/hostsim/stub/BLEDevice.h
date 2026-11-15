#pragma once
#include "Arduino.h"
// Mimics the ESP32 Arduino core 2.0.14 BLE API surface used by ble_kb.cpp
typedef uint8_t esp_bd_addr_t[6];
typedef int esp_err_t;
#define ESP_OK 0
typedef struct { esp_bd_addr_t bd_addr; } esp_ble_bond_dev_t;
typedef struct { bool success; esp_bd_addr_t bd_addr; int fail_reason; } esp_ble_auth_cmpl_t;
typedef union { struct { uint16_t conn_id; esp_bd_addr_t remote_bda; } connect; } esp_ble_gatts_cb_param_t;
typedef uint8_t esp_ble_auth_req_t; typedef uint8_t esp_ble_io_cap_t;
#define ESP_LE_AUTH_REQ_SC_BOND 0x09
#define ESP_IO_CAP_NONE 3
#define ESP_BLE_ENC_KEY_MASK 1
#define ESP_BLE_ID_KEY_MASK 2
#define HID_KEYBOARD 0x03C1
extern int g_bonds; extern bool g_ble_conn;
inline int esp_ble_get_bond_device_num(){return g_bonds;}
inline esp_err_t esp_ble_get_bond_device_list(int*n,esp_ble_bond_dev_t*l){for(int i=0;i<*n;i++){uint8_t a[6]={0x7C,0x04,0xD0,0x11,0x22,(uint8_t)i};memcpy(l[i].bd_addr,a,6);}return ESP_OK;}
inline esp_err_t esp_ble_remove_bond_device(esp_bd_addr_t){if(g_bonds)g_bonds--;return ESP_OK;}
struct BLEUUID{}; struct BLEService{BLEUUID getUUID(){return {};}};
struct BLECharacteristic{void setValue(uint8_t*,size_t){} void notify(){}};
class BLEServer; struct BLEServerCallbacks{virtual ~BLEServerCallbacks(){} virtual void onConnect(BLEServer*){} virtual void onConnect(BLEServer*,esp_ble_gatts_cb_param_t*){} virtual void onDisconnect(BLEServer*){} };
class BLEServer{public: BLEServerCallbacks*cb=nullptr; void setCallbacks(BLEServerCallbacks*c){cb=c;} void startAdvertising(){} uint16_t getConnId(){return 0;} void disconnect(uint16_t){g_ble_conn=false; if(cb)cb->onDisconnect(this);} };
struct BLESecurityCallbacks{virtual ~BLESecurityCallbacks(){} virtual uint32_t onPassKeyRequest()=0; virtual void onPassKeyNotify(uint32_t)=0; virtual bool onSecurityRequest()=0; virtual void onAuthenticationComplete(esp_ble_auth_cmpl_t)=0; virtual bool onConfirmPIN(uint32_t)=0;};
struct BLESecurity{ void setAuthenticationMode(esp_ble_auth_req_t){} void setCapability(esp_ble_io_cap_t){} void setInitEncryptionKey(uint8_t){} void setRespEncryptionKey(uint8_t){} };
struct BLEAdvertising{ void setAppearance(uint16_t){} void addServiceUUID(BLEUUID){} void setScanResponse(bool){} void start(){} };
extern BLEServer*g_srv;
struct BLEDevice{ static void init(const char*){} static BLEServer*createServer(){return g_srv=new BLEServer();} static BLEAdvertising*getAdvertising(){static BLEAdvertising a;return &a;} static void startAdvertising(){} static void setSecurityCallbacks(BLESecurityCallbacks*){} };
struct BLEHIDDevice{ BLEHIDDevice(BLEServer*){} BLECharacteristic*manufacturer(){static BLECharacteristic c;return &c;} void manufacturer(const char*){} void pnp(int,int,int,int){} void hidInfo(int,int){} void reportMap(uint8_t*,size_t){} void startServices(){} BLECharacteristic*inputReport(int){static BLECharacteristic c;return &c;} void setBatteryLevel(uint8_t){} BLEService*hidService(){static BLEService s;return &s;} };
