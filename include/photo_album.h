#pragma once
#include <LittleFS.h>
constexpr size_t PHOTO_BYTES=128*160*2;
bool albumReady=false;
int photoSlots[4]{},photoCount=0;
String photoPath(int slot){return String("/photo")+slot+".rgb";}
void scanPhotos(){photoCount=0;if(!albumReady)return;for(int i=0;i<4;i++){File f=LittleFS.open(photoPath(i),"r");if(f&&f.size()==PHOTO_BYTES)photoSlots[photoCount++]=i;}}
void initPhotos(){
  Preferences prefs;prefs.begin("mi-album",false);
  albumReady=LittleFS.begin(false);
  if(!albumReady&&!prefs.getBool("initialized",false)){if(LittleFS.format())albumReady=LittleFS.begin(false);}
  if(albumReady)prefs.putBool("initialized",true);
  prefs.end();scanPhotos();
}
bool readPhoto(int index,uint16_t* pixels){if(index<0||index>=photoCount)return false;File f=LittleFS.open(photoPath(photoSlots[index]),"r");return f&&f.read((uint8_t*)pixels,PHOTO_BYTES)==PHOTO_BYTES;}
