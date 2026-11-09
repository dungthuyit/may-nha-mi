#pragma once
#include "pressure_trend.h"
#include <string.h>
inline bool trendSelfTest(){
  PressureTrend t;
  uint32_t now=0;
  for(int i=0;i<=3600;i++){now=i*3000;t.add(now,1000.0f-i*3000/3600000.0f);}
  auto r=t.get(now,true);
  if(!r.ready || strcmp(r.trend,"falling_fast") || fabs(r.slope+1)>0.01)return false;
  if(t.get(now,false).ready || t.get(now+121000,true).ready)return false;
  t.add(now+400000,997);if(t.get(now+400000,true).ready)return false;
  t.reset();
  for(int i=0;i<=1400;i++){now=uint32_t(0xFFF00000)+i*3000;t.add(now,1000.0f);}
  r=t.get(now,true);if(!r.ready || strcmp(r.trend,"steady"))return false;
  t.add(now+3000,1005);if(t.get(now+3000,true).ready)return false;
  t.reset();
  for(int i=0;i<=100;i++)t.add(i*3000,1000);
  if(t.get(300000,true).ready)return false;
  t.reset();
  // Restore a one-hour history across reboot (negative millis wraps correctly).
  for(int i=0;i<=65;i++)t.restorePoint(uint32_t(10000-(65-i)*60000),1008.0f-i/120.0f);
  t.add(40000,1008.0f-65/120.0f);
  r=t.get(40000,true);if(!r.ready||strcmp(r.trend,"falling"))return false;
  t.add(400000,1007.5f);if(t.get(400000,true).ready)return false;
  return true;
}
