#pragma once
#include <Update.h>
#include "maintenance_secret.h"
volatile bool otaBusy=false;
bool otaSuccess=false,setupAP=false;
uint32_t disconnectedSince=0,wifiChangeAt=0;
String pendingSSID,pendingPass;
void drawUpdating(size_t done,size_t total) {
  static uint32_t last=0;if(done && millis()-last<90 && done!=total)return;last=millis();
  tft.enableDisplay(true);if(displayBacklightEnabled)ledcWrite(BACKLIGHT_CHANNEL,140);
  frame.setRotation(0);frame.fillScreen(0xFF9D);INK=0x51EB;PEACH=0xF6F9;
  label(8,24,"Mi thay áo",INK);
  float a=millis()/220.0f;
  cloudFace(49+int(5*cos(a)),57+int(4*sin(a)));
  for(int i=0;i<8;i++){float b=a+i*0.7854f;frame.fillCircle(64+int(35*cos(b)),70+int(30*sin(b)),i==0?3:2,i==0?0xA3D5:0xE6DC);}
  frame.fillRoundRect(10,115,108,9,4,0xE6DC);
  int percent=total?min(size_t(100),done*100/total):0;
  if(percent)frame.fillRoundRect(10,115,108*percent/100,9,4,0xA3D5);
  char text[24];snprintf(text,sizeof(text),"%d%%",percent);digits(48,134,text,1);
  presentFrame();
}
bool maintenanceAuth(){
  if(server.authenticate("mi",MAINT_PASSWORD))return true;
  server.requestAuthentication(DIGEST_AUTH,"MayNhaMi");return false;
}
const char maintenancePage[] PROGMEM=R"HTML(<!doctype html><html lang="vi"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Mi thay áo</title><style>body{font:17px system-ui;background:#faf7ff;color:#514660;max-width:560px;margin:30px auto;padding:20px}section{background:#eee3fa;padding:24px;border-radius:28px;margin:20px 0}input,button{box-sizing:border-box;width:100%;padding:14px;border:0;border-radius:14px;margin:8px 0}button{background:#d0ebe2;color:#344f48;font-weight:bold}progress{width:100%}small{line-height:1.6}</style><a href="/">← Nhà của Mi</a><h1>☁ Mi thay áo</h1><p id="firmwareInfo">Đang đọc phiên bản…</p><small>Bản mới: lịch sử 7 ngày, gửi bù cloud, giờ ngủ, hiệu chỉnh theo nhiệt kế.</small><section><h2>Giấc ngủ của Mi</h2><form id="sleep"><label>Giờ ngủ (0–23)</label><input id="sleepStart" type="number" min="0" max="23" required><label>Giờ thức (0–23)</label><input id="sleepEnd" type="number" min="0" max="23" required><label><input id="clockOnly" type="checkbox" style="width:auto">Chỉ hiện đồng hồ vào giờ ngủ</label><small>Độ sáng dùng mức ban đêm đã đặt. Giờ ngủ bằng giờ thức: tắt lịch ngủ.</small><button>Lưu giờ ngủ</button></form></section><section><h2>Wi-Fi nhà mình</h2><small>Nhập tên mạng 2.4 GHz và mật khẩu mới. Nếu kết nối không được trong 45 giây, kết nối mạng MayNhaMi-Setup rồi mở 192.168.4.1/setup. Mật khẩu mạng cứu hộ giống mật khẩu quản trị.</small><form id="wifi"><input id="ssid" placeholder="Tên Wi-Fi" maxlength="32" required><input id="pass" type="password" placeholder="Mật khẩu (để trống nếu mạng mở)" maxlength="63"><button>Lưu và kết nối</button></form></section><section><h2>Áo mới cho Mi</h2><small>Chỉ chọn firmware .bin dành cho Mây nhà Mi. Giữ nguồn điện trong lúc cập nhật. Ảnh và cài đặt được giữ lại.</small><input id="fw" type="file" accept=".bin"><button id="upload">Cập nhật ESP32</button><progress id="progress" value="0" max="100"></progress></section><details><summary>Hiệu chỉnh cảm biến</summary><section><small>Số hiển thị = số gốc + độ bù. Ví dụ 31,7°C so với nhiệt kế 27°C: nhập -4,7°C. Chỉ hiệu chỉnh số đo tại trạm; không đổi dự báo. Giá trị được lưu khi mất điện.</small><p id="rawReadings"></p><label>Nhiệt kế đối chiếu (°C)</label><input id="reference" type="number" min="-40" max="85" step="0.1"><button id="useReference" type="button">Tính độ bù từ nhiệt kế</button><small>Nút này chỉ điền độ bù, nhấn Lưu hiệu chỉnh để áp dụng. Đặt nhiệt kế cạnh cảm biến và chờ số ổn định.</small><form id="cal"><label for="ct">Bù nhiệt độ (°C)</label><input id="ct" type="number" min="-15" max="15" step="0.1" required><label for="ch">Bù độ ẩm (điểm %)</label><input id="ch" type="number" min="-30" max="30" step="0.1" required><button id="calSave" disabled>Lưu hiệu chỉnh</button><button type="button" id="calReset" disabled>Đặt độ bù về 0</button></form></section></details><p id="msg" role="status"></p><script>
const msg=document.getElementById('msg');
let rawTemperature=null;
fetch('/api/status').then(r=>r.json()).then(d=>{document.getElementById('firmwareInfo').textContent='Đang chạy '+d.firmware+' · build '+(d.build||'chưa có');if(d.sensor.ok&&d.sensor.age_s<15){rawTemperature=d.sensor.raw_temperature_c;document.getElementById('rawReadings').textContent='Số gốc: '+rawTemperature.toFixed(1)+'°C · sau bù: '+d.sensor.temperature_c.toFixed(1)+'°C';}});
document.getElementById('useReference').onclick=async()=>{try{const d=await fetch('/api/status').then(r=>r.json());const input=document.getElementById('reference');if(!input.value||!input.checkValidity()||!d.sensor.ok||d.sensor.age_s>=15)throw Error();const delta=Math.round((Number(input.value)-d.sensor.raw_temperature_c)*10)/10;if(Math.abs(delta)>15)throw Error();document.getElementById('ct').value=delta;msg.textContent='Đã tính độ bù '+delta+'°C. Nhấn Lưu hiệu chỉnh để áp dụng.'}catch{msg.textContent='Cần số đo mới và nhiệt độ đối chiếu hợp lệ; độ bù tối đa ±15°C.'}};
fetch('/api/sleep').then(r=>r.json()).then(d=>{document.getElementById('sleepStart').value=d.start;document.getElementById('sleepEnd').value=d.end;document.getElementById('clockOnly').checked=d.clock_only});
document.getElementById('sleep').onsubmit=async e=>{e.preventDefault();try{const r=await fetch('/api/sleep',{method:'POST',headers:{'Content-Type':'application/json','X-Mi-Control':'1'},body:JSON.stringify({start:Number(document.getElementById('sleepStart').value),end:Number(document.getElementById('sleepEnd').value),clock_only:document.getElementById('clockOnly').checked})});if(!r.ok)throw Error();msg.textContent='Đã lưu giờ ngủ.'}catch{msg.textContent='Chưa lưu được giờ ngủ.'}};
let calReady=false;
fetch('/api/calibration').then(r=>{if(!r.ok)throw Error();return r.json()}).then(d=>{document.getElementById('ct').value=d.temperature_offset_c;document.getElementById('ch').value=d.humidity_offset_pct;calReady=true;document.getElementById('calSave').disabled=false;document.getElementById('calReset').disabled=false}).catch(()=>msg.textContent='Chưa đọc được hiệu chỉnh. Tải lại trang để thử lại.');
async function saveCal(t,h){if(!calReady)return;try{const r=await fetch('/api/calibration',{method:'POST',headers:{'Content-Type':'application/json','X-Mi-Control':'1'},body:JSON.stringify({temperature_offset_c:t,humidity_offset_pct:h})});if(!r.ok)throw Error();document.getElementById('ct').value=t;document.getElementById('ch').value=h;msg.textContent='Đã lưu hiệu chỉnh cho màn hình, web và dữ liệu gửi app/cloud.'}catch(e){msg.textContent='Chưa lưu được. Kiểm tra giá trị và kết nối.'}}
document.getElementById('cal').onsubmit=e=>{e.preventDefault();saveCal(Number(document.getElementById('ct').value),Number(document.getElementById('ch').value))};
document.getElementById('calReset').onclick=()=>{if(confirm('Đặt cả hai độ bù về 0?'))saveCal(0,0)};

