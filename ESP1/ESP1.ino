#include <TZ.h>
#include <WiFiClient.h>
#include <WiFiServer.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include "DHTesp.h"
#include <sntp.h>
#include <EEPROM.h>
#include <cstring>

/* define constants of various pins for easy accessibility */
#define RELAY1 D3
#define RELAY2 D5
#define RELAY3 D6
#define RELAY4 D7

#define THERMOMETER D4
#define bcd2dec(bcd_in) (bcd_in >> 4) * 10 + (bcd_in & 0x0f)
#define dec2bcd(dec_in) ((dec_in / 10) << 4) + (dec_in % 10)
#define MYTZ TZ_Europe_Warsaw
#define DHTPIN D0

uint en1 = 0;
String start1 = "";
String stop1 = "";

uint en2 = 0;
String start2 = "";
String stop2 = "";

uint en3 = 0;
String start3 = "";
String stop3 = "";

uint en4 = 0;
String start4 = "";
String stop4 = "";

OneWire oneWire(THERMOMETER);
DallasTemperature tempSensors(&oneWire);

DHTesp dht;

static timeval tv;
static timespec tp;
static time_t now;

byte mByte[0x07]; // holds the array from the DS3231 register
byte tByte[0x07]; // holds the array from the NTP server

/* Enter ssid & password of your WiFi inside double quotes */
const char *ssid = ""; //ENTER WIFI SSID
const char *password = ""; //ENTER WIFI PASSWORD

String serverName = ""; //PUT YOUR API DOMAIN
String apiKeyValue = ""; //ENTER API KEY

unsigned long previousMillisFetch = 0;
unsigned long previousMillisSensor = 0;
unsigned long previousMillisTime = 0;
const long dbFetchInterval = 10000;
const long sensorFetchInterval = 5000;
const long timeFetchInterval = 60000;

String getValue(String data, char separator, int index)
{
  int found = 0;
  int strIndex[] = {0, -1};
  int maxIndex = data.length() - 1;

  for (int i = 0; i <= maxIndex && found <= index; i++)
  {
    if (data.charAt(i) == separator || i == maxIndex)
    {
      found++;
      strIndex[0] = strIndex[1] + 1;
      strIndex[1] = (i == maxIndex) ? i + 1 : i;
    }
  }

  return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}

class Socket
{
private:
  byte pin;
  bool enabled;
  String start;
  String stop;
  bool isEarlier(int h1, int m1, int h2, int m2)
  {
    if (h1 < h2)
    {
      return true;
    }
    else if (h1 == h2)
    {
      if (m1 < m2)
      {
        return true;
      }
    }
    return false;
  };

public:
  Socket(byte pin)
  {
    this->pin = pin;
    this->enabled = false;
    this->start = "";
    this->stop = "";
    init();
  }
  void init()
  {
    Serial.print("init: ");
    Serial.println(pin);
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
  }
  void on()
  {
    Serial.print("on: ");
    Serial.println(pin);
    digitalWrite(pin, LOW);
  }
  void off()
  {
    Serial.print("off: ");
    Serial.println(pin);
    digitalWrite(pin, HIGH);
  }
  void setTimes(bool _enabled, char *_start, char *_stop)
  {
    enabled = _enabled;
    start = _start;
    stop = _stop;
  }
  byte getPin()
  {
    return pin;
  }
  String getStart()
  {
    return start;
  }
  String getStop()
  {
    return stop;
  }
  bool getEnabled()
  {
    return enabled;
  }
  int getPinState()
  {
    return digitalRead(pin);
  }
  void handleCurrentTime()
  {
    if (enabled)
    {
      if (start == "" || stop == "")
      {
        on();
        return;
      }
      int currH = bcd2dec(mByte[2]);
      int currM = bcd2dec(mByte[1]);
      int startH = getValue(start, ':', 0).toInt();
      int startM = getValue(start, ':', 1).toInt();
      int stopH = getValue(stop, ':', 0).toInt();
      int stopM = getValue(stop, ':', 1).toInt();

      if (isEarlier(stopH, stopM, startH, startM))
      {
        if (!(isEarlier(currH, currM, startH, startM) && !isEarlier(currH, currM, stopH, stopM)))
        {
          on();
        }
        else
        {
          Serial.print("off");
          off();
        }
      }
      else
      {
        if (!isEarlier(currH, currM, startH, startM) && isEarlier(currH, currM, stopH, stopM))
        {
          on();
        }
        else
        {
          off();
        }
      }
    }
    else
    {
      off();
    }
  }
};

