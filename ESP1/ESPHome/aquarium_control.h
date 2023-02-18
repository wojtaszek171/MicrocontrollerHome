#include "esphome.h"
#include <TZ.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <sntp.h>
#include <EEPROM.h>
#include <cstring>


#define bcd2dec(bcd_in) (bcd_in >> 4) * 10 + (bcd_in & 0x0f)
#define dec2bcd(dec_in) ((dec_in / 10) << 4) + (dec_in % 10)

int en1 = 0;
String light1 = "";

int en2 = 0;
String light2 = "";

int en3 = 0;
String light3 = "";

int en4 = 0;
String light4 = "";

static timeval tv;
static timespec tp;
static time_t now;

byte mByte[0x07]; // holds the array from the DS3231 register
byte tByte[0x07]; // holds the array from the NTP server

String serverName; //PUT YOUR API DOMAIN
String apiKeyValue = "tPmAT5Ab3j7F9"; //ENTER API KEY

const long dbFetchInterval = 5000;
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
    this->lightModes = ""; // stringified JSON { [key: String]: string }
    this->currentMode = 0;
    init();
  }
  void init()
  {
    ESP_LOGD("custom", "init: %d", pin);
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
  }
  void on()
  {
    if (getPinState() == HIGH){
      currentMode = 1;
      digitalWrite(pin, LOW);
    }
  }
  void off()
  {
    if (getPinState() == LOW){
      digitalWrite(pin, HIGH);
      currentMode = 0;
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
    if (currentMode == newMode) {
      return;
    }

    ESP_LOGD("custom", "Changing mode: %d -> %d", currentMode, newMode);

    if (newMode == 0) {
      off();
      return;
    }

    while (currentMode != newMode) {
      delay(50);
      if (currentMode == 0) {
        on(); // on sets current mode to 1 already
        continue;
      } else {
        blink();
      }
      if (currentMode + 1 >3) {
        currentMode = 1;
      } else {
        currentMode++;
      }
    }
  }
  void setTimes(bool _enabled, char *_lightModes)
  {
    enabled = _enabled;
    lightModes = _lightModes;
  }
  byte getPin()
  {
    return pin;
  }
  bool getEnabled()
  {
    return enabled;
  }
  void setEnabled(bool _enabled)
  {
    enabled = _enabled;
  }
  String getLightModes()
  {
    return lightModes;
  }
  void addLightMode(char * _hour, int mode)
  {
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, lightModes);
    doc[_hour] = mode;

    String _lightModes;
    serializeJson(doc, _lightModes);
    lightModes = _lightModes;
  }
  void removeLightMode(char * _hour)
  {
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, lightModes);
    doc.remove(_hour);

    String _lightModes;
    serializeJson(doc, _lightModes);
    lightModes = _lightModes;
  }
  void setLightMdes(char * _lightModes)
  {
    lightModes = _lightModes;
  }
  int getPinState()
  {
    return digitalRead(pin);
  }
  void handleLightModes()
  {
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, lightModes);
    JsonObject obj = doc.as<JsonObject>();

    if (obj.size() == 0) {
      on();
      return;
    }

    if (obj.size() == 1) {
      JsonObject::iterator firstEl = obj.begin(); // get first element and enable
      changeLightMode(firstEl -> value().as<const int>());
      return;
    }

    int currH = bcd2dec(tByte[2]); // TODO: replace with mByte
    int currM = bcd2dec(tByte[1]);

    int prevMode = -1;
    int prevH = 0;
    int prevM = 0;

    if (obj.size() > 1) {
      JsonObject::iterator lastEl = obj.begin(); // last element to compare with first
      lastEl += obj.size() - 1;
      prevMode = lastEl -> value().as<const int>();
      prevH = getValue(lastEl -> key().c_str(), ':', 0).toInt();
      prevM = getValue(lastEl -> key().c_str(), ':', 1).toInt();
    }

    for (JsonPair fullMode : obj) {
      int modeMode = fullMode.value().as<const int>();
      int modeHour = getValue(fullMode.key().c_str(), ':', 0).toInt();
      int modeMinutes = getValue(fullMode.key().c_str(), ':', 1).toInt();

      if (prevMode != -1) {
        if (isEarlier(prevH, prevM, modeHour, modeMinutes))
        {
          if (!isEarlier(currH, currM, prevH, prevM) && isEarlier(currH, currM, modeHour, modeMinutes))
          {
            changeLightMode(prevMode);
            return;
          }
        }
        else
        {
          if (!(isEarlier(currH, currM, prevH, prevM) && !isEarlier(currH, currM, modeHour, modeMinutes)))
          {
            changeLightMode(prevMode);
            return;
          }
        }
      }

      prevMode = modeMode;
      prevH = modeHour;
      prevM = modeMinutes;
    }
  }
  void handleCurrentTime()
  {
    if (!enabled)
    {
      off();
      return;
    }
    
    handleLightModes();
  }
};

