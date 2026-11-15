#include "Arduino.h"
#include "Wire.h"
#include "Preferences.h"
#include "BLEDevice.h"
uint32_t g_ms = 1000; int g_bl = 0; int g_adc_mv = 1300; uint32_t g_sof = 0;
int g_bonds = 0; bool g_ble_conn = false;
SerialStub Serial; EspStub ESP; TwoWire Wire;
std::map<std::string,int> Preferences::st;
BLEServer* g_srv = nullptr;
#include "esp_lcd_touch_axs5106l.h"
static bool tapPending=false; static int tapX, tapY;
void bsp_touch_read(void){}
bool bsp_touch_get_coordinates(touch_data_t*t){ if(!tapPending) return false; tapPending=false; t->touch_num=1; t->coords[0].x=tapX; t->coords[0].y=tapY; return true; }
void bsp_touch_init(TwoWire*,int,int,uint16_t,uint16_t,uint16_t){}
#include "../../nyancat.ino"

static bool usbOn = true;
void run(int ms){ for(int t=0;t<ms;t+=20){ g_ms+=20; if(usbOn) g_sof=(g_sof+20)&0x7FF; loop(); } }
void tap(int x,int y){ run(300); tapX=x; tapY=y; tapPending=true; run(60); run(300); }
void snap(const char*name){
  run(300);
  char fn[128]; snprintf(fn,sizeof fn,"out/%s.ppm",name);
  FILE*f=fopen(fn,"wb"); fprintf(f,"P6 %d %d 255\n",canvas->_w,canvas->_h);
  for(uint16_t c: canvas->fb){ uint8_t r=(c>>11)<<3,g=((c>>5)&63)<<2,b=(c&31)<<3; fputc(r|r>>5,f);fputc(g|g>>6,f);fputc(b|b>>5,f);} fclose(f);
  printf("[snap] %-28s ui=%d st=%s wiz=%d\n",name,(int)ui,stateName(st),wizPage);
}
std::string takeOut(){ std::string o=Serial.out; Serial.out.clear(); return o; }
void feed(const char*s){ Serial.feed(s); run(100); }
#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n",__LINE__,#c); fails++; } else printf("ok   %s\n",#c);}while(0)
int main(){
  int fails=0;
  g_adc_mv = 1300;  // 3.90 V
  setup(); run(2500);
  CHECK(ui==UI_WIZARD && wizPage==0);
  snap("01_wizard_welcome");
  tap(250,150); CHECK(wizPage==1); snap("02_wizard_bt_waiting");
  tap(265,150); CHECK(wizPage==1);            // NEXT disabled until connected
  esp_ble_gatts_cb_param_t p; uint8_t a[6]={0x5A,0x11,0x22,0x33,0x44,0x55}; memcpy(p.connect.remote_bda,a,6);
  g_bonds=1; g_srv->cb->onConnect(g_srv,&p);
  snap("03_wizard_bt_connected");
  tap(265,150); CHECK(wizPage==2); snap("04_wizard_agent_waiting");
  takeOut(); feed("S|host|claude\n"); feed("S|ping\n");
  std::string o=takeOut(); printf("     device said: %s", o.c_str());
  CHECK(o.find("I|fw=2.0")!=std::string::npos && o.find("ble=1")!=std::string::npos);
  snap("05_wizard_agent_linked");
  tap(265,150); CHECK(wizPage==3);
  tap(160,150); CHECK(kbTest==1); snap("06_wizard_test");
  // approval arrives mid-wizard
  feed("S|approve|a1|Bash: git push origin main  (claude wants to run a shell command)\n");
  CHECK(st==ST_APPROVE && ui==UI_NONE); snap("07_approve");
  takeOut(); tap(60,130); o=takeOut();
  CHECK(o.find("E|approve|a1|1")!=std::string::npos);
  run(4000); CHECK(ui==UI_WIZARD && wizPage==3);
  // think/idle must not cover a pending approval; cancel must clear it
  feed("S|approve|a2|Write: /tmp/x\n"); feed("S|think\n"); CHECK(st==ST_THINK && ui==UI_WIZARD);
  feed("S|approve|a3|x\n"); CHECK(st==ST_APPROVE && ui==UI_NONE);
  feed("S|cancel|a3\n"); CHECK(st==ST_THINK && ui==UI_WIZARD);
  tap(265,150); CHECK(cfgSetupDone && ui==UI_NONE);
  run(4000); CHECK(st==ST_IDLE);
  feed("S|think\n"); g_adc_mv=1330; run(3000); snap("08_think_cat_charging");
  feed("S|idle\n"); run(500);
  tap(100,80); CHECK(ui==UI_STATUS); snap("09_status_charging");
  tap(240,150); CHECK(ui==UI_SETTINGS);
  tap(293,40); CHECK(cfgBri==3); snap("10_settings");
  tap(239,108); CHECK(cfgKb==KB_ALWAYS);
  tap(239,108); tap(239,108); CHECK(cfgKb==KB_AUTO);
  tap(160,150); CHECK(ui==UI_CONFIRM); snap("11_confirm_repair");
  tap(240,150); CHECK(ui==UI_WIZARD && wizPage==1 && g_bonds==0 && !bleKbConnected()); snap("12_wizard_after_forget");
  // back on battery, low
  usbOn=false; g_adc_mv=1150; for(int i=0;i<6;i++) run(2100);
  feed("S|wizard\n"); tap(60,150);  // SKIP SETUP -> finish
  run(4000); tap(100,80); snap("13_status_low_battery");
  tap(60,150); run(500); snap("14_idle_low_overlay");
  takeOut(); feed("S|cfg|sleep|30\n"); o=takeOut(); printf("     device said: %s", o.c_str());
  CHECK(SLEEP_OPTIONS[cfgSleep]==30 && Preferences::st["sleep"]==2);
  // screen sleeps on idle, first tap only wakes
  run(31000); CHECK(!screenOn); tap(100,80); CHECK(screenOn && ui==UI_NONE); tap(100,80); CHECK(ui==UI_STATUS);
  printf("\n%s (%d failures)\n", fails?"FAILED":"ALL PASSED", fails);
  return fails?1:0;
}
