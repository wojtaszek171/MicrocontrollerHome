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
#include "ClosedCube_HDC1080.h"
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
#define RESET_EEPROM false

WiFiClient wifi;

int en1 = 0;
String start1 = "";
String stop1 = "";
String light1 = "";

int en2 = 0;
String start2 = "";
String stop2 = "";
String light2 = "";

int en3 = 0;
String start3 = "";
String stop3 = "";
String light3 = "";

int en4 = 0;
String start4 = "";
String stop4 = "";
String light4 = "";

OneWire oneWire(THERMOMETER);
DallasTemperature tempSensors(&oneWire);

ClosedCube_HDC1080 hdc1080;

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
  String lightModes;
  int currentMode;
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
    this->lightModes = "";
    this->currentMode = 1;
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
    if (getPinState() == HIGH){
      if (pin == RELAY1) {
        delay(5000);  // Required to initialize Aquael lamp (delay of 5s start with mode 1)
      }
      currentMode = 1;
      digitalWrite(pin, LOW);
    }
  }
  void off()
  {
    if (getPinState() == LOW){
      digitalWrite(pin, HIGH);
    }
  }
  void blink() {
    digitalWrite(pin, HIGH);
    delay(50);
    digitalWrite(pin, LOW);
    delay(50);
  }
  void changeLightMode(int newMode) // Aquael Leddy Day Night control (turn off and on change light mode)
  {
    delay(50);
    Serial.println("Changing mode:");
    Serial.print(currentMode);
    Serial.print(" -> ");
    Serial.println(newMode);
    if (currentMode == newMode) {
      return;
    }
    
    while (currentMode != newMode) {
      blink();
      if (currentMode + 1 >3) {
        currentMode = 1;
      } else {
        currentMode++;
      }
    }
  }
  void setTimes(bool _enabled, char *_start, char *_stop, char *_lightModes)
  {
    enabled = _enabled;
    start = _start;
    stop = _stop;
    lightModes = _lightModes;
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
  String getLightModes()
  {
    return lightModes;
  }
  int getPinState()
  {
    return digitalRead(pin);
  }
  void handleLightModes()
  {
    if (lightModes != "") {
      int currH = bcd2dec(mByte[2]);
      int currM = bcd2dec(mByte[1]);
      int prevMode = 0;
      int prevH = 0;
      int prevM = 0;
      int index = 0;
      bool isData = true;
      while(isData == true) {
        String lightModeStr = getValue(lightModes, '/', index);
        if (lightModeStr == NULL || lightModeStr == "") {
          isData = false;
          index = 0;
          lightModeStr = getValue(lightModes, '/', 0);
        }
        int modeMode = getValue(lightModeStr, ':', 0).toInt();
        int modeHour = getValue(lightModeStr, ':', 1).toInt();
        int modeMinutes = getValue(lightModeStr, ':', 2).toInt();

        if (prevMode != 0) {
          if (isEarlier(prevH, prevM, modeHour, modeMinutes))
          {
            if (!isEarlier(currH, currM, prevH, prevM) && isEarlier(currH, currM, modeHour, modeMinutes))
            {
              changeLightMode(prevMode);
              isData = false;
            }
          }
          else
          {
            if (!(isEarlier(currH, currM, prevH, prevM) && !isEarlier(currH, currM, modeHour, modeMinutes)))
            {
              changeLightMode(prevMode);
              isData = false;
            }
          }
        }

        prevMode = modeMode;
        prevH = modeHour;
        prevM = modeMinutes;
        index++;
      }
    }
  }
  void handleCurrentTime()
  {
    if (!enabled)
    {
      off();
      return;
    }
    
    if (start == "" || stop == "")
    {
      on();
      handleLightModes();
      return;
    }
    else
    {
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
          handleLightModes();
        }
        else
        {
          off();
        }
      }
      else
      {
        if (!isEarlier(currH, currM, startH, startM) && isEarlier(currH, currM, stopH, stopM))
        {
          on();
          handleLightModes();
        }
        else
        {
          off();
        }
      }
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
    http.begin(wifi, serverName + "getSockets.php");
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
        const char *lightModes = obj["lightModes"];
        const char *s1 = "socket1";
        const char *s2 = "socket2";
        const char *s3 = "socket3";
        const char *s4 = "socket4";

        if (strcmp(socketKey, s1) == 0)
        {
          socket1.setTimes(socketEnabled, (char *)socketStart, (char *)socketStop, (char *)lightModes);
        }
        else if (strcmp(socketKey, s2) == 0)
        {
          socket2.setTimes(socketEnabled, (char *)socketStart, (char *)socketStop, (char *)lightModes);
        }
        else if (strcmp(socketKey, s3) == 0)
        {
          socket3.setTimes(socketEnabled, (char *)socketStart, (char *)socketStop, (char *)lightModes);
        }
        else if (strcmp(socketKey, s4) == 0)
        {
          socket4.setTimes(socketEnabled, (char *)socketStart, (char *)socketStop, (char *)lightModes);
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

  float roomTemp = hdc1080.readTemperature();
  float roomHumidity = hdc1080.readHumidity();

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
  light1 = "";
  light2 = "";
  light3 = "";
  light4 = "";
}

int writeWord(int addrOffset, const String &strToWrite)
{
  byte len = strToWrite.length();
  EEPROM.write(addrOffset, len);
  for (int i = 0; i < len; i++)
  {
    EEPROM.write(addrOffset + 1 + i, strToWrite[i]);
  }
  EEPROM.commit();
  return addrOffset + 1 + len;
}

int readWord(int addrOffset, String *strToRead)
{
  int newStrLen = EEPROM.read(addrOffset);
  char data[newStrLen + 1];
  for (int i = 0; i < newStrLen; i++)
  {
    data[i] = EEPROM.read(addrOffset + 1 + i);
  }
  data[newStrLen] = '\0';
  *strToRead = String(data);
  return addrOffset + 1 + newStrLen;
}

void readEEPromData()
{
  clearEEPromData();
  int address = 0;

  EEPROM.get(address, en1);
  address += 6;
  EEPROM.get(address, en2);
  address += 6;
  EEPROM.get(address, en3);
  address += 6;
  EEPROM.get(address, en4);
  address += 6;

  int addressOffset;
  addressOffset = readWord(address, &start1);
  addressOffset = readWord(addressOffset, &stop1);
  addressOffset = readWord(addressOffset, &start2);
  addressOffset = readWord(addressOffset, &stop2);
  addressOffset = readWord(addressOffset, &start3);
  addressOffset = readWord(addressOffset, &stop3);
  addressOffset = readWord(addressOffset, &start4);
  addressOffset = readWord(addressOffset, &stop4);
  addressOffset = readWord(addressOffset, &light1);
  addressOffset = readWord(addressOffset, &light2);
  addressOffset = readWord(addressOffset, &light3);
  addressOffset = readWord(addressOffset, &light4);

  Serial.println("--");
  Serial.println(en1);
  Serial.println(en2);
  Serial.println(en3);
  Serial.println(en4);
  Serial.println(start1);
  Serial.println(stop1);
  Serial.println(start2);
  Serial.println(stop2);
  Serial.println(start3);
  Serial.println(stop3);
  Serial.println(start4);
  Serial.println(stop4);
  Serial.println(light1);
  Serial.println(light2);
  Serial.println(light3);
  Serial.println(light4);
}

void recoverLastSockets()
{
  socket1.setTimes(en1, (char *) start1.c_str(), (char *) stop1.c_str(),(char *) light1.c_str());
  socket2.setTimes(en2, (char *) start2.c_str(), (char *) stop2.c_str(),(char *) light2.c_str());
  socket3.setTimes(en3, (char *) start3.c_str(), (char *) stop3.c_str(),(char *) light3.c_str());
  socket4.setTimes(en4, (char *) start4.c_str(), (char *) stop4.c_str(),(char *) light4.c_str());
}

void saveSettingsToEEPROM()
{
  for (int i = 0; i < 512; i++) {
    EEPROM.write(i, '\0');
  }
  EEPROM.commit();
  delay(500);
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
  light1 = socket1.getLightModes();
  light2 = socket2.getLightModes();
  light3 = socket3.getLightModes();
  light4 = socket4.getLightModes();

  int address = 0;
  EEPROM.put(address, en1);
  address += 6;
  EEPROM.put(address, en2);
  address += 6;
  EEPROM.put(address, en3);
  address += 6;
  EEPROM.put(address, en4);
  address += 6;

  int addressOffset;
  addressOffset = writeWord(address, start1);
  addressOffset = writeWord(addressOffset, stop1);
  addressOffset = writeWord(addressOffset, start2);
  addressOffset = writeWord(addressOffset, stop2);
  addressOffset = writeWord(addressOffset, start3);
  addressOffset = writeWord(addressOffset, stop3);
  addressOffset = writeWord(addressOffset, start4);
  addressOffset = writeWord(addressOffset, stop4);
  addressOffset = writeWord(addressOffset, light1);
  addressOffset = writeWord(addressOffset, light2);
  addressOffset = writeWord(addressOffset, light3);
  addressOffset = writeWord(addressOffset, light4);
}

/* This function helps initialize program and set initial values */
void setup(void)
{
  EEPROM.begin(512);
  if ( RESET_EEPROM ) {
    for (int i = 0; i < 512; i++) {
      EEPROM.write(i, 0);
    }
    EEPROM.commit();
    delay(500);
  }
  digitalWrite(THERMOMETER, HIGH);
  Serial.begin(74880);
  while (!Serial)
  {}
  Wire.begin();
  hdc1080.begin(0x40);
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
