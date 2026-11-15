#pragma once
#include "Arduino.h"
#include "glcdfont.h"
#include <vector>
enum { BEGIN_WRITE=1, WRITE_COMMAND_8, END_WRITE, DELAY, WRITE_C8_D16, WRITE_C8_D8, WRITE_BYTES };
#define GFX_SKIP_OUTPUT_BEGIN -2
struct Arduino_DataBus{ virtual ~Arduino_DataBus(){} void batchOperation(const uint8_t*,size_t){} };
struct Arduino_ESP32SPI: Arduino_DataBus{ Arduino_ESP32SPI(int,int,int,int){} };
class Arduino_GFX : public Print { public:
 int16_t _w,_h; int16_t cx=0,cy=0; uint8_t tsz=1; uint16_t tcol=0xFFFF; uint8_t rot=0;
 std::vector<uint16_t> fb;
 Arduino_GFX(int w,int h):_w(w),_h(h),fb(w*h){}
 virtual bool begin(int=0){return true;}
 void setRotation(int r){rot=r;} uint8_t getRotation(){return rot;} int16_t width(){return _w;} int16_t height(){return _h;}
 void px(int x,int y,uint16_t c){ if(x>=0&&y>=0&&x<_w&&y<_h) fb[y*_w+x]=c; }
 void fillRect(int x,int y,int w,int h,uint16_t c){for(int j=y;j<y+h;j++)for(int i=x;i<x+w;i++)px(i,j,c);}
 void fillScreen(uint16_t c){fillRect(0,0,_w,_h,c);}
 void drawRect(int x,int y,int w,int h,uint16_t c){fillRect(x,y,w,1,c);fillRect(x,y+h-1,w,1,c);fillRect(x,y,1,h,c);fillRect(x+w-1,y,1,h,c);}
 void drawLine(int x0,int y0,int x1,int y1,uint16_t c){int dx=abs(x1-x0),sx=x0<x1?1:-1,dy=-abs(y1-y0),sy=y0<y1?1:-1,e=dx+dy;for(;;){px(x0,y0,c);if(x0==x1&&y0==y1)break;int e2=2*e;if(e2>=dy){e+=dy;x0+=sx;}if(e2<=dx){e+=dx;y0+=sy;}}}
 void fillRoundRect(int x,int y,int w,int h,int r,uint16_t c){for(int j=0;j<h;j++)for(int i=0;i<w;i++){int ddx=i<r?r-i:(i>=w-r?i-(w-r-1):0),ddy=j<r?r-j:(j>=h-r?j-(h-r-1):0);if(ddx*ddx+ddy*ddy<=r*r)px(x+i,y+j,c);}}
 void setCursor(int x,int y){cx=x;cy=y;} void setTextSize(int s){tsz=s;} void setTextColor(uint16_t c){tcol=c;}
 size_t write(uint8_t ch)override{ if(ch=='\n'){cx=0;cy+=8*tsz;return 1;} for(int i=0;i<5;i++){uint8_t l=font[ch*5+i];for(int j=0;j<8;j++)if(l>>j&1)fillRect(cx+i*tsz,cy+j*tsz,tsz,tsz,tcol);} cx+=6*tsz; return 1;}
};
struct Arduino_ST7789: Arduino_GFX{ Arduino_ST7789(Arduino_DataBus*,int,int,bool,int w,int h,int,int,int,int):Arduino_GFX(h,w){} };
struct Arduino_Canvas: Arduino_GFX{ Arduino_GFX*o; Arduino_Canvas(int w,int h,Arduino_GFX*g,int,int,int):Arduino_GFX(w,h),o(g){}
 uint16_t*getFramebuffer(){return fb.data();} int flushes=0; void flush(){flushes++;} };