Socket socket1(0);
Socket socket2(14);
Socket socket3(12);
Socket socket4(13);

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

void clearEEPromData()
{
  en1 = 0;
  en2 = 0;
  en3 = 0;
  en4 = 0;
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
  addressOffset = readWord(address, &light1);
  addressOffset = readWord(addressOffset, &light2);
  addressOffset = readWord(addressOffset, &light3);
  addressOffset = readWord(addressOffset, &light4);

  ESP_LOGD("custom", "--");
  ESP_LOGD("custom", "%d", en1);
  ESP_LOGD("custom", "%d", en2);
  ESP_LOGD("custom", "%d", en3);
  ESP_LOGD("custom", "%d", en4);
  ESP_LOGD("custom", "%d", light1);
  ESP_LOGD("custom", "%d", light2);
  ESP_LOGD("custom", "%d", light3);
  ESP_LOGD("custom", "%d", light4);
}

void recoverLastSockets()
{
  socket1.setTimes(en1,(char *) light1.c_str());
  socket2.setTimes(en2,(char *) light2.c_str());
  socket3.setTimes(en3,(char *) light3.c_str());
  socket4.setTimes(en4,(char *) light4.c_str());

  // update switch states by ids defined in yaml file
  id(s1_switch).publish_state(socket1.getEnabled());
  id(s2_switch).publish_state(socket2.getEnabled());
  id(s3_switch).publish_state(socket3.getEnabled());
  id(s4_switch).publish_state(socket4.getEnabled());
}

void saveSettingsToEEPROM()
{
  for (int i = 0; i < 512; i++) {
    EEPROM.write(i, '\0');
  }
  EEPROM.commit();
  delay(500);
  clearEEPromData();
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
  addressOffset = writeWord(address, light1);
  addressOffset = writeWord(addressOffset, light2);
  addressOffset = writeWord(addressOffset, light3);
  addressOffset = writeWord(addressOffset, light4);
}

void setSocketNewSchedule(int id, char *lightModes)
{
  switch (id) {
    case 1:
      socket1.setLightMdes(lightModes);
      break;
    case 2:
      socket2.setLightMdes(lightModes);
      break;
    case 3:
      socket3.setLightMdes(lightModes);
      break;
    case 4:
      socket4.setLightMdes(lightModes);
      break;
    default:
      ESP_LOGD("custom", "Wrong socket id specified!");
      break;
  }

  saveSettingsToEEPROM();
}

void setSocketNewEnabled(int id, bool enabled)
{
  switch (id) {
    case 1:
      socket1.setEnabled(enabled);
      break;
    case 2:
      socket2.setEnabled(enabled);
      break;
    case 3:
      socket3.setEnabled(enabled);
      break;
    case 4:
      socket4.setEnabled(enabled);
      break;
    default:
      ESP_LOGD("custom", "Wrong socket id specified!");
      break;
  }

  saveSettingsToEEPROM();
}

void removeSocketSchedule(int id, char *hour)
{
  switch (id) {
    case 1:
      socket1.removeLightMode(hour);
      break;
    case 2:
      socket2.removeLightMode(hour);
      break;
    case 3:
      socket3.removeLightMode(hour);
      break;
    case 4:
      socket4.removeLightMode(hour);
      break;
    default:
      ESP_LOGD("custom", "Wrong socket id specified!");
      break;
  }

  saveSettingsToEEPROM();
}

