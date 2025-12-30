/***************************************************************
 * ESP01S WEATHER CLOCK - FINAL ASCII SAFE VERSION
 ***************************************************************/

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include <time.h>
#include <ESP8266HTTPClient.h>
#include <EEPROM.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>

/***************************************************************
 * OLED
 ***************************************************************/
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
#define I2C_SDA 0
#define I2C_SCL 2

/***************************************************************
 * CONFIG STRUCT
 ***************************************************************/
struct ConfigData {
  uint32_t magic;
  double latitude;
  double longitude;
  char locationName[32];
  uint8_t wifiConfigured;
  char wifiSsid[32];
  char wifiPass[64];
  char mqttBroker[16];
};

ConfigData config;
#define EEPROM_SIZE 256
const uint32_t CONFIG_MAGIC = 0xDEADBEEF;

/***************************************************************
 * RTC DOUBLE RESET
 ***************************************************************/
struct RtcData {
  uint32_t magic;
  uint32_t armed;
};

RtcData rtcData;
const uint32_t RTC_MAGIC = 0x13572468;
bool doubleResetDetected = false;
bool doubleResetWindowActive = false;
unsigned long doubleResetWindowStart = 0;
const unsigned long DOUBLE_RESET_WINDOW_MS = 5000;

void initDoubleReset() {
  uint32_t buf[2];
  ESP.rtcUserMemoryRead(0, buf, sizeof(buf));
  rtcData.magic = buf[0];
  rtcData.armed = buf[1];

  if (rtcData.magic != RTC_MAGIC) {
    rtcData.magic = RTC_MAGIC;
    rtcData.armed = 0;
  } else if (rtcData.armed == 1) {
    doubleResetDetected = true;
    rtcData.armed = 0;
  } else {
    rtcData.armed = 1;
    doubleResetWindowActive = true;
    doubleResetWindowStart = millis();
  }

  buf[0] = rtcData.magic;
  buf[1] = rtcData.armed;
  ESP.rtcUserMemoryWrite(0, buf, sizeof(buf));
}

void updateDoubleResetWindow() {
  if (!doubleResetWindowActive) return;
  if (millis() - doubleResetWindowStart > DOUBLE_RESET_WINDOW_MS) {
    rtcData.armed = 0;
    uint32_t buf[2] = {rtcData.magic, rtcData.armed};
    ESP.rtcUserMemoryWrite(0, buf, sizeof(buf));
    doubleResetWindowActive = false;
  }
}

/***************************************************************
 * TOWNS
 ***************************************************************/
struct Town { const char* name; double lat; double lon; };
Town towns[] = {
  {"London",51.5074,-0.1278},{"Birmingham",52.4862,-1.8904},
  {"Manchester",53.4808,-2.2426},{"Liverpool",53.4084,-2.9916},
  {"Leeds",53.8008,-1.5491},{"Newcastle",54.9783,-1.6178},
  {"Glasgow",55.8642,-4.2518},{"Edinburgh",55.9533,-3.1883},
  {"Cardiff",51.4816,-3.1791},{"Swansea",51.6214,-3.9436},
  {"Pembrey",51.6890,-4.3170},{"Carmarthen",51.8570,-4.3110},
  {"Bristol",51.4545,-2.5879},{"Oxford",51.7520,-1.2577},
  {"Cambridge",52.2053,0.1218},{"Nottingham",52.9548,-1.1581},
  {"Sheffield",53.3811,-1.4701},{"York",53.9590,-1.0815},
  {"Rugby",52.3709,-1.2642}
};
int townCount = sizeof(towns)/sizeof(towns[0]);

/***************************************************************
 * EEPROM
 ***************************************************************/
void loadConfig() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, config);
  if (config.magic != CONFIG_MAGIC) {
    memset(&config, 0, sizeof(config));
    config.latitude = NAN;
    config.longitude = NAN;
  }
}