Socket socket1(RELAY1);
Socket socket2(RELAY2);
Socket socket3(RELAY3);
Socket socket4(RELAY4);

void fetchSockets()
{
  if (WiFi.status() == WL_CONNECTED)
  {
    HTTPClient http;
    http.begin(serverName + "getSockets.php");
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    String httpRequestData = "api_key=" + apiKeyValue + "";
    int httpCode = http.POST(httpRequestData);
    if (httpCode > 0)
    { //Check the returning code
      DynamicJsonDocument doc(2048);
      char *char_arr;
      String str_obj(http.getString());
      char_arr = &str_obj[0];
      DeserializationError error = deserializeJson(doc, char_arr);
      if (error)
      {
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.f_str());
        return;
      }
      for (int i = 0; i < doc.size(); i++)
      {
        JsonObject obj = doc[i];
        const char *socketKey = obj["key"];
        const char *socketStart = obj["start"];
        const char *socketStop = obj["stop"];
        const int socketEnabled = obj["enabled"];
        const char *s1 = "socket1";
        const char *s2 = "socket2";
        const char *s3 = "socket3";
        const char *s4 = "socket4";

        if (strcmp(socketKey, s1) == 0)
        {
          socket1.setTimes(socketEnabled, (char *)socketStart, (char *)socketStop);
        }
        else if (strcmp(socketKey, s2) == 0)
        {
          socket2.setTimes(socketEnabled, (char *)socketStart, (char *)socketStop);
        }
        else if (strcmp(socketKey, s3) == 0)
        {
          socket3.setTimes(socketEnabled, (char *)socketStart, (char *)socketStop);
        }
        else if (strcmp(socketKey, s4) == 0)
        {
          socket4.setTimes(socketEnabled, (char *)socketStart, (char *)socketStop);
        }
      }
    }
    else
    {
      Serial.print("Error code: ");
      Serial.println(httpCode);
    }
    http.end(); //Close connection
  }
}

void setSensor(char *name, float value)
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

int intLength(int N)
{
  if (N < 0)
    return 1 + intLength(-N);
  else if (N < 10)
    return 1;
  else
    return 1 + intLength(N / 10);
}

void readSensors()
{
  tempSensors.requestTemperatures();
  float waterTemp = tempSensors.getTempCByIndex(0);

  float roomTemp = dht.getTemperature();
  float roomHumidity = dht.getHumidity();

  if (WiFi.status() == WL_CONNECTED)
  {
    setSensor("waterTemp", waterTemp);
    setSensor("roomAquariumTemp", roomTemp);
    setSensor("roomAquariumHumidity", roomHumidity);
  }
}

void setSocketsState()
{
  socket1.handleCurrentTime();
  socket2.handleCurrentTime();
  socket3.handleCurrentTime();
  socket4.handleCurrentTime();
}

void getRTCdatetime()
{
  Wire.beginTransmission(0x68);
  // Set device to start read reg 0
  Wire.write(0x00);
  Wire.endTransmission();

  // request 7 bytes from the DS3231 and release the I2C bus
  Wire.requestFrom(0x68, 0x07, true);

  int idx = 0;

  // read the first seven bytes from the DS3231 module into the array
  while (Wire.available())
  {

    byte input = Wire.read(); // read each byte from the register
    mByte[idx] = input;       // store each single byte in the array
    idx++;
  }
}

void getNTPDateTime()
{
  gettimeofday(&tv, nullptr);
  now = time(nullptr);
  const tm *tm = localtime(&now);
  tByte[0] = (int)tm->tm_sec;
  tByte[1] = (int)tm->tm_min;
  tByte[2] = (int)tm->tm_hour;
  tByte[3] = (int)tm->tm_wday;
  tByte[4] = (int)tm->tm_mday;
  tByte[5] = (int)tm->tm_mon + 1;
  tByte[6] = (int)tm->tm_year - 100;
}

