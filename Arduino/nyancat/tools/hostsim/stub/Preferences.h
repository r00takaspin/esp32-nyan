#pragma once
#include "Arduino.h"
#include <map>
struct Preferences{ static std::map<std::string,int> st;
 bool begin(const char*,bool){return true;} void end(){}
 uint8_t getUChar(const char*k,uint8_t d){return st.count(k)?st[k]:d;}
 bool getBool(const char*k,bool d){return st.count(k)?st[k]:d;}
 void putUChar(const char*k,uint8_t v){st[k]=v;} void putBool(const char*k,bool v){st[k]=v;} };
