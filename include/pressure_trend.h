#pragma once
#include <stdint.h>
#include <math.h>
// Experimental pressure-only outlook, not a calibrated rain probability model.
// No absolute sea-level thresholds: station altitude is unknown.

class PressureTrend {
  struct Point { uint32_t ms; float pressure; } points[181]{};
  int count=0, head=0, bucketCount=0;
  float bucketSum=0, lastPressure=0;
  uint32_t bucketMs=0,lastGoodMs=0;
  bool seen=false;
public:
  void reset(){count=0;head=0;bucketCount=0;bucketSum=0;seen=false;}
  void add(uint32_t now,float value){
    if(!isfinite(value)||value<300||value>1100)return;
    // Gaps and sudden steps can indicate relocation or sensor restart.
    if(seen && (now-lastGoodMs>300000 || fabsf(value-lastPressure)>2.0f))reset();
    if(!seen){bucketMs=now;seen=true;}
    lastGoodMs=now;lastPressure=value;bucketSum+=value;bucketCount++;
    if(now-bucketMs<60000)return;
    if(bucketCount>=10){points[head]={now,bucketSum/bucketCount};head=(head+1)%181;if(count<181)count++;}
    while(count>0 && now-points[(head-count+181)%181].ms>10800000)count--;
    bucketMs=now;bucketSum=0;bucketCount=0;
  }
  int size() const {return count;}
  uint32_t pointMs(int i) const {return points[(head-count+i+181)%181].ms;}
  float pointPressure(int i) const {return points[(head-count+i+181)%181].pressure;}
  // Restored UTC points are mapped to wrapping millis; only a continuous recent tail is usable.
  void restorePoint(uint32_t stamp,float value){
    if(!isfinite(value)||value<300||value>1100)return;
    if(count){auto p=points[(head+180)%181];if(stamp-p.ms>300000||fabsf(value-p.pressure)>2.0f)reset();}
    points[head]={stamp,value};head=(head+1)%181;if(count<181)count++;
    seen=true;lastGoodMs=stamp;lastPressure=value;bucketMs=stamp;bucketCount=0;bucketSum=0;
  }
  struct Result { const char* state; const char* trend; int minutes, samples; float slope,change; bool ready; };
  Result get(uint32_t now,bool valid) const {
    Result r={"unavailable","unknown",0,count,NAN,NAN,false};
    if(!valid||!seen||now-lastGoodMs>120000)return r;
    r.state="warming_up";
    if(count<2)return r;
    int first=(head-count+181)%181;
    const Point &a=points[first],&b=points[(head+180)%181];
    r.minutes=(b.ms-a.ms)/60000;
    if(count<61||r.minutes<60||count<r.minutes*0.8f)return r;
    double sx=0,sy=0,sxx=0,sxy=0;
    for(int i=0;i<count;i++){const auto& p=points[(first+i)%181];double x=(p.ms-a.ms)/3600000.0,y=p.pressure;sx+=x;sy+=y;sxx+=x*x;sxy+=x*y;}
    double denominator=count*sxx-sx*sx;
    if(denominator<=0)return r;
    double slope=(count*sxy-sx*sy)/denominator, intercept=(sy-slope*sx)/count,variance=0;
    for(int i=0;i<count;i++){const auto& p=points[(first+i)%181];double e=p.pressure-(intercept+slope*((p.ms-a.ms)/3600000.0));variance+=e*e;}
    if(sqrt(variance/count)>0.5){r.state="noisy";return r;}
    r.ready=true;r.state="ready";r.slope=slope;r.change=b.pressure-a.pressure;
    r.trend=slope<=-1.0?"falling_fast":slope<=-0.3?"falling":slope>=0.3?"rising":"steady";
    return r;
  }
};