void saveConfig() {
  config.magic = CONFIG_MAGIC;
  EEPROM.put(0, config);
  EEPROM.commit();
}

void factoryReset() {
  memset(&config, 0, sizeof(config));
  EEPROM.begin(EEPROM_SIZE);
  for (int i = 0; i < EEPROM_SIZE; i++) EEPROM.write(i, 0xFF);
  EEPROM.commit();
}

/***************************************************************
 * WEB PORTAL
 ***************************************************************/
ESP8266WebServer server(80);

void handleScan() {
  int n = WiFi.scanNetworks();
  String json = "[";
  for (int i = 0; i < n; i++) {
    json += "\"" + WiFi.SSID(i) + "\"";
    if (i < n - 1) json += ",";
  }
  json += "]";
  server.send(200, "application/json", json);
}

void handleInfo() {
  String p = "<html><body><h1>Info</h1>";
  p += "Location: " + String(config.locationName) + "<br>";
  p += "MQTT: " + String(config.mqttBroker) + "<br>";
  p += "IP: " + WiFi.localIP().toString() + "<br>";
  p += "<a href='/'>Back</a></body></html>";
  server.send(200, "text/html", p);
}

void handleRoot() {
  String p =
    "<html><body><h1>Setup</h1>"
    "<a href='/info'>Info</a><br><br>"
    "<button onclick='scan()'>Scan WiFi</button><br><br>"
    "<form method='POST' action='/save'>"
    "SSID:<br><select id='ssid' name='ssid'></select><br><br>"
    "Password:<br><input name='pass' type='password'><br><br>"
    "Location:<br><select name='town'>";

  for (int i = 0; i < townCount; i++)
    p += "<option value='" + String(i) + "'>" + towns[i].name + "</option>";

  p +=
    "</select><br><br>"
    "MQTT Broker:<br>"
    "<input name='mqtt' value='192.168.1.10'><br><br>"
    "<input type='submit' value='Save & Reboot'>"
    "</form>"
    "<script>"
    "function scan(){fetch('/scan').then(r=>r.json()).then(list=>{"
    "let s=document.getElementById('ssid');s.innerHTML='';"
    "list.forEach(x=>{let o=document.createElement('option');o.value=x;o.text=x;s.appendChild(o);});"
    "});}"
    "</script></body></html>";

  server.send(200, "text/html", p);
}

void handleSave() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  int town = server.arg("town").toInt();
  String mqtt = server.arg("mqtt");

  strncpy(config.wifiSsid, ssid.c_str(), sizeof(config.wifiSsid));
  strncpy(config.wifiPass, pass.c_str(), sizeof(config.wifiPass));
  strncpy(config.mqttBroker, mqtt.c_str(), sizeof(config.mqttBroker));

  config.latitude = towns[town].lat;
  config.longitude = towns[town].lon;
  strncpy(config.locationName, towns[town].name, sizeof(config.locationName));

  config.wifiConfigured = 1;
  saveConfig();

  server.send(200, "text/html", "Saved. Rebooting...");
  delay(1000);
  ESP.restart();
}

void startConfigPortal() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP("WeatherClock-Setup");

  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/scan", handleScan);
  server.on("/info", handleInfo);
  server.begin();

  while (true) {
    server.handleClient();
    yield();
  }
}

/***************************************************************
 * WIFI + OTA
 ***************************************************************/
bool connectToWiFiStation() {
  if (!config.wifiConfigured) return false;
  WiFi.mode(WIFI_STA);
  WiFi.begin(config.wifiSsid, config.wifiPass);
  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < 15000) delay(250);
  return WiFi.status() == WL_CONNECTED;
}

void setupOTA() {
  ArduinoOTA.setHostname("ESP01S-WeatherClock");
  ArduinoOTA.begin();
}

/***************************************************************
 * MQTT
 ***************************************************************/
