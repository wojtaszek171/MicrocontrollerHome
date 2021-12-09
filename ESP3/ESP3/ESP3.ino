#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include "ClosedCube_HDC1080.h"

ClosedCube_HDC1080 hdc1080;
WiFiClient wifi;

String microcontrollerId = String("room4");
/* Enter ssid & password of your WiFi inside double quotes */
const char *ssid = ""; //ENTER WIFI SSID
const char *password = ""; //ENTER WIFI PASSWORD
String serverName = ""; //PUT YOUR API DOMAIN
String apiKeyValue = ""; //ENTER API KEY
const long sensorFetchInterval = 5000;
unsigned long previousMillisSensor = 0;

void setSensor(String name, float value)
{
  HTTPClient http;
  http.begin(wifi, serverName + "setSensor.php");
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  String httpRequestData = "api_key=" + apiKeyValue + "&name=" + name + "&value=" + value + "";
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
    setSensor(microcontrollerId + String("Humidity"), hdc1080.readHumidity());
    setSensor(microcontrollerId + String("Temp"), hdc1080.readTemperature());
  }
}

void setup() {
  Serial.begin(9600);

  hdc1080.begin(0x40);
  WiFi.begin(ssid, password); /* connect to WiFi */
}

void loop() { 
  unsigned long currentMillis = millis();

  if (currentMillis - previousMillisSensor >= sensorFetchInterval)
  {
    previousMillisSensor = currentMillis;
    readSensors();
  }
}