void updateRTC()
{
  if (WiFi.status() == WL_CONNECTED)
  {
    getNTPDateTime();
    getRTCdatetime();
    if (mByte != tByte)
    {
      Wire.beginTransmission(0x68);
      // Set device to start read reg 0
      Wire.write(0x00);
      for (int idx = 0; idx < 7; idx++)
      {
        Wire.write(dec2bcd(tByte[idx]));
      }
      Wire.endTransmission();

      Serial.println(F("New DS3231 register content........\n"));
    }
  }
}

void clearEEPromData()
{
  en1 = 0;
  en2 = 0;
  en3 = 0;
  en4 = 0;
  start1 = "";
  start2 = "";
  start3 = "";
  start4 = "";
  stop1 = "";
  stop2 = "";
  stop3 = "";
  stop4 = "";
}

String readWord(int addr)
{
  String word;
  char readChar;
  int i = addr;

  while (readChar != '\0')
  {
    readChar = char(EEPROM.read(i));
    delay(10);
    i++;
    word += readChar;
  }

  return word;
}

void readEEPromData()
{
  clearEEPromData();
  EEPROM.get(0, en1);
  EEPROM.get(30, en2);
  EEPROM.get(60, en3);
  EEPROM.get(90, en4);
  start1 = readWord(10);
  stop1 = readWord(20);
  start2 = readWord(40);
  stop2 = readWord(50);
  start3 = readWord(70);
  stop3 = readWord(80);
  start4 = readWord(100);
  stop4 = readWord(110);
}

void recoverLastSockets()
{
  socket1.setTimes(en1, (char *) start1.c_str(), (char *) stop1.c_str());
  socket2.setTimes(en2, (char *) start2.c_str(), (char *) stop2.c_str());
  socket3.setTimes(en3, (char *) start3.c_str(), (char *) stop3.c_str());
  socket4.setTimes(en4, (char *) start4.c_str(), (char *) stop4.c_str());
}

void saveSettingsToEEPROM()
{
  clearEEPromData();
  start1 = socket1.getStart();
  stop1 = socket1.getStop();
  start2 = socket2.getStart();
  stop2 = socket2.getStop();
  start3 = socket3.getStart();
  stop3 = socket3.getStop();
  start4 = socket4.getStart();
  stop4 = socket4.getStop();
  en1 = socket1.getEnabled();
  en2 = socket2.getEnabled();
  en3 = socket3.getEnabled();
  en4 = socket4.getEnabled();
  EEPROM.put(0, en1);
  EEPROM.put(30, en2);
  EEPROM.put(60, en3);
  EEPROM.put(90, en4);
  EEPROM.put(10, start1);
  EEPROM.put(20, stop1);
  EEPROM.put(40, start2);
  EEPROM.put(50, stop2);
  EEPROM.put(70, start3);
  EEPROM.put(80, stop3);
  EEPROM.put(100, start4);
  EEPROM.put(110, stop4);
  EEPROM.commit();

  readEEPromData();
}

/* This function helps initialize program and set initial values */
void setup(void)
{
  digitalWrite(THERMOMETER, HIGH);
  Serial.begin(9600);
  while (!Serial)
  {}
  Wire.begin();
  EEPROM.begin(512);
  digitalWrite(DHTPIN, HIGH);
  dht.setup(DHTPIN, DHTesp::DHT22);
  tempSensors.begin();
  delay(10);
  WiFi.begin(ssid, password); /* connect to WiFi */
  configTime(MYTZ, "pool.ntp.org");
  readEEPromData();
  recoverLastSockets();
}

void loop(void)
{
  unsigned long currentMillis = millis();

  if (currentMillis - previousMillisFetch >= dbFetchInterval)
  {
    previousMillisFetch = currentMillis;
    getRTCdatetime();
    fetchSockets();
    saveSettingsToEEPROM();
    setSocketsState();
  }

  if (currentMillis - previousMillisSensor >= sensorFetchInterval)
  {
    previousMillisSensor = currentMillis;
    readSensors();
  }

  if (currentMillis - previousMillisTime >= timeFetchInterval)
  {
    previousMillisTime = currentMillis;
    updateRTC();
  }
}