WiFiClient mqttWifiClient;
PubSubClient mqttClient(mqttWifiClient);

void mqttPublishDiscovery() {
  if (strlen(config.mqttBroker) == 0) return;

  String id = "weatherclock_" + String(ESP.getChipId(), HEX);
  String dev = "{\"identifiers\":[\"" + id + "\"]}";

  auto pub = [&](String sensor, String unit, String field) {
    String topic = "homeassistant/sensor/" + id + "_" + sensor + "/config";
    String payload =
      "{\"name\":\"WeatherClock " + sensor + "\","
      "\"state_topic\":\"weatherclock/state\","
      "\"unit_of_measurement\":\"" + unit + "\","
      "\"value_template\":\"{{ value_json." + field + " }}\","
      "\"unique_id\":\"" + id + "_" + sensor + "\","
      "\"device\":" + dev + "}";
    mqttClient.publish(topic.c_str(), payload.c_str(), true);
  };

  pub("temperature","C","temperature");
  pub("humidity","%","humidity");
  pub("rain","%","rain");
}

void mqttPublishState(float t, int h, int r) {
  if (!mqttClient.connected()) return;
  String payload = "{";
  payload += "\"temperature\":" + String(t,1) + ",";
  payload += "\"humidity\":" + String(h) + ",";
  payload += "\"rain\":" + String(r);
  payload += "}";
  mqttClient.publish("weatherclock/state", payload.c_str());
}

void mqttReconnect() {
  if (strlen(config.mqttBroker) == 0) return;
  mqttClient.setServer(config.mqttBroker, 1883);
  if (!mqttClient.connected()) {
    String cid = "WC-" + String(ESP.getChipId(), HEX);
    if (mqttClient.connect(cid.c_str())) mqttPublishDiscovery();
  }
}

/***************************************************************
 * WEATHER
 ***************************************************************/
float currentTemp = NAN;
int currentHumidity = -1;
int currentRainProb = -1;
float dailyMax[3];
float dailyMin[3];
int dailyRain[3];

void updateWeather() {
  WiFiClient client;
  HTTPClient http;

  String url =
    "http://api.open-meteo.com/v1/forecast?latitude=" + String(config.latitude,6) +
    "&longitude=" + String(config.longitude,6) +
    "&hourly=temperature_2m,relative_humidity_2m,precipitation_probability"
    "&daily=temperature_2m_max,temperature_2m_min,precipitation_probability_max"
    "&forecast_days=3&timezone=auto";

  if (!http.begin(client, url)) return;
  if (http.GET() != 200) return;

  DynamicJsonDocument doc(4096);
  deserializeJson(doc, http.getString());

  currentTemp     = doc["hourly"]["temperature_2m"][0];
  currentHumidity = doc["hourly"]["relative_humidity_2m"][0];
  currentRainProb = doc["hourly"]["precipitation_probability"][0];

  for (int i = 0; i < 3; i++) {
    dailyMax[i]  = doc["daily"]["temperature_2m_max"][i];
    dailyMin[i]  = doc["daily"]["temperature_2m_min"][i];
    dailyRain[i] = doc["daily"]["precipitation_probability_max"][i];
  }
}

/***************************************************************
 * BRIGHTNESS
 ***************************************************************/
void updateBrightness() {
  time_t now = time(nullptr);
  int hour = localtime(&now)->tm_hour;
  display.ssd1306_command(SSD1306_SETCONTRAST);
  display.ssd1306_command((hour >= 22 || hour < 6) ? 0x20 : 0xCF);
}

/***************************************************************
 * ICONS
 ***************************************************************/
const uint8_t sunFrames[2][8] PROGMEM = {
  {0x10,0x38,0x7C,0x7C,0x7C,0x38,0x10,0x00},
  {0x00,0x28,0x5C,0x7C,0x5C,0x28,0x00,0x00}
};

