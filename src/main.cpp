#include <Arduino.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_SHT4x.h>
#include <Adafruit_BMP280.h>
#include "display_ui.h"
#include "pressure_trend.h"
#include "trend_selftest.h"
PressureTrend pressureTrend;
#include <time.h>
#include "secrets.h"
#if __has_include("cloud_secrets.h")
#include "cloud_secrets.h"
#else
constexpr char CLOUD_URL[] = "";
constexpr char CLOUD_INGEST_KEY[] = "";
constexpr char CLOUD_CA_PEM[] = "";
#endif

extern const char page[] asm("_binary_web_index_html_start");
extern const char rootCA[] asm("_binary_include_isrgrootx1_pem_start");
constexpr uint32_t SENSOR_INTERVAL = 3000;
constexpr uint32_t WEATHER_INTERVAL = 900000;
constexpr uint32_t RETRY_INTERVAL = 60000;
#include "sensor_calibration.h"
constexpr char WEATHER_URL[] =
  "https://api.open-meteo.com/v1/forecast?latitude=21.0285&longitude=105.8542"
  "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m,precipitation"
  "&temperature_unit=celsius&wind_speed_unit=kmh&precipitation_unit=mm"
  "&hourly=temperature_2m,precipitation_probability,weather_code"
  "&daily=temperature_2m_max,temperature_2m_min,precipitation_probability_max,weather_code"
  "&forecast_days=3"
  "&timezone=Asia%2FBangkok";

WebServer server(80);
uint32_t restartAt=0;
TaskHandle_t weatherTaskHandle=nullptr;
const char* resetReasonName(){
  switch(esp_reset_reason()){
    case ESP_RST_POWERON:return "power_on";case ESP_RST_EXT:return "external_reset";
    case ESP_RST_SW:return "software_restart";case ESP_RST_PANIC:return "panic";
    case ESP_RST_INT_WDT:return "interrupt_watchdog";case ESP_RST_TASK_WDT:return "task_watchdog";
    case ESP_RST_WDT:return "watchdog";case ESP_RST_BROWNOUT:return "brownout";
    case ESP_RST_DEEPSLEEP:return "deep_sleep";default:return "unknown";
  }
}
#include "station_history.h"
#include "maintenance.h"
Adafruit_SHT4x sht;
Adafruit_BMP280 bmp;
bool shtReady=false,bmpReady=false,pressureValid=false;
float pressureHpa=NAN;
uint8_t bmpAddress=0;
bool pressurePending=false;
uint32_t pressureRequestedMs=0;
bool bmpWrite(uint8_t reg,uint8_t value) {
  Wire.beginTransmission(bmpAddress);Wire.write(reg);Wire.write(value);return Wire.endTransmission()==0;
}
bool bmpRead(uint8_t reg,uint8_t* out,uint8_t count) {
  Wire.beginTransmission(bmpAddress);Wire.write(reg);
  if(Wire.endTransmission(true)!=0 || Wire.requestFrom(bmpAddress,count)!=count)return false;
  for(uint8_t i=0;i<count;i++)out[i]=Wire.read();return true;
}
bool initPressure() {
  for(uint8_t address : {uint8_t(0x76),uint8_t(0x77)}) {
    bmpAddress=address;uint8_t id=0;
    if(!bmpRead(0xD0,&id,1) || id!=0x58)continue;
    if(!bmpWrite(0xE0,0xB6))continue;
    delay(10); // Bosch requires NVM-copy time after soft reset.
    if(!bmp.begin(address))continue;
    bmp.setSampling(Adafruit_BMP280::MODE_SLEEP,Adafruit_BMP280::SAMPLING_X2,Adafruit_BMP280::SAMPLING_X16,Adafruit_BMP280::FILTER_OFF);
    Serial.printf("[BMP280] chip=0x%02X address=0x%02X forced mode\n",id,address);
    return true;
  }
  bmpAddress=0;return false;
}
void finishPressure() {
  if(!pressurePending || millis()-pressureRequestedMs<80)return;
  uint8_t status=0,raw[6]{};
  bool good=bmpRead(0xF3,&status,1);
  if(good && (status&8) && millis()-pressureRequestedMs<250)return;
  pressurePending=false;
  good=good && !(status&8) && bmpRead(0xF7,raw,6);
  uint32_t adcP=(uint32_t(raw[0])<<12)|(uint32_t(raw[1])<<4)|(raw[2]>>4);
  uint32_t adcT=(uint32_t(raw[3])<<12)|(uint32_t(raw[4])<<4)|(raw[5]>>4);
  good=good && adcP!=0x80000 && adcT!=0x80000 && adcP!=0 && adcT!=0;
  pressureHpa=good?bmp.readPressure()/100.0f:NAN;
  pressureValid=good && isfinite(pressureHpa) && pressureHpa>=300 && pressureHpa<=1100;
  if(!pressureValid)bmpReady=false;
  if(pressureValid)pressureTrend.add(millis(),pressureHpa);
  Serial.printf("[BMP280] raw=%lu/%lu status=0x%02X valid=%d pressure=%.2f temp=%.2f\n",adcP,adcT,status,pressureValid,pressureHpa,good?bmp.readTemperature():NAN);
}