document.getElementById('wifi').onsubmit=async e=>{e.preventDefault();if(!confirm('Chuyển sang Wi-Fi mới? Trang có thể mất kết nối.'))return;try{let r=await fetch('/api/wifi',{method:'POST',headers:{'Content-Type':'application/json','X-Mi-Control':'1'},body:JSON.stringify({ssid:document.getElementById('ssid').value,password:document.getElementById('pass').value})});msg.textContent=r.ok?'Đã lưu. Mi đang kết nối mạng mới. Mở http://may-nha-mi.local/ khi đã vào cùng mạng.':'Tên mạng hoặc mật khẩu không hợp lệ.'}catch(e){msg.textContent='Mất kết nối. Kiểm tra mạng của Mây.'}};
document.getElementById('upload').onclick=async()=>{let f=document.getElementById('fw').files[0];if(!f||!f.name.endsWith('.bin')||f.size<100000||f.size>1310720){msg.textContent='Chọn firmware .bin hợp lệ, tối đa 1.25 MiB.';return}const header=new Uint8Array(await f.slice(0,24).arrayBuffer());if(header.length<24||header[0]!==0xe9||header[12]!==0||header[13]!==0){msg.textContent='File không phải app image dành cho ESP32 của Mi.';return}if(!confirm('Cập nhật '+f.name+' ('+Math.round(f.size/1024)+' KiB) và khởi động lại?'))return;let x=new XMLHttpRequest();x.open('POST','/api/firmware?size='+f.size);x.setRequestHeader('X-Mi-Control','1');x.timeout=180000;x.upload.onprogress=e=>{if(e.lengthComputable)document.getElementById('progress').value=e.loaded/e.total*100};x.onload=async()=>{if(x.status!==200){msg.textContent='Không cập nhật được. Firmware cũ vẫn được giữ.';return}msg.textContent='Đã ghi firmware. Đang chờ Mi khởi động lại…';await new Promise(r=>setTimeout(r,6000));for(let i=0;i<20;i++){try{const r=await fetch('/api/status',{signal:AbortSignal.timeout(3000)});const d=await r.json();if(d.device.uptime_s<120){msg.textContent='Mi đã chạy lại: '+d.firmware+'. Tải lại trang để dùng giao diện mới.';return}}catch{}await new Promise(r=>setTimeout(r,2000))}msg.textContent='Đã ghi firmware nhưng chưa xác nhận máy lên mạng. Kiểm tra màn hình và Wi-Fi.'};x.onerror=x.ontimeout=()=>msg.textContent='Kết nối bị gián đoạn. Chờ Mi rồi thử lại.';let d=new FormData();d.append('firmware',f);msg.textContent='Mi đang thay áo…';x.send(d)};
</script></html>)HTML";
void beginSavedWifi(){
  Preferences p;p.begin("mi-network",true);String ssid=p.getString("ssid",WIFI_SSID),pass=p.getString("pass",WIFI_PASSWORD);p.end();WiFi.begin(ssid.c_str(),pass.c_str());
}
void maintenanceTick(){
  if(wifiChangeAt && int32_t(millis()-wifiChangeAt)>=0){wifiChangeAt=0;WiFi.disconnect();WiFi.begin(pendingSSID.c_str(),pendingPass.c_str());pendingPass="";disconnectedSince=millis();}
  if(WiFi.status()==WL_CONNECTED){disconnectedSince=millis();}
  else if(!setupAP && millis()-disconnectedSince>45000){WiFi.mode(WIFI_AP_STA);setupAP=WiFi.softAP("MayNhaMi-Setup",MAINT_PASSWORD);}
}
void drawNetworkHelp(){
  if(!setupAP || WiFi.status()==WL_CONNECTED)return;
  tft.enableDisplay(true);if(displayBacklightEnabled)ledcWrite(BACKLIGHT_CHANNEL,100);
  frame.setRotation(0);frame.fillScreen(0xFF9D);INK=0x51EB;
  label(7,20,"Kết nối Mây",INK);cloudFace(49,28);
  frame.setTextColor(INK);frame.setTextSize(1);frame.setCursor(7,65);frame.println("MayNhaMi-Setup");
  label(7,89,"Mật khẩu:",INK);frame.setCursor(7,98);frame.print(MAINT_PASSWORD);
  frame.setCursor(7,124);frame.print("192.168.4.1");frame.setCursor(7,139);frame.print("/setup");
  presentFrame();
}
void initMaintenance(){
  server.on("/api/sleep",HTTP_GET,[](){if(!maintenanceAuth())return;StaticJsonDocument<128>d;d["start"]=sleepStart;d["end"]=sleepEnd;d["clock_only"]=sleepClockOnly;String body;serializeJson(d,body);server.send(200,"application/json",body);});
  server.on("/api/sleep",HTTP_POST,[](){if(!maintenanceAuth())return;if(server.header("X-Mi-Control")!="1"){server.send(403);return;}StaticJsonDocument<192>d;if(server.arg("plain").length()>160||deserializeJson(d,server.arg("plain"))||!d["start"].is<int>()||!d["end"].is<int>()||!d["clock_only"].is<bool>()){server.send(400);return;}int a=d["start"],b=d["end"];if(a<0||a>23||b<0||b>23){server.send(400);return;}sleepStart=a;sleepEnd=b;sleepClockOnly=d["clock_only"];displayPrefs.putUInt("sleepStart",a);displayPrefs.putUInt("sleepEnd",b);displayPrefs.putBool("clockOnly",sleepClockOnly);applyDisplaySettings();server.send(200,"application/json","{\"ok\":true}");});

  server.on("/api/calibration",HTTP_GET,[](){
    if(!maintenanceAuth())return;
    server.sendHeader("Cache-Control","no-store");
    StaticJsonDocument<128> d;d["temperature_offset_c"]=temperatureOffset;d["humidity_offset_pct"]=humidityOffset;
    String body;serializeJson(d,body);server.send(200,"application/json",body);
  });
  server.on("/api/calibration",HTTP_POST,[](){
    if(!maintenanceAuth())return;
    if(server.header("X-Mi-Control")!="1"){server.send(403);return;}
    StaticJsonDocument<256> d;
    if(server.arg("plain").length()>192||deserializeJson(d,server.arg("plain"))||!d["temperature_offset_c"].is<float>()||!d["humidity_offset_pct"].is<float>()){server.send(400);return;}
    float t=d["temperature_offset_c"],h=d["humidity_offset_pct"];
    if(!isfinite(t)||!isfinite(h)||fabsf(t)>15||fabsf(h)>30){server.send(400);return;}
    if(!saveCalibration(t,h)){server.send(500);return;}
    server.send(200,"application/json","{\"ok\":true}");
  });
  server.on("/setup",HTTP_GET,[](){if(!maintenanceAuth())return;server.sendHeader("Cache-Control","no-store");server.send_P(200,"text/html; charset=utf-8",maintenancePage);});
  server.on("/api/wifi",HTTP_POST,[](){
    if(!maintenanceAuth())return;if(server.header("X-Mi-Control")!="1"){server.send(403);return;}
    StaticJsonDocument<256> d;if(server.arg("plain").length()>240||deserializeJson(d,server.arg("plain"))||!d["ssid"].is<const char*>()||!d["password"].is<const char*>()){server.send(400);return;}
    String s=d["ssid"],p=d["password"];if(s.length()<1||s.length()>32||(p.length()!=0&&(p.length()<8||p.length()>63))){server.send(400);return;}
    Preferences prefs;prefs.begin("mi-network",false);prefs.putString("ssid",s);prefs.putString("pass",p);prefs.end();pendingSSID=s;pendingPass=p;wifiChangeAt=millis()+1500;server.send(200,"application/json","{\"ok\":true}");
  });
  server.on("/api/firmware",HTTP_POST,[](){
    if(!maintenanceAuth())return;if(server.header("X-Mi-Control")!="1"){server.send(403);return;}
    server.send(otaSuccess?200:400,"text/plain",otaSuccess?"OK":"Invalid firmware");
    if(otaSuccess)restartAt=millis()+1500;else{otaBusy=false;applyDisplaySettings();tft.enableDisplay(displayOn);}
  },[](){
    auto& u=server.upload();
    if(u.status==UPLOAD_FILE_START){otaSuccess=false;otaBusy=false;if(!server.authenticate("mi",MAINT_PASSWORD)||server.header("X-Mi-Control")!="1")return;
      size_t size=server.arg("size").toInt();if(size<100000||size>ESP.getFreeSketchSpace())return;
      otaBusy=Update.begin(size,U_FLASH);if(otaBusy)drawUpdating(0,size);
    }else if(u.status==UPLOAD_FILE_WRITE && otaBusy){if(Update.write(u.buf,u.currentSize)!=u.currentSize){Update.abort();otaBusy=false;}else drawUpdating(Update.progress(),Update.size());}
    else if(u.status==UPLOAD_FILE_END && otaBusy){otaSuccess=Update.end(false);if(!otaSuccess)otaBusy=false;}
    else if(u.status==UPLOAD_FILE_ABORTED){Update.abort();otaBusy=false;applyDisplaySettings();tft.enableDisplay(displayOn);}
  });
}