const uint8_t cloudFrames[2][8] PROGMEM = {
  {0x00,0x38,0x7C,0xFE,0xFE,0x7C,0x00,0x00},
  {0x00,0x00,0x38,0x7C,0xFE,0xFE,0x7C,0x00}
};

const uint8_t rainFrames[2][8] PROGMEM = {
  {0x00,0x38,0x7C,0xFE,0xFE,0x48,0x90,0x00},
  {0x00,0x38,0x7C,0xFE,0xFE,0x08,0x50,0x80}
};

const uint8_t heavyRainFrames[2][8] PROGMEM = {
  {0x00,0x38,0x7C,0xFE,0xFE,0x4A,0xA4,0x00},
  {0x00,0x38,0x7C,0xFE,0xFE,0x0A,0x44,0xA0}
};

const uint8_t thunderFrames[2][8] PROGMEM = {
  {0x08,0x1C,0x3E,0x3C,0x18,0x3C,0x20,0x00},
  {0x00,0x08,0x1C,0x3E,0x1C,0x08,0x00,0x00}
};

int animFrame = 0;

void drawAnimatedIcon(int rain, int x, int y) {
  int f = animFrame % 2;
  if (rain < 10) display.drawBitmap(x,y,sunFrames[f],8,8,1);
  else if (rain < 30) display.drawBitmap(x,y,cloudFrames[f],8,8,1);
  else if (rain < 60) display.drawBitmap(x,y,rainFrames[f],8,8,1);
  else if (rain < 80) display.drawBitmap(x,y,heavyRainFrames[f],8,8,1);
  else display.drawBitmap(x,y,thunderFrames[f],8,8,1);
}

/***************************************************************
 * DISPLAY
 ***************************************************************/
char timeStr[16];
char dateStr[16];

void drawCurrent() {
  display.clearDisplay();
  display.setCursor(0,0);
  display.print(config.locationName);

  display.setCursor(0,12);
  display.print(timeStr);

  display.setCursor(0,22);
  display.print(dateStr);

  drawAnimatedIcon(currentRainProb, 0, 40);

  display.setCursor(12,40);
  display.print(currentTemp,1); display.print("C ");
  display.print(currentHumidity); display.print("% ");
  display.print(currentRainProb); display.print("%");

  display.display();
}

/***************************************************************
 * SETUP
 ***************************************************************/
void setup() {
  Serial.begin(115200);
  delay(500);

  initDoubleReset();

  Wire.begin(I2C_SDA, I2C_SCL);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.setRotation(2);

  if (doubleResetDetected) {
    display.clearDisplay();
    display.setCursor(0,0);
    display.print("Factory reset");
    display.display();
    factoryReset();
    startConfigPortal();
  }

  loadConfig();

  if (!connectToWiFiStation()) startConfigPortal();

  setupOTA();
  configTime(0,0,"pool.ntp.org","time.nist.gov");
  updateWeather();
}

/***************************************************************
 * LOOP
 ***************************************************************/
void loop() {
  ArduinoOTA.handle();
  mqttClient.loop();
  mqttReconnect();
  updateDoubleResetWindow();

  static unsigned long lastAnim=0,lastBright=0,lastWeather=0,lastState=0,lastDisp=0;

  if (millis()-lastAnim>500) { animFrame++; lastAnim=millis(); }
  if (millis()-lastBright>60000) { updateBrightness(); lastBright=millis(); }
  if (millis()-lastWeather>600000) { updateWeather(); lastWeather=millis(); }
  if (millis()-lastState>60000) { mqttPublishState(currentTemp,currentHumidity,currentRainProb); lastState=millis(); }

  time_t now=time(nullptr);
  strftime(timeStr,sizeof(timeStr),"%H:%M",localtime(&now));
  strftime(dateStr,sizeof(dateStr),"%d/%m/%Y",localtime(&now));

  if (millis()-lastDisp>1000) {
    drawCurrent();
    lastDisp=millis();
  }
}