SemaphoreHandle_t weatherMutex;
struct HourForecast { char time[24]; float temperature; int rain, code; };
struct DayForecast { char date[16]; float high, low; int rain, code; };
struct Weather {
  bool available = false;
  bool fetching = false;
  float temperature = 0, humidity = 0, wind = 0, precipitation = 0;
  int code = 0;
  uint32_t updatedMs = 0;
  time_t fetchedAt = 0;
  char modelTime[32] = "";
  char error[96] = "Waiting for Wi-Fi and time sync";
  HourForecast hours[12]{};
  DayForecast days[3]{};
  int hourCount = 0, dayCount = 0;
} weather;
bool sensorValid = false;
float sensorTemperature = NAN, sensorHumidity = NAN;
uint32_t sensorSampleMs = 0;
uint32_t sensorSequence = 0;

void weatherFailure(const char* message) {
  xSemaphoreTake(weatherMutex, portMAX_DELAY);
  weather.fetching = false;
  strlcpy(weather.error, message, sizeof(weather.error));
  xSemaphoreGive(weatherMutex);
  Serial.printf("[Weather] %s\n", message);
}

bool fetchWeather() {
  xSemaphoreTake(weatherMutex, portMAX_DELAY);
  weather.fetching = true;
  xSemaphoreGive(weatherMutex);
  WiFiClientSecure client;
  client.setCACert(rootCA);
  client.setHandshakeTimeout(30);
  HTTPClient http;
  http.setConnectTimeout(12000);
  http.setTimeout(10000);
  if (!http.begin(client, WEATHER_URL)) {
    weatherFailure("Could not initialize HTTPS");
    return false;
  }
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    char error[96],tlsMessage[96];int tlsCode=client.lastError(tlsMessage,sizeof(tlsMessage));
    snprintf(error,sizeof(error),"Open-Meteo HTTP %d; TLS %d: %.48s",code,tlsCode,tlsMessage);
    Serial.printf("[Weather] %s\n",error);
    http.end();
    weatherFailure(error);
    return false;
  }
  DynamicJsonDocument doc(24576);
  auto parseError = deserializeJson(doc, http.getString());
  http.end();
  JsonObject current = doc["current"];
  if (parseError || !current["temperature_2m"].is<float>() ||
      !current["relative_humidity_2m"].is<float>() ||
      !current["wind_speed_10m"].is<float>() ||
      !current["precipitation"].is<float>() ||
      !current["weather_code"].is<int>() || !current["time"].is<const char*>()) {
    weatherFailure("Invalid or incomplete weather response");
    return false;
  }
  Weather next;
  next.available = true;
  next.temperature = current["temperature_2m"];
  next.humidity = current["relative_humidity_2m"];
  next.wind = current["wind_speed_10m"];
  next.precipitation = current["precipitation"];
  next.code = current["weather_code"];
  next.updatedMs = millis();
  next.fetchedAt = time(nullptr);
  strlcpy(next.modelTime, current["time"], sizeof(next.modelTime));
  next.error[0] = '\0';
  JsonObject hourly = doc["hourly"], daily = doc["daily"];
  for (size_t i = 0; i < hourly["time"].size() && next.hourCount < 12; ++i) {
    const char* t = hourly["time"][i] | "";
    if (strcmp(t, next.modelTime) < 0) continue;
    if (!hourly["temperature_2m"][i].is<float>() ||
        !hourly["precipitation_probability"][i].is<int>() ||
        !hourly["weather_code"][i].is<int>()) continue;
    auto &h = next.hours[next.hourCount++];
    strlcpy(h.time, t, sizeof(h.time));
    h.temperature = hourly["temperature_2m"][i];
    h.rain = hourly["precipitation_probability"][i];
    h.code = hourly["weather_code"][i];
  }
  for (size_t i = 0; i < daily["time"].size() && next.dayCount < 3; ++i) {
    if (!daily["temperature_2m_max"][i].is<float>() ||
        !daily["temperature_2m_min"][i].is<float>() ||
        !daily["precipitation_probability_max"][i].is<int>() ||
        !daily["weather_code"][i].is<int>()) continue;
    auto &d = next.days[next.dayCount++];
    strlcpy(d.date, daily["time"][i] | "", sizeof(d.date));
    d.high = daily["temperature_2m_max"][i];
    d.low = daily["temperature_2m_min"][i];
    d.rain = daily["precipitation_probability_max"][i];
    d.code = daily["weather_code"][i];
  }
  if (next.hourCount != 12 || next.dayCount != 3) {
    weatherFailure("Incomplete forecast; will retry");
    return false;
  }
  xSemaphoreTake(weatherMutex, portMAX_DELAY);
  weather = next;
  xSemaphoreGive(weatherMutex);
  Serial.printf("[Weather] %.1f C, %.0f %%, wind %.1f km/h\n",
                next.temperature, next.humidity, next.wind);
  return true;
}