void addSocketSchedule(int id, char *hour, int mode)
{
  switch (id) {
    case 1:
      socket1.addLightMode(hour, mode);
      break;
    case 2:
      socket2.addLightMode(hour, mode);
      break;
    case 3:
      socket3.addLightMode(hour, mode);
      break;
    case 4:
      socket4.addLightMode(hour, mode);
      break;
    default:
      ESP_LOGD("custom", "Wrong socket id specified!");
      break;
  }

  saveSettingsToEEPROM();
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

class CustomTextSensor : public PollingComponent, public TextSensor {
  public:
    CustomTextSensor() : PollingComponent(5000) {}
    float get_setup_priority() const override { return esphome::setup_priority::LATE; }
    TextSensor *socket1_modes_sensor = new TextSensor();
    TextSensor *socket2_modes_sensor = new TextSensor();
    TextSensor *socket3_modes_sensor = new TextSensor();
    TextSensor *socket4_modes_sensor = new TextSensor();

    void setup() override {}

    void update() override {
      socket1_modes_sensor->publish_state(socket1.getLightModes().c_str());
      socket2_modes_sensor->publish_state(socket2.getLightModes().c_str());
      socket3_modes_sensor->publish_state(socket3.getLightModes().c_str());
      socket4_modes_sensor->publish_state(socket4.getLightModes().c_str());
    };
};

class CustomBinarySensor : public PollingComponent, public BinarySensor {
  public:
    CustomBinarySensor() : PollingComponent(5000) {}
    float get_setup_priority() const override { return esphome::setup_priority::LATE; }
    BinarySensor *socket1_enabled_sensor = new BinarySensor();
    BinarySensor *socket2_enabled_sensor = new BinarySensor();
    BinarySensor *socket3_enabled_sensor = new BinarySensor();
    BinarySensor *socket4_enabled_sensor = new BinarySensor();

    void setup() override {}
    void update() override {
      socket1_enabled_sensor->publish_state(socket1.getEnabled());
      socket2_enabled_sensor->publish_state(socket2.getEnabled());
      socket3_enabled_sensor->publish_state(socket3.getEnabled());
      socket4_enabled_sensor->publish_state(socket4.getEnabled());
    }
    
};

class SocketSwitch : public Component, public Switch {
  private:
    int id;
  public:
    SocketSwitch(int id)
    {
      this->id = id;
      init(false);
    }
    void init(bool _state)
    {
      write_state(_state);
    }
    float get_setup_priority() const override { return esphome::setup_priority::LATE; }
    void setup() override {}
    void write_state(bool state) override {
      setSocketNewEnabled(id, state);
      publish_state(state);
    }
};

class MyCustomComponent : public PollingComponent, public CustomAPIDevice {
  public:
    MyCustomComponent() : PollingComponent(5000) {}
    float get_setup_priority() const override { return esphome::setup_priority::LATE; }

    void on_schedule_change(int id, std::string modes) {
      ESP_LOGD("custom", "Updating socket data");
      setSocketNewSchedule(id, const_cast<char*>(modes.c_str()));
    }

    void on_schedule_remove(int id, std::string hour) {
      ESP_LOGD("custom", "Removing socket schedule");
      removeSocketSchedule(id, const_cast<char*>(hour.c_str()));
    }

    void on_schedule_add(int id, std::string hour, int mode) {
      ESP_LOGD("custom", "Adding socket schedule");
      addSocketSchedule(id, const_cast<char*>(hour.c_str()), mode);
    }

    void setup() override
    {
      register_service(&MyCustomComponent::on_schedule_change, "update_schedule",
        {"socket_num", "modes"});

      register_service(&MyCustomComponent::on_schedule_remove, "remove_schedule",
        {"socket_num", "hour"});

      register_service(&MyCustomComponent::on_schedule_add, "add_schedule",
        {"socket_num", "hour", "mode"});

      EEPROM.begin(512);

      while (!Serial)
      {}
      Wire.begin();
      delay(10);
      configTime("TZ_Europe_Warsaw", "pool.ntp.org");
      readEEPromData();
      recoverLastSockets();
      delay(5000);
    }

    void update() override
    {
        getRTCdatetime();
        saveSettingsToEEPROM(); // save values for later use
        setSocketsState();  // update sockets with new values

        updateRTC();
    }
};
