#pragma once
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <deque>
typedef bool boolean;
extern uint32_t g_ms;
inline uint32_t millis(){return g_ms;}
inline void delay(uint32_t d){g_ms+=d;}
#define ESP_ARDUINO_VERSION_VAL(a,b,c) ((a)*10000+(b)*100+(c))
#define ESP_ARDUINO_VERSION ESP_ARDUINO_VERSION_VAL(2,0,14)
#define OUTPUT 1
#define LOW 0
#define HIGH 1
#define ADC_11db 3
inline void pinMode(int,int){} inline void digitalWrite(int,int){}
inline void ledcSetup(int,int,int){} inline void ledcAttachPin(int,int){}
extern int g_bl; inline void ledcWrite(int,int v){g_bl=v;}
extern int g_adc_mv; inline uint32_t analogReadMilliVolts(int){return g_adc_mv;}
inline void analogReadResolution(int){} inline void analogSetPinAttenuation(int,int){}
class String {
 public: std::string s;
 String(){} String(const char*c):s(c?c:""){} String(const std::string&x):s(x){}
 unsigned length()const{return s.size();}
 char operator[](unsigned i)const{return s[i];}
 String substring(unsigned a,unsigned b)const{return String(s.substr(a,b-a));}
 const char*c_str()const{return s.c_str();}
 bool operator==(const String&o)const{return s==o.s;}
 String& operator+=(char c){s+=c;return *this;}
 String& operator=(const char*c){s=c;return *this;}
 long toInt()const{return atol(s.c_str());}
};
inline bool operator==(const String&a,const char*b){return a.s==b;}
inline bool operator!=(const String&a,const char*b){return a.s!=b;}
class Print { public:
 virtual size_t write(uint8_t)=0;
 size_t print(const char*s){size_t n=0;while(*s){write((uint8_t)*s++);n++;}return n;}
 size_t println(const char*s){size_t n=print(s);write('\n');return n+1;}
 virtual ~Print(){}
};
struct SerialStub : public Print {
 std::deque<char> in; std::string out;
 void begin(int){} void setTxTimeoutMs(int){} void flush(){}
 int available(){return in.size();} int availableForWrite(){return 1000;}
 int read(){char c=in.front();in.pop_front();return c;}
 size_t write(uint8_t c)override{out+=(char)c;return 1;}
 void feed(const char*s){while(*s)in.push_back(*s++);}
};
extern SerialStub Serial;
struct EspStub{ uint64_t getEfuseMac(){return 0x2A3F11223344ULL;} void restart(){} };
extern EspStub ESP;
