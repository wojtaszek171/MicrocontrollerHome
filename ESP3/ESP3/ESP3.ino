#include <StreamUtils.h>
#include <EEPROM.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include "ClosedCube_HDC1080.h"
#include <WiFiManager.h>
#include <ArduinoJson.h>

#define JSON_ADDR 0

bool shouldSaveConfig = false;
ClosedCube_HDC1080 hdc1080;
WiFiClient wifi;
WiFiManager wifiManager;
WiFiManagerParameter hostTextBox;
WiFiManagerParameter controllerIdTextBox;
WiFiManagerParameter apiKeyTextBox;

struct Config {
  char microcontrollerId[10];
  char serverName[64];
  char apiKeyValue[64];
};

Config config;

const long sensorFetchInterval = 5000;
unsigned long previousMillisSensor = 0;

void setSensor(String name, float value)
{
  HTTPClient http;
  http.begin(wifi, String(config.serverName) + "setSensor.php");
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  String httpRequestData = "api_key=" + String(config.apiKeyValue) + "&name=" + name + "&value=" + String(value) + "";
  int httpCode = http.POST(httpRequestData);
  if (httpCode <= 0)
  {
    Serial.print("Error code: ");
    Serial.println(httpCode);
  }
}

void readSensors() {
  if (WiFi.status() == WL_CONNECTED)
  {
    setSensor(config.microcontrollerId + String("Humidity"), hdc1080.readHumidity());
    setSensor(config.microcontrollerId + String("Temp"), hdc1080.readTemperature());
  }
}

void printConfig() {
  Serial.println(config.microcontrollerId);
  Serial.println(config.serverName);
  Serial.println(config.apiKeyValue);
}

void loadConfiguration(Config &config) {
  StaticJsonDocument<512> doc;
  EepromStream eepromStream(JSON_ADDR, 256);

  DeserializationError error = deserializeJson(doc, eepromStream);
  if (error)
    Serial.println(F("Failed to read, using default configuration"));

  strlcpy(config.microcontrollerId,
          doc["controller_id"] | "room2",
          sizeof(config.microcontrollerId));
  strlcpy(config.serverName,
          doc["api_domain"] | "http://pwojtaszko.ddns.net/espapi/",
          sizeof(config.serverName));
  strlcpy(config.apiKeyValue,
          doc["api_key"] | "tPmAT5Ab3j7F9",
          sizeof(config.apiKeyValue));

  printConfig();
}

void saveConfiguration() {
  StaticJsonDocument<256> doc;

  doc["controller_id"] = config.microcontrollerId;
  doc["api_domain"] = config.serverName;
  doc["api_key"] = config.apiKeyValue;

  EepromStream eepromStream(JSON_ADDR, 256);

  if (serializeJson(doc, eepromStream) == 0) {
    Serial.println(F("Failed to write to EEPROM"));
  }
  EEPROM.commit();

  Serial.println(F("Print config..."));
  printConfig();
}

void saveConfigCallback () {
  Serial.println(F("Saving configuration..."));
  shouldSaveConfig = true;
}

void setup() {
  Serial.begin(9600);
  EEPROM.begin(512);
  hdc1080.begin(0x40);

  Serial.println(F("Loading configuration..."));
  loadConfiguration(config);

  wifiManager.setSaveConfigCallback(saveConfigCallback);
  wifiManager.setBreakAfterConfig(true);
  wifiManager.setConfigPortalTimeout(30);
  new (&hostTextBox) WiFiManagerParameter("api_domain", "API domain", config.serverName, 50);
  wifiManager.addParameter(&hostTextBox);
  new (&controllerIdTextBox) WiFiManagerParameter("controller_id", "Controller ID", config.microcontrollerId, 50);
  wifiManager.addParameter(&controllerIdTextBox);
  new (&apiKeyTextBox) WiFiManagerParameter("api_key", "HTTP API Key", config.apiKeyValue, 50);
  wifiManager.addParameter(&apiKeyTextBox);
  wifiManager.autoConnect("ROOM STATION 2");
  wifiManager.startWebPortal();
}

void loop() {
  if (shouldSaveConfig) {
    strlcpy(config.serverName,
          hostTextBox.getValue(),
          sizeof(config.serverName));
    strlcpy(config.microcontrollerId,
          controllerIdTextBox.getValue(),
          sizeof(config.microcontrollerId));
    strlcpy(config.apiKeyValue,
          apiKeyTextBox.getValue(),
          sizeof(config.apiKeyValue));
    wifiManager.startWebPortal();
  
    saveConfiguration();
    shouldSaveConfig = false;
  }
  wifiManager.process();
  unsigned long currentMillis = millis();

  if (currentMillis - previousMillisSensor >= sensorFetchInterval)
  {
    previousMillisSensor = currentMillis;
    readSensors();
  }
}
