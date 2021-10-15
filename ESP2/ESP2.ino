/*********
  Complete project details at https://randomnerdtutorials.com  
*********/

#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include "DHTesp.h"
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>

#define SEALEVELPRESSURE_HPA (1013.25)
#define DHTPin D4

Adafruit_BME280 bme; // I2C
DHTesp dht;

String microcontrollerId = String("room1");
/* Enter ssid & password of your WiFi inside double quotes */
const char *ssid = ""; //ENTER WIFI SSID
const char *password = ""; //ENTER WIFI PASSWORD
String serverName = ""; //PUT YOUR API DOMAIN
String apiKeyValue = ""; //ENTER API KEY
const long sensorFetchInterval = 5000;
unsigned long previousMillisSensor = 0;

unsigned long delayTime;

void setSensor(String name, float value)
{
  HTTPClient http;
  http.begin(serverName + "setSensor.php");
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
    setSensor(microcontrollerId + String("Temp"), bme.readTemperature());
    setSensor(microcontrollerId + String("Altitude"), bme.readAltitude(SEALEVELPRESSURE_HPA));
    setSensor(microcontrollerId + String("Pressure"), bme.readPressure() / 100.0F);
    setSensor(microcontrollerId + String("Humidity"), bme.readHumidity());
    setSensor(microcontrollerId + String("HumidityOut"), dht.getHumidity());
    setSensor(microcontrollerId + String("TempOut"), dht.getTemperature());
  }
}

void setup() {
  Serial.begin(9600);
  Serial.println(F("BME280 test"));

  bool status;

  status = bme.begin(0x76);  
  if (!status) {
    Serial.println("Could not find a valid BME280 sensor, check wiring!");
    while (1);
  }
  dht.setup(DHTPin, DHTesp::DHT22);
  delayTime = 1000;
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