String cloudPending, cloudRestorePending;
time_t cloudLastSuccess=0;int cloudLastHTTP=0;uint32_t cloudAckPending=0;
bool cloudHistoryRestored=false;
void publishCloud() {
  if (!CLOUD_URL[0] || WiFi.status() != WL_CONNECTED || time(nullptr) < 1700000000) return;
  String body;
  xSemaphoreTake(weatherMutex, portMAX_DELAY);
  body = cloudPending;
  cloudPending = "";
  xSemaphoreGive(weatherMutex);
  if (!body.length()) return;
  DynamicJsonDocument meta(24576);deserializeJson(meta,body);uint32_t ack=meta["history_batch_seq"]|0u;meta.clear();meta.shrinkToFit();
  WiFiClientSecure client;
  client.setCACert(CLOUD_CA_PEM);
  client.setHandshakeTimeout(30);
  HTTPClient http;
  http.setConnectTimeout(12000);
  http.setTimeout(6000);
  if (!http.begin(client, CLOUD_URL)) return;
  http.setUserAgent("MayNhaMi-ESP32/1.9");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Bearer ") + CLOUD_INGEST_KEY);
  Serial.println("[Cloud] Starting HTTPS upload");
  int status = http.POST(body);
  cloudLastHTTP=status;
  Serial.printf("[Cloud] HTTP %d\n", status);
  if(status==200){
    cloudLastSuccess=time(nullptr);cloudAckPending=ack;
    Serial.println("[Cloud] Reading response");
    String response=http.getString();
    xSemaphoreTake(weatherMutex,portMAX_DELAY);cloudRestorePending=response;xSemaphoreGive(weatherMutex);
  }
  http.end();
}
String statusJSON(bool forCloud = false);

