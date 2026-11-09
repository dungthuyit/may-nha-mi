#pragma once
#include <LittleFS.h>
constexpr uint32_t HISTORY_CAP=2016;
struct HistoryPoint {uint32_t seq,epoch;float t,h,p;uint32_t crc;};
uint32_t historySeq=0,historyAck=0,historyLastEpoch=0;
bool historyReady=false;
uint32_t historyCRC(const HistoryPoint& p){const uint8_t* b=(const uint8_t*)&p;uint32_t h=2166136261u;for(size_t i=0;i<sizeof(p)-4;i++)h=(h^b[i])*16777619u;return h;}
bool readHistoryPoint(File& f,uint32_t seq,HistoryPoint& p){
 if(!seq||!f.seek(((seq-1)%HISTORY_CAP)*sizeof(p)))return false;
 return f.read((uint8_t*)&p,sizeof(p))==sizeof(p)&&p.seq==seq&&p.crc==historyCRC(p);
}
void initHistory(){
 if(!albumReady)return;
 File f=LittleFS.open("/history.bin","r");HistoryPoint p{};
 if(f){while(f.read((uint8_t*)&p,sizeof(p))==sizeof(p)){if(p.seq&&p.crc==historyCRC(p)&&p.seq>historySeq){historySeq=p.seq;historyLastEpoch=p.epoch;}delay(1);}f.close();}
 else{f=LittleFS.open("/history.bin","w");if(!f)return;f.close();}
 Preferences prefs;prefs.begin("mi-history",true);historyAck=min(prefs.getUInt("ack",0),historySeq);prefs.end();historyReady=true;
}
void sampleHistory(bool valid,float t,float h,bool pressureOK,float pressure){
 uint32_t epoch=time(nullptr);if(!historyReady||!valid||epoch<1700000000||epoch<=historyLastEpoch||epoch-historyLastEpoch<300)return;
 HistoryPoint p{historySeq+1,epoch,t,h,pressureOK?pressure:NAN,0};p.crc=historyCRC(p);
 File f=LittleFS.open("/history.bin","r+");if(!f||!f.seek(((p.seq-1)%HISTORY_CAP)*sizeof(p)))return;
 if(f.write((uint8_t*)&p,sizeof(p))==sizeof(p)){f.flush();historySeq=p.seq;historyLastEpoch=epoch;}f.close();
}
void acknowledgeHistory(uint32_t seq){seq=min(seq,historySeq);if(seq<=historyAck)return;Preferences p;if(p.begin("mi-history",false)){if(p.putUInt("ack",seq)==4)historyAck=seq;p.end();}}
uint32_t historyBacklog(){return min(historySeq-historyAck,HISTORY_CAP);}
uint32_t appendHistory(JsonArray a){
 File f=LittleFS.open("/history.bin","r");HistoryPoint p{};uint32_t last=historyAck;
 uint32_t first=max(historyAck+1,historySeq>HISTORY_CAP?historySeq-HISTORY_CAP+1:1u);
 for(uint32_t seq=first;seq<=historySeq && a.size()<32;seq++)if(readHistoryPoint(f,seq,p)){JsonArray r=a.createNestedArray();r.add(p.epoch);r.add(p.t);r.add(p.h);if(isfinite(p.p))r.add(p.p);else r.add(nullptr);last=seq;}
 return last;
}
void sendHistory(){
 uint32_t hours=server.arg("hours")=="168"?168:24,now=time(nullptr);File f=LittleFS.open("/history.bin","r");
 server.sendHeader("Cache-Control","no-store");server.setContentLength(CONTENT_LENGTH_UNKNOWN);server.send(200,"application/json","");server.sendContent("{\"interval_s\":300,\"points\":[");
 bool comma=false;HistoryPoint p{};uint32_t first=historySeq>HISTORY_CAP?historySeq-HISTORY_CAP+1:1;
 for(uint32_t seq=first;seq<=historySeq;seq++){if(readHistoryPoint(f,seq,p)&&p.epoch<=now&&now-p.epoch<=hours*3600){char row[120],pressure[24];if(isfinite(p.p))snprintf(pressure,sizeof(pressure),"%.2f",p.p);else strcpy(pressure,"null");snprintf(row,sizeof(row),"%s[%lu,%.2f,%.2f,%s]",comma?",":"",(unsigned long)p.epoch,p.t,p.h,pressure);server.sendContent(row);comma=true;}delay(1);}
 server.sendContent("]}");server.sendContent("");
}
