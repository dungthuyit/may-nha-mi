#pragma once
#include <Preferences.h>
float temperatureOffset=0.0f,humidityOffset=0.0f;
void initCalibration(){
  Preferences p;p.begin("mi-cal",true);
  temperatureOffset=p.getFloat("temp",0.0f);humidityOffset=p.getFloat("hum",0.0f);p.end();
  if(!isfinite(temperatureOffset)||fabsf(temperatureOffset)>15)temperatureOffset=0;
  if(!isfinite(humidityOffset)||fabsf(humidityOffset)>30)humidityOffset=0;
}
bool saveCalibration(float t,float h){
  Preferences p;if(!p.begin("mi-cal",false))return false;
  bool ok=p.putFloat("temp",t)==sizeof(float)&&p.putFloat("hum",h)==sizeof(float);p.end();
  if(ok){temperatureOffset=t;humidityOffset=h;}return ok;
}
