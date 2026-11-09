#pragma once
#include <Adafruit_ST7735.h>
#include <U8g2_for_Adafruit_GFX.h>
#include "lunar_vn.h"
#include "display_settings.h"
#include "photo_album.h"

// TFT wiring: SCL18, SDA23, CS27, DC25, RST26. VDD 3V3, BLK GPIO32 (no VDD bridge).
Adafruit_ST7735 tft(27,25,26);
GFXcanvas16 frame(128,160);
// Reuse the same framebuffer for native landscape; photos retain their aspect ratio.
void presentFrame(){
  if(!frame.getBuffer())return;
  static int applied=-1;
  if(applied!=displayRotation){tft.setRotation(displayRotation/90);tft.fillScreen(ST77XX_BLACK);applied=displayRotation;}
  if(displayRotation%180==0){tft.drawRGBBitmap(0,0,frame.getBuffer(),128,160);return;}
  uint16_t line[160];const uint16_t* pixels=frame.getBuffer();
  if(frame.getRotation()==1){
    for(int y=0;y<128;y++){
      for(int x=0;x<160;x++)line[x]=pixels[x*128+127-y];
      tft.drawRGBBitmap(0,y,line,160,1);
    }
    return;
  }
  for(int y=0;y<128;y++){
    for(int x=0;x<102;x++)line[x]=pixels[(y*160/128)*128+x*128/102];
    tft.drawRGBBitmap(29,y,line,102,1);
  }
}
U8G2_FOR_ADAFRUIT_GFX vnFont;
uint16_t PAPER=0xFF9D, INK=0x51EB, LILAC=0xE6DC, PEACH=0xF6F9, MINT=0xD79B, YELLOW=0xFF13;
void label(int x,int baseline,const char* text,uint16_t color=INK) {
  vnFont.setForegroundColor(color);vnFont.setCursor(x,baseline);vnFont.print(text);
}
void digits(int x,int y,const char* text,int size=2) {
  frame.setTextSize(size);frame.setTextColor(INK);frame.setCursor(x,y);frame.print(text);
}
void cloudFace(int x,int y) {
  frame.fillCircle(x+6,y+7,6,displayDark?0x528D:0xFFFF);frame.fillCircle(x+13,y+4,8,displayDark?0x528D:0xFFFF);
  frame.fillCircle(x+21,y+8,6,displayDark?0x528D:0xFFFF);frame.fillRoundRect(x+2,y+7,24,12,5,displayDark?0x528D:0xFFFF);
  frame.fillCircle(x+10,y+11,1,INK);frame.fillCircle(x+18,y+11,1,INK);
  frame.drawPixel(x+13,y+14,INK);frame.drawPixel(x+14,y+15,INK);frame.drawPixel(x+15,y+14,INK);
  frame.drawFastHLine(x+6,y+14,3,PEACH);frame.drawFastHLine(x+20,y+14,3,PEACH);
}
void initDisplay() {
  initPhotos();
  initDisplaySettings();
  SPI.begin(18,-1,23,27);
  tft.initR(INITR_BLACKTAB);tft.setRotation(0);tft.setSPISpeed(16000000);tft.fillScreen(PAPER);
  vnFont.begin(frame);vnFont.setFont(u8g2_font_unifont_t_vietnamese2);vnFont.setFontMode(1);
}
int screenPage=-1;
int slideshowPage(uint32_t now){
  int pages[6],count=0;
  for(unsigned i=0;i<slideOrder.length();i++){
    int p=slideOrder[i]-'0';
    if(p<2)pages[count++]=p;
    else for(int j=0;j<photoCount;j++)if(photoSlots[j]==p-2){pages[count++]=j+2;break;}
  }
  return count?pages[(now/(slideSeconds*1000u))%count]:0;
}
void applyPalette(){
  if(displayPalette=="pastel")return;
  if(displayDark){
    if(displayPalette=="mint"){PAPER=0x0924;LILAC=0x1A09;PEACH=0x224A;MINT=0x19C8;}
    else if(displayPalette=="rose"){PAPER=0x2085;LILAC=0x414A;PEACH=0x498B;MINT=0x3949;}
    else {PAPER=0x08C5;LILAC=0x194B;PEACH=0x21CD;MINT=0x19AA;}
  }else{
    if(displayPalette=="mint"){PAPER=0xF7FD;LILAC=0xC735;PEACH=0xD7B9;MINT=0xB6D4;INK=0x2269;}
    else if(displayPalette=="rose"){PAPER=0xFF9D;LILAC=0xF67B;PEACH=0xFEDC;MINT=0xEDB8;INK=0x69EB;}
    else {PAPER=0xF7DF;LILAC=0xC6FD;PEACH=0xD77F;MINT=0xB65C;INK=0x224D;}
  }
}
void analogClock(bool landscape){
  time_t instant=time(nullptr);struct tm local{};localtime_r(&instant,&local);
  if(instant<1700000000){label(15,80,"Chờ giờ mạng");return;}
  int cx=landscape?80:64,cy=landscape?53:69,r=landscape?48:34;
  frame.fillCircle(cx,cy,r,displayDark?0x18C5:0xFFDF);frame.drawCircle(cx,cy,r,LILAC);
  for(int i=0;i<60;i++){
    float a=i*0.10471976f;int inner=r-(i%5==0?5:2);
    frame.drawLine(cx+int(inner*sin(a)),cy-int(inner*cos(a)),cx+int((r-1)*sin(a)),cy-int((r-1)*cos(a)),i%5==0?INK:LILAC);
  }
  for(int i=1;i<=12;i++){
    float a=i*0.52359878f;char number[3];snprintf(number,sizeof(number),"%d",i);
    digits(cx+int((r-11)*sin(a))-int(strlen(number))*3,cy-int((r-11)*cos(a))-4,number,1);
  }
  float seconds=local.tm_sec,minutes=local.tm_min+seconds/60.0f,hours=(local.tm_hour%12)+minutes/60.0f;
  auto hand=[&](float a,int length,uint16_t color,bool thick){int x=cx+int(length*sin(a)),y=cy-int(length*cos(a));frame.drawLine(cx,cy,x,y,color);if(thick)frame.drawLine(cx+1,cy,x+1,y,color);};
  hand(hours*0.52359878f,int(r*.48f),INK,true);hand(minutes*0.10471976f,int(r*.70f),INK,true);
  hand(seconds*0.10471976f,int(r*.77f),displayDark?0xFD14:0xB248,false);frame.fillCircle(cx,cy,3,INK);
  static const char* days[]={"Chủ nhật","Thứ hai","Thứ ba","Thứ tư","Thứ năm","Thứ sáu","Thứ bảy"};
  char text[32];auto lunar=vietnameseLunar(local.tm_mday,local.tm_mon+1,local.tm_year+1900);
  if(landscape){
    frame.fillRoundRect(3,106,154,19,6,MINT);
    const char* shortDays[]={"CN","T2","T3","T4","T5","T6","T7"};
    if(lunar.valid)snprintf(text,sizeof(text),"%s %02d/%02d | %s %02d/%02d",shortDays[local.tm_wday],local.tm_mday,local.tm_mon+1,lunar.leap?"Â*":"Âm",lunar.day,lunar.month);
    else snprintf(text,sizeof(text),"%s %02d/%02d",shortDays[local.tm_wday],local.tm_mday,local.tm_mon+1);
    label(4,120,text);
  }else{
    label(34,120,days[local.tm_wday]);strftime(text,sizeof(text),"%d/%m/%Y",&local);digits(34,127,text,1);
    if(lunar.valid)snprintf(text,sizeof(text),"%s %02d/%02d/%04d",lunar.leap?"Âm*":"Âm",lunar.day,lunar.month,lunar.year);else strcpy(text,"Chờ âm lịch");label(7,150,text);
  }
}
void drawLandscape(int page,bool sensorOk,float temperature,float humidity,bool pressureOk,float pressure,uint32_t now){
  frame.fillScreen(PAPER);
  if(page==1&&displayClock=="analog"){analogClock(true);return;}
  frame.fillRoundRect(3,3,154,23,8,LILAC);
  label(10,20,"Mây nhà Mi");cloudFace(126,5);
  char text[40];
  if(page==0){
    frame.fillRoundRect(3,31,74,68,10,PEACH);
    frame.fillRoundRect(83,31,74,68,10,MINT);
    label(8,48,"Nhiệt độ");label(94,48,"Độ ẩm");
    if(sensorOk)snprintf(text,sizeof(text),"%.1f",temperature);else strcpy(text,"--.-");
    digits(10,57,text,strlen(text)>5?1:2);label(29,92,"°C");
    if(sensorOk)snprintf(text,sizeof(text),"%.1f",humidity);else strcpy(text,"--.-");
    digits(90,57,text,strlen(text)>5?1:2);label(116,92,"%");
    if(pressureOk)snprintf(text,sizeof(text),"%.0f hPa",pressure);else strcpy(text,"Chờ cảm biến");
    label(40,116,text);
  }else if(displayClock=="analog"){analogClock(true);
  }else{
    time_t instant=time(nullptr);struct tm local{};localtime_r(&instant,&local);
    if(instant<1700000000){label(31,67,"Chờ giờ mạng");label(31,94,"Kết nối WiFi");}
    else{
      strftime(text,sizeof(text),local.tm_sec%2?"%H %M":"%H:%M",&local);digits(22,32,text,4);
      frame.fillRoundRect(20,64,120,3,1,LILAC);
      frame.fillRoundRect(20,64,max(2,120*local.tm_sec/60),3,1,MINT);
      static const char* days[]={"Chủ nhật","Thứ hai","Thứ ba","Thứ tư","Thứ năm","Thứ sáu","Thứ bảy"};
      label(5,83,days[local.tm_wday]);
      strftime(text,sizeof(text),"%d/%m/%Y",&local);digits(94,75,text,1);
      frame.fillRoundRect(3,89,154,30,8,LILAC);
      auto lunar=vietnameseLunar(local.tm_mday,local.tm_mon+1,local.tm_year+1900);
      label(9,109,lunar.leap?"Âm*":"Âm");
      if(lunar.valid)snprintf(text,sizeof(text),"%02d/%02d/%04d",lunar.day,lunar.month,lunar.year);else strcpy(text,"Chờ ngày");
      label(50,109,text);
    }
  }
  for(int i=0;i<2;i++)frame.fillCircle(75+i*10,124,2,i==page?INK:LILAC);
  frame.fillCircle(152,124,2,WiFi.status()==WL_CONNECTED?0x5C72:0xD326);
}
void updateDisplay(bool sensorOk,float temperature,float humidity,bool pressureOk,float pressure) {
  static uint32_t lastPaint=0;
  static bool panelOn=true;
  if(panelOn!=displayOn){tft.enableDisplay(displayOn);panelOn=displayOn;lastPaint=0;}
  if(!displayOn)return;
  uint32_t now=millis();int page=(sleepClockOnly&&scheduledSleep())?1:slideshowPage(now);
  if(page==screenPage && now-lastPaint<(page==1?250:1000))return;
  if(!frame.getBuffer())return;
  if(page!=screenPage)Serial.printf("[Display] page=%s\n",page?"calendar":"sensors");
  screenPage=page;lastPaint=now;
  applyDisplaySettings();
  PAPER=displayDark?0x1083:0xFF9D;INK=displayDark?0xE73D:0x51EB;
  LILAC=displayDark?0x3108:0xE6DC;PEACH=displayDark?0x3907:0xF6F9;
  MINT=displayDark?0x1948:0xD79B;YELLOW=displayDark?0x3984:0xFF13;
  applyPalette();
  frame.setRotation(0);
  if(page>=2){if(readPhoto(page-2,frame.getBuffer()))presentFrame();return;}
  if(displayRotation%180!=0){
    frame.setRotation(1);drawLandscape(page,sensorOk,temperature,humidity,pressureOk,pressure,now);presentFrame();return;
  }
  frame.fillScreen(PAPER);
  frame.fillRoundRect(3,3,122,24,9,LILAC);
  label(9,21,"Mây nhà Mi");cloudFace(96,6);
  char buf[40];
  if(page==0) {
    frame.fillRoundRect(5,32,118,48,10,PEACH);
    label(13,49,"Nhiệt độ");
    if(sensorOk)snprintf(buf,sizeof(buf),"%.1f",temperature);else strcpy(buf,"--.-");
    digits(13,57,buf);label(79,71,"°C");
    frame.fillRoundRect(5,85,118,48,10,MINT);
    label(13,102,"Độ ẩm");
    if(sensorOk)snprintf(buf,sizeof(buf),"%.1f",humidity);else strcpy(buf,"--.-");
    digits(13,110,buf);label(86,124,"%");
    if(pressureOk)snprintf(buf,sizeof(buf),"%.0f hPa",pressure);else strcpy(buf,"Chờ cảm biến");
    label(8,149,buf);
  } else if(displayClock=="analog"){analogClock(false);
  } else {
    time_t instant=time(nullptr);struct tm local{};localtime_r(&instant,&local);
    if(instant<1700000000) {
      cloudFace(49,44);label(12,91,"Chờ giờ mạng");label(15,113,"Kết nối WiFi");
    } else {
      strftime(buf,sizeof(buf),local.tm_sec%2?"%H %M":"%H:%M",&local);digits(19,37,buf,3);
      frame.fillRoundRect(10,63,108,3,1,LILAC);
      frame.fillRoundRect(10,63,max(2,108*local.tm_sec/60),3,1,MINT);
      frame.fillCircle(108+int(3*sin(now/400.0)),73,2,PEACH);
      static const char* days[]={"Chủ nhật","Thứ hai","Thứ ba","Thứ tư","Thứ năm","Thứ sáu","Thứ bảy"};
      label(8,76,days[local.tm_wday]);
      strftime(buf,sizeof(buf),"%d/%m/%Y",&local);digits(4,83,buf,2);
      frame.fillRoundRect(5,105,118,43,9,LILAC);
      auto lunar=vietnameseLunar(local.tm_mday,local.tm_mon+1,local.tm_year+1900);
      label(13,121,lunar.leap?"Âm lịch nhuận":"Âm lịch");
      if(lunar.valid)snprintf(buf,sizeof(buf),"%02d/%02d/%04d",lunar.day,lunar.month,lunar.year);else strcpy(buf,"Ngoài phạm vi");
      label(13,141,buf);
    }
  }
  for(int i=0;i<2;i++)frame.fillCircle(59+i*10,155,2,i==page?INK:LILAC);
  frame.fillCircle(119,155,2,WiFi.status()==WL_CONNECTED?0x5C72:0xD326);
  presentFrame();
}