void weatherTask(void*) {
  bool attempted = false;
  uint32_t previous = 0, interval = RETRY_INTERVAL;
  for (;;) {
    if (!otaBusy && WiFi.status() == WL_CONNECTED && time(nullptr) > 1700000000 &&
        (!attempted || millis() - previous >= interval)) {
      interval = fetchWeather() ? WEATHER_INTERVAL : RETRY_INTERVAL;
      previous = millis();
      attempted = true;
    }
    if(!otaBusy)publishCloud();
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

String statusJSON(bool forCloud) {
  Weather snapshot;
  xSemaphoreTake(weatherMutex, portMAX_DELAY);
  snapshot = weather;
  xSemaphoreGive(weatherMutex);
  uint32_t now = millis();
  DynamicJsonDocument doc(24576);
  doc["api_version"] = 1;
  doc["build"] = __DATE__ " " __TIME__;
  doc["sync"]["last_success_unix"]=cloudLastSuccess;doc["sync"]["last_http"]=cloudLastHTTP;doc["sync"]["pending_points"]=historyBacklog();doc["sync"]["storage_ready"]=historyReady;
  if(forCloud)doc["history_batch_seq"]=appendHistory(doc.createNestedArray("sensor_history"));
  doc["firmware"] = "1.14.1";
  doc["device"]["name"] = "Luc Ngan Weather";
  doc["device"]["id"] = WiFi.macAddress();
  doc["device"]["uptime_s"] = now / 1000;
  doc["device"]["free_heap_bytes"] = ESP.getFreeHeap();
  doc["device"]["reset_reason"] = resetReasonName();
  doc["device"]["min_free_heap_bytes"] = ESP.getMinFreeHeap();
  doc["device"]["largest_free_block"] = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  doc["device"]["loop_stack_min_bytes"] = uxTaskGetStackHighWaterMark(nullptr);
  if(weatherTaskHandle)doc["device"]["weather_stack_min_bytes"] = uxTaskGetStackHighWaterMark(weatherTaskHandle);
  doc["wifi"]["connected"] = WiFi.status() == WL_CONNECTED;
  doc["wifi"]["ip"] = WiFi.localIP().toString();
  doc["wifi"]["rssi_dbm"] = WiFi.RSSI();
  doc["location"]["name"] = "Khu vực của bạn · khu vực Tân Hoa";
  doc["location"]["latitude"] = 21.0285;
  doc["location"]["longitude"] = 105.8542;
  doc["location"]["approximate"] = true;
  doc["sensor"]["type"] = "SHT4X";
  doc["sensor"]["sda"] = 21;
  doc["sensor"]["scl"] = 22;
  doc["sensor"]["ok"] = sensorValid;
  doc["sensor"]["sequence"] = sensorSequence;
  doc["sensor"]["temperature_c"] = nullptr;
  doc["sensor"]["humidity_pct"] = nullptr;
  doc["sensor"]["age_s"] = nullptr;
  doc["sensor"]["raw_temperature_c"] = nullptr;
  doc["sensor"]["raw_humidity_pct"] = nullptr;
  doc["sensor"]["calibration"]["enabled"] = temperatureOffset!=0 || humidityOffset!=0;
  doc["sensor"]["calibration"]["method"] = (temperatureOffset!=0 || humidityOffset!=0)?"manual_offset":"factory";
  doc["sensor"]["calibration"]["temperature_offset_c"] = temperatureOffset;
  doc["sensor"]["calibration"]["humidity_offset_pct"] = humidityOffset;
  doc["sensor"]["calibration"]["reference_temperature_c"] = nullptr;
  doc["sensor"]["calibration"]["reference_humidity_pct"] = nullptr;
  if (sensorSequence) doc["sensor"]["age_s"] = (now - sensorSampleMs) / 1000;
  if (sensorValid) {
    doc["sensor"]["raw_temperature_c"] = sensorTemperature;
    doc["sensor"]["raw_humidity_pct"] = sensorHumidity;
    doc["sensor"]["temperature_c"] = sensorTemperature + temperatureOffset;
    doc["sensor"]["humidity_pct"] = constrain(sensorHumidity + humidityOffset, 0.0f, 100.0f);
  }
  doc["sensor"]["error"] = sensorValid ? "" :
    (sensorSequence ? "SHT4X read failed; check SDA21/SCL22 and power" : "Waiting for first sample");
  doc["pressure"]["type"] = "BMP280";
  doc["pressure"]["ok"] = pressureValid;
  doc["pressure"]["address"] = bmpAddress;
  doc["pressure"]["error"] = pressureValid?"":"BMP280 has no valid completed reading; check wiring";
  doc["pressure"]["pressure_hpa"] = nullptr;
  if(pressureValid)doc["pressure"]["pressure_hpa"] = pressureHpa;
  auto trend=pressureTrend.get(now,pressureValid);
  doc["local_forecast"]["state"]=trend.state;
  doc["local_forecast"]["trend"]=trend.trend;
  doc["local_forecast"]["ready"]=trend.ready;
  doc["local_forecast"]["history_minutes"]=trend.minutes;
  doc["local_forecast"]["samples"]=trend.samples;
  doc["local_forecast"]["confidence"]="low";
  doc["local_forecast"]["horizon_hours"]=3;
  doc["local_forecast"]["slope_hpa_hour"]=nullptr;
  doc["local_forecast"]["change_hpa"]=nullptr;
  if(trend.ready){doc["local_forecast"]["slope_hpa_hour"]=trend.slope;doc["local_forecast"]["change_hpa"]=trend.change;}
  auto album=doc["display"].createNestedArray("photo_slots");for(int i=0;i<photoCount;i++)album.add(photoSlots[i]);
  doc["display"]["album_ready"]=albumReady;
  doc["display"]["on"] = displayOn;
  doc["display"]["page"] = screenPage>=2?"photo":screenPage==1?"calendar":"sensors";
  doc["display"]["interval_s"] = slideSeconds;
  doc["display"]["rotation"] = displayRotation;
  doc["display"]["palette"] = displayPalette;doc["display"]["clock_style"] = displayClock;
  doc["display"]["slide_order"] = slideOrder;
  doc["display"]["backlight_enabled"] = displayBacklightEnabled;
  doc["display"]["brightness"] = displayBrightness;
  doc["display"]["night_brightness"] = displayNightBrightness;
  doc["display"]["mode"] = displayMode;
  doc["display"]["dark"] = displayDark;
  doc["display"]["effective_brightness"] = displayEffectiveBrightness;
  time_t instant=time(nullptr);struct tm local{};localtime_r(&instant,&local);
  doc["clock"]["synced"] = instant>1700000000;
  doc["clock"]["timezone"] = "Asia/Ho_Chi_Minh";
  if(instant>1700000000) {
    char stamp[32];strftime(stamp,sizeof(stamp),"%Y-%m-%dT%H:%M:%S+07:00",&local);
    doc["clock"]["local"] = stamp;
    auto lunar=vietnameseLunar(local.tm_mday,local.tm_mon+1,local.tm_year+1900);
    doc["clock"]["lunar"]["valid"] = lunar.valid;
    doc["clock"]["lunar"]["day"] = lunar.day;
    doc["clock"]["lunar"]["month"] = lunar.month;
    doc["clock"]["lunar"]["year"] = lunar.year;
    doc["clock"]["lunar"]["leap"] = lunar.leap;
  }
  JsonObject w = doc.createNestedObject("weather");
  w["source"] = "Open-Meteo";
  w["available"] = snapshot.available;
  w["fetching"] = snapshot.fetching;
  w["stale"] = !snapshot.available || snapshot.error[0] != '\0' ||
    WiFi.status() != WL_CONNECTED || now - snapshot.updatedMs >= WEATHER_INTERVAL + RETRY_INTERVAL;
  w["error"] = snapshot.error;
  w["refresh_interval_s"] = WEATHER_INTERVAL / 1000;
  w["temperature_c"] = nullptr;
  w["humidity_pct"] = nullptr;
  w["wind_kmh"] = nullptr;
  w["precipitation_mm"] = nullptr;
  w["weather_code"] = nullptr;
  w["age_s"] = nullptr;
  w["fetched_at_unix"] = nullptr;
  w["model_time"] = nullptr;
  if (snapshot.available) {
    w["temperature_c"] = snapshot.temperature;
    w["humidity_pct"] = snapshot.humidity;
    w["wind_kmh"] = snapshot.wind;
    w["precipitation_mm"] = snapshot.precipitation;
    w["weather_code"] = snapshot.code;
    w["age_s"] = (now - snapshot.updatedMs) / 1000;
    w["fetched_at_unix"] = snapshot.fetchedAt;
    w["model_time"] = snapshot.modelTime;
  }
  JsonArray hours = w.createNestedArray("hourly");
  for (int i = 0; i < snapshot.hourCount; ++i) {
    auto h = hours.createNestedObject();
    h["time"] = snapshot.hours[i].time;
    h["temperature_c"] = snapshot.hours[i].temperature;
    h["rain_probability_pct"] = snapshot.hours[i].rain;
    h["weather_code"] = snapshot.hours[i].code;
  }
  JsonArray days = w.createNestedArray("daily");
  for (int i = 0; i < snapshot.dayCount; ++i) {
    auto d = days.createNestedObject();
    d["date"] = snapshot.days[i].date;
    d["high_c"] = snapshot.days[i].high;
    d["low_c"] = snapshot.days[i].low;
    d["rain_probability_pct"] = snapshot.days[i].rain;
    d["weather_code"] = snapshot.days[i].code;
  }
  if (forCloud) {
    doc.remove("wifi");
    doc.remove("device");
    doc.remove("location");
    time_t epoch=time(nullptr);
    doc["sent_at_unix"] = epoch;
    doc["restore_history"] = !cloudHistoryRestored;
    JsonArray history=doc.createNestedArray("pressure_history");
    for(int i=0;i<pressureTrend.size();i++){
      uint32_t age=now-pressureTrend.pointMs(i);if(age>10800000)continue;
      auto point=history.createNestedArray();point.add(epoch-age/1000);point.add(pressureTrend.pointPressure(i));
    }
  }
  String json;
  serializeJson(doc, json);
  return json;
}
void sendStatus() {
  String json = statusJSON();
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json; charset=utf-8", json);
}

File photoUpload;size_t photoReceived=0;bool photoFailed=true;int photoUploadSlot=-1;
void setup() {
  Serial.begin(115200);
  Serial.printf("[Boot] reason=%s firmware=1.14.1\n",resetReasonName());
  Serial.printf("[TrendTest] %s\n",trendSelfTest()?"PASS":"FAIL");
  Wire.begin(21,22);Wire.setClock(100000);Wire.setTimeOut(50);
  for(uint8_t addr=1;addr<127;addr++){Wire.beginTransmission(addr);if(Wire.endTransmission()==0)Serial.printf("[I2C] found 0x%02X\n",addr);}
  shtReady=sht.begin(&Wire);
  if(shtReady){sht.setPrecision(SHT4X_HIGH_PRECISION);sht.setHeater(SHT4X_NO_HEATER);Serial.printf("[SHT4X] serial %lu\n",sht.readSerial());}
  bmpReady=initPressure();
  Serial.printf("[Sensors] SHT4X=%d BMP280=%d\n",shtReady,bmpReady);
  initCalibration();
  initDisplay();
  initHistory();
  weatherMutex = xSemaphoreCreateMutex();
  if (!weatherMutex) { Serial.println("Mutex allocation failed"); ESP.restart(); }
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("may-nha-mi");
  WiFi.setAutoReconnect(true);
  beginSavedWifi();
  configTime(7*3600, 0, "pool.ntp.org", "time.google.com");
  server.on("/", HTTP_GET, []() {
    server.sendHeader("Cache-Control", "no-cache");
    server.send_P(200, "text/html; charset=utf-8", page);
  });
  server.on("/api/status", HTTP_GET, sendStatus);
  server.on("/api/history",HTTP_GET,sendHistory);
  const char* controlHeaders[]={"X-Mi-Control"};server.collectHeaders(controlHeaders,1);
  initMaintenance();
  server.on("/api/slideshow",HTTP_POST,[](){
    server.sendHeader("Cache-Control","no-store");
    if(server.header("X-Mi-Control")!="1"){server.send(403);return;}
    StaticJsonDocument<192> body;
    if(server.arg("plain").length()>128||deserializeJson(body,server.arg("plain"))||!body["seconds"].is<int>()||!body["order"].is<const char*>()){server.send(400);return;}
    int seconds=body["seconds"];String order=body["order"].as<String>();
    if(seconds<5||seconds>60||!validSlideOrder(order)){server.send(400);return;}
    saveSlideshow(seconds,order);server.send(200,"application/json","{\"ok\":true}");
  });
  server.on("/api/display/style",HTTP_POST,[](){
    server.sendHeader("Cache-Control","no-store");
    if(server.header("X-Mi-Control")!="1"){server.send(403);return;}
    StaticJsonDocument<256> body;
    if(server.arg("plain").length()>128||deserializeJson(body,server.arg("plain"))||!body["palette"].is<const char*>()||!body["clock_style"].is<const char*>()){server.send(400);return;}
    String palette=body["palette"].as<String>(),clock=body["clock_style"].as<String>();
    if(!validPalette(palette)||!validClock(clock)){server.send(400);return;}
    saveDisplayStyle(palette,clock);server.send(200,"application/json","{\"ok\":true}");
  });
  server.on("/api/display/rotation",HTTP_POST,[](){
    server.sendHeader("Cache-Control","no-store");
    if(server.header("X-Mi-Control")!="1"){server.send(403);return;}
    StaticJsonDocument<128> body;
    if(server.arg("plain").length()>64||deserializeJson(body,server.arg("plain"))||!body["rotation"].is<int>()){server.send(400);return;}
    int angle=body["rotation"];
    if(angle!=0&&angle!=90&&angle!=180&&angle!=270){server.send(400);return;}
    saveDisplayRotation(angle);server.send(200,"application/json","{\"ok\":true}");
  });
  server.on("/api/display",HTTP_POST,[](){
    server.sendHeader("Cache-Control","no-store");
    if(server.header("X-Mi-Control")!="1"){server.send(403,"application/json","{\"error\":\"control_header_required\"}");return;}
    if(server.arg("plain").length()>256){server.send(413);return;}
    StaticJsonDocument<384> body;
    if(deserializeJson(body,server.arg("plain")) || !body["brightness"].is<int>() || !body["night_brightness"].is<int>() || !body["mode"].is<const char*>() || !body["backlight_enabled"].is<bool>()){
      server.send(400,"application/json","{\"error\":\"invalid_settings\"}");return;
    }
    int day=body["brightness"],night=body["night_brightness"];String mode=body["mode"].as<String>();
    if(day<0||day>100||night<0||night>100||(mode!="auto"&&mode!="light"&&mode!="dark")){
      server.send(400,"application/json","{\"error\":\"invalid_settings\"}");return;
    }
    saveDisplaySettings(day,night,mode,body["backlight_enabled"].as<bool>());
    server.send(200,"application/json","{\"ok\":true}");
  });
  server.on("/api/device",HTTP_POST,[](){
    server.sendHeader("Cache-Control","no-store");
    if(server.header("X-Mi-Control")!="1"){server.send(403,"application/json","{\"error\":\"control_header_required\"}");return;}
    StaticJsonDocument<192> body;
    if(server.arg("plain").length()>128 || deserializeJson(body,server.arg("plain")) || !body["action"].is<const char*>()){server.send(400,"application/json","{\"error\":\"invalid_action\"}");return;}
    String action=body["action"].as<String>();
    if(action=="restart"){server.send(200,"application/json","{\"ok\":true}");restartAt=millis()+750;return;}
    if(action!="screen_on" && action!="screen_off"){server.send(400,"application/json","{\"error\":\"invalid_action\"}");return;}
    if(action=="screen_off"&&!displayBacklightEnabled){server.send(409,"application/json","{\"error\":\"backlight_not_configured\"}");return;}
    setDisplayPower(action=="screen_on");server.send(200,"application/json","{\"ok\":true}");
  });
  server.on("/api/photo",HTTP_POST,[](){
    if(server.header("X-Mi-Control")!="1"){server.send(403);return;}
    if(photoFailed||photoReceived!=PHOTO_BYTES){LittleFS.remove("/upload.tmp");server.send(400,"application/json","{\"error\":\"invalid_photo\"}");return;}
    if(!LittleFS.rename("/upload.tmp",photoPath(photoUploadSlot))){server.send(500);return;}
    scanPhotos();server.send(200,"application/json","{\"ok\":true}");
  },[](){
    auto& upload=server.upload();
    if(upload.status==UPLOAD_FILE_START){
      photoFailed=true;photoReceived=0;photoUploadSlot=-1;
      if(photoUpload)photoUpload.close();
      String slot=server.arg("slot");
      if(server.header("X-Mi-Control")!="1"||!albumReady||slot.length()!=1||slot[0]<'0'||slot[0]>'3')return;
      photoUploadSlot=slot.toInt();photoUpload=LittleFS.open("/upload.tmp","w");photoFailed=!photoUpload;
    }else if(upload.status==UPLOAD_FILE_WRITE){
      if(!photoFailed){if(photoReceived+upload.currentSize>PHOTO_BYTES||photoUpload.write(upload.buf,upload.currentSize)!=upload.currentSize)photoFailed=true;photoReceived+=upload.currentSize;}
    }else if(upload.status==UPLOAD_FILE_END){if(photoUpload)photoUpload.close();}
    else if(upload.status==UPLOAD_FILE_ABORTED){if(photoUpload)photoUpload.close();photoFailed=true;LittleFS.remove("/upload.tmp");}
  });
  server.on("/api/photo/delete",HTTP_POST,[](){
    if(server.header("X-Mi-Control")!="1"){server.send(403);return;}
    String slot=server.arg("slot");if(slot.length()!=1||slot[0]<'0'||slot[0]>'3'){server.send(400);return;}
    LittleFS.remove(photoPath(slot.toInt()));scanPhotos();server.send(200,"application/json","{\"ok\":true}");
  });
  server.on("/favicon.ico", HTTP_GET, []() { server.send(204); });
  server.onNotFound([]() {
    server.send(404, "application/json", "{\"error\":\"not_found\"}");
  });
  server.begin();
  // HTTPS may compute without blocking for >5 s. Share CPU0 with IDLE0
  // at idle priority so the watchdog stays active and can run normally.
  if (xTaskCreatePinnedToCore(weatherTask, "weather", 12288, nullptr, tskIDLE_PRIORITY, &weatherTaskHandle, 0) != pdPASS)
    weatherFailure("Weather task could not start");
  Serial.println("\n[Ready] SHT4X/BMP280 I2C21/22 TFT18/23/25/26/27. Connecting to Wi-Fi...");
}

void loop() {
  if(restartAt && int32_t(millis()-restartAt)>=0){Serial.println("[Restart] Scheduled by control/OTA endpoint");Serial.flush();ESP.restart();}
  static uint32_t lastSensor = 0, lastReconnect = 0;
  static bool wasConnected = false;
  bool connected = WiFi.status() == WL_CONNECTED;
  if (connected && !wasConnected) {
    MDNS.end();
    if (MDNS.begin("may-nha-mi")) MDNS.addService("http", "tcp", 80);
    Serial.printf("[Web] http://%s/ | http://may-nha-mi.local/\n",
                  WiFi.localIP().toString().c_str());
  }
  if (!connected && wasConnected) Serial.println("[WiFi] Disconnected; reconnecting");
  wasConnected = connected;
  if (!connected && millis() - lastReconnect >= 20000) {
    lastReconnect = millis();
    WiFi.reconnect();
    Serial.printf("[WiFi] Waiting, status=%d\n", WiFi.status());
  }
  server.handleClient();
  maintenanceTick();
  if(otaBusy){delay(2);return;}
  if (millis() - lastSensor >= SENSOR_INTERVAL) {
    lastSensor = millis();
    if(!shtReady && sensorSequence%10==0){shtReady=sht.begin(&Wire);if(shtReady){sht.setPrecision(SHT4X_HIGH_PRECISION);sht.setHeater(SHT4X_NO_HEATER);}}
    sensors_event_t humidity{},temperature{};
    sensorValid=shtReady && sht.getEvent(&humidity,&temperature);
    sensorTemperature=temperature.temperature;sensorHumidity=humidity.relative_humidity;
    sensorValid= sensorValid && isfinite(sensorTemperature) && isfinite(sensorHumidity) && sensorHumidity>=0 && sensorHumidity<=100;
    if(!sensorValid)shtReady=false;
    if(!bmpReady && sensorSequence%10==0)bmpReady=initPressure();
    if(bmpReady){pressurePending=bmpWrite(0xF4,0x55);pressureRequestedMs=millis();if(!pressurePending){pressureValid=false;bmpReady=false;}}
    sensorSampleMs = millis();
    ++sensorSequence;
    Serial.printf("[Sensors] SHT4X=%d %.2f C %.2f %% | BMP280=%d %.2f hPa\n",sensorValid,sensorTemperature,sensorHumidity,pressureValid,pressureHpa);
  }
  static uint32_t lastCloud = 0;
  if (CLOUD_URL[0] && connected && time(nullptr) > 1700000000 && millis() - lastCloud >= 30000) {
    lastCloud = millis();
    String next = statusJSON(true);
    xSemaphoreTake(weatherMutex, portMAX_DELAY);
    cloudPending = next;
    xSemaphoreGive(weatherMutex);
  }
  String restored;
  xSemaphoreTake(weatherMutex,portMAX_DELAY);restored=cloudRestorePending;cloudRestorePending="";xSemaphoreGive(weatherMutex);
  if(!cloudHistoryRestored && restored.length()){
    DynamicJsonDocument history(16384);
    if(!deserializeJson(history,restored) && history["pressure_history"].is<JsonArray>()){
      PressureTrend merged;uint32_t tick=millis();time_t epoch=time(nullptr);
      for(JsonArray p:history["pressure_history"].as<JsonArray>()){
        int64_t age=int64_t(epoch)-p[0].as<int64_t>();
        if(age>=0&&age<=10800)merged.restorePoint(tick-uint32_t(age*1000),p[1].as<float>());
      }
      // Merge new readings collected while HTTPS/NTP was connecting.
      for(int i=0;i<pressureTrend.size();i++){
        uint32_t stamp=pressureTrend.pointMs(i);
        if(!merged.size() || tick-stamp < tick-merged.pointMs(merged.size()-1))merged.restorePoint(stamp,pressureTrend.pointPressure(i));
      }
      if(merged.size() && tick-merged.pointMs(merged.size()-1)<=300000 && pressureValid && fabsf(pressureHpa-merged.pointPressure(merged.size()-1))<=2.0f){
        pressureTrend=merged;pressureTrend.add(tick,pressureHpa);
      }
      cloudHistoryRestored=pressureValid || history["pressure_history"].size()==0;Serial.printf("[Cloud] history restored: %d points\n",pressureTrend.size());
    }
  }
  finishPressure();
  sampleHistory(sensorValid,sensorTemperature+temperatureOffset,constrain(sensorHumidity+humidityOffset,0.0f,100.0f),pressureValid,pressureHpa);
  if(cloudAckPending){acknowledgeHistory(cloudAckPending);cloudAckPending=0;}
  if(!setupAP || connected)updateDisplay(sensorValid,sensorTemperature+temperatureOffset,constrain(sensorHumidity+humidityOffset,0.0f,100.0f),pressureValid,pressureHpa);
  if(setupAP && !connected){static uint32_t helpPaint=0;if(millis()-helpPaint>1000){helpPaint=millis();drawNetworkHelp();}}
  delay(2);
}
