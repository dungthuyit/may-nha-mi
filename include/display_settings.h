#pragma once
#include <Preferences.h>
constexpr int BACKLIGHT_PIN=32, BACKLIGHT_CHANNEL=7;
int displayBrightness=80, displayNightBrightness=20;
String displayMode="auto";
bool displayOn=true;
bool displayDark=false, displayBacklightEnabled=false, displayPwmAttached=false;
int displayEffectiveBrightness=80;
Preferences displayPrefs;
String displayPalette="pastel",displayClock="digital";
bool validPalette(const String& v){return v=="pastel"||v=="mint"||v=="rose"||v=="blue";}
bool validClock(const String& v){return v=="digital"||v=="analog";}
void saveDisplayStyle(const String& palette,const String& clock){
  if(palette!=displayPalette){displayPalette=palette;displayPrefs.putString("palette",palette);}
  if(clock!=displayClock){displayClock=clock;displayPrefs.putString("clock",clock);}
}
int displayRotation=0;
void saveDisplayRotation(int degrees){if(displayRotation!=degrees){displayRotation=degrees;displayPrefs.putUInt("rotation",degrees);}}
int slideSeconds=10;
String slideOrder="012345";
bool validSlideOrder(const String& order){
  if(order.length()<1||order.length()>6)return false;
  unsigned seen=0;
  for(unsigned i=0;i<order.length();i++){char c=order[i];if(c<'0'||c>'5'||(seen&(1u<<(c-'0'))))return false;seen|=1u<<(c-'0');}
  return true;
}
void saveSlideshow(int seconds,const String& order){
  if(seconds!=slideSeconds){slideSeconds=seconds;displayPrefs.putUInt("slideSeconds",seconds);}
  if(order!=slideOrder){slideOrder=order;displayPrefs.putString("slideOrder",order);}
}
int sleepStart=22,sleepEnd=6;bool sleepClockOnly=true;
inline bool nightHour(int hour){return sleepStart==sleepEnd?false:(sleepStart<sleepEnd?(hour>=sleepStart&&hour<sleepEnd):(hour>=sleepStart||hour<sleepEnd));}
bool scheduledSleep(){time_t instant=time(nullptr);struct tm local{};localtime_r(&instant,&local);return instant>1700000000&&nightHour(local.tm_hour);}

bool displayNightNow(){
  if(sleepClockOnly&&scheduledSleep())return true;
  if(displayMode=="dark")return true;
  if(displayMode=="light")return false;
  time_t instant=time(nullptr);struct tm local{};localtime_r(&instant,&local);
  return instant>1700000000 && nightHour(local.tm_hour);
}
void applyDisplaySettings(){
  displayDark=displayNightNow();
  displayEffectiveBrightness=displayOn?(displayDark?displayNightBrightness:displayBrightness):0;
  if(!displayBacklightEnabled){
    if(displayPwmAttached){ledcDetachPin(BACKLIGHT_PIN);displayPwmAttached=false;}
    pinMode(BACKLIGHT_PIN,INPUT);return;
  }
  if(!displayPwmAttached){ledcSetup(BACKLIGHT_CHANNEL,5000,8);ledcAttachPin(BACKLIGHT_PIN,BACKLIGHT_CHANNEL);displayPwmAttached=true;}
  ledcWrite(BACKLIGHT_CHANNEL,(displayEffectiveBrightness*255+50)/100);
}
void initDisplaySettings(){
  displayPrefs.begin("mi-display",false);
  displayPalette=displayPrefs.getString("palette","pastel");if(!validPalette(displayPalette))displayPalette="pastel";
  displayClock=displayPrefs.getString("clock","digital");if(!validClock(displayClock))displayClock="digital";
  displayRotation=displayPrefs.getUInt("rotation",0);if(displayRotation!=0&&displayRotation!=90&&displayRotation!=180&&displayRotation!=270)displayRotation=0;
  slideSeconds=constrain(displayPrefs.getUInt("slideSeconds",10),5u,60u);
  slideOrder=displayPrefs.getString("slideOrder","012345");if(!validSlideOrder(slideOrder))slideOrder="012345";
  displayBrightness=constrain(displayPrefs.getUInt("day",80),0u,100u);
  displayNightBrightness=constrain(displayPrefs.getUInt("night",20),0u,100u);
  displayMode=displayPrefs.getString("mode","auto");
  if(displayMode!="auto"&&displayMode!="light"&&displayMode!="dark")displayMode="auto";
  sleepStart=constrain(displayPrefs.getUInt("sleepStart",22),0u,23u);sleepEnd=constrain(displayPrefs.getUInt("sleepEnd",6),0u,23u);sleepClockOnly=displayPrefs.getBool("clockOnly",true);
  displayOn=displayPrefs.getBool("on",true);
  displayBacklightEnabled=displayPrefs.getBool("wired",false);
  applyDisplaySettings();
}
void saveDisplaySettings(int day,int night,const String& mode,bool wired){
  if(wired!=displayBacklightEnabled){displayPrefs.putBool("wired",wired);displayBacklightEnabled=wired;}
  if(day!=displayBrightness){displayPrefs.putUInt("day",day);displayBrightness=day;}
  if(night!=displayNightBrightness){displayPrefs.putUInt("night",night);displayNightBrightness=night;}
  if(mode!=displayMode){displayPrefs.putString("mode",mode);displayMode=mode;}
  applyDisplaySettings();
}

void setDisplayPower(bool on){if(on!=displayOn){displayOn=on;displayPrefs.putBool("on",on);}applyDisplaySettings();}
