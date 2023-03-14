#include "esphome.h"
#include <TZ.h>
#include <Wire.h>
#include <sntp.h>
#include <EEPROM.h>
#include <cstring>
#include <sstream>
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>

#define bcd2dec(bcd_in) (bcd_in >> 4) * 10 + (bcd_in & 0x0f)
#define dec2bcd(dec_in) ((dec_in / 10) << 4) + (dec_in % 10)

int EEPROM_SIZE = 1024; // high value to secure cases when somebody sets many schedules

int en1 = 0;
int scheduleEnabled1 = 0;
std::string light1 = "";

int en2 = 0;
int scheduleEnabled2 = 0;
std::string light2 = "";

int en3 = 0;
int scheduleEnabled3 = 0;
std::string light3 = "";

int en4 = 0;
int scheduleEnabled4 = 0;
std::string light4 = "";

static timeval tv;
static timespec tp;
static time_t now;

byte mByte[0x07]; // holds the array from the DS3231 register
byte tByte[0x07]; // holds the array from the NTP server

std::string getValue(std::string data, char separator, int index)
{
  int found = 0;
  int strIndex[] = {0, -1};
  int maxIndex = data.length() - 1;

  for (int i = 0; i <= maxIndex && found <= index; i++)
  {
    if (data.at(i) == separator || i == maxIndex)
    {
      found++;
      strIndex[0] = strIndex[1] + 1;
      strIndex[1] = (i == maxIndex) ? i + 1 : i;
    }
  }

  return found > index ? data.substr(strIndex[0], strIndex[1]) : "";
}

class Socket
{
private:
  byte pin;
  bool enabled;
  bool scheduleEnabled;
  std::map<std::string, int> lightModes;
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
    this->scheduleEnabled = false;
    this->enabled = false;
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
  void restoreSocket(bool _enabled, char* _lightModes)
  {
    enabled = _enabled;

    setLightModes(_lightModes);
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
  bool getScheduleEnabled()
  {
    return scheduleEnabled;
  }
  void setScheduleEnabled(bool _scheduleEnabled)
  {
    scheduleEnabled = _scheduleEnabled;
  }
  std::map<std::string, int> getLightModes()
  {
    return lightModes;
  }
  std::string getStringifiedLightModes()
  {
    DynamicJsonDocument doc(1024);
    std::string res = "";
    for (std::map<std::string, int>::iterator it = lightModes.begin(); it != lightModes.end(); it++) {
        doc[it -> first] = std::to_string(it -> second);
    }

    std::string serializedDoc;
    serializeJson(doc, serializedDoc);
    return serializedDoc;
  }

  void addLightMode(std::string _hour, int mode)
  {
    lightModes[_hour] = mode;
  }
  void removeLightMode(std::string _hour)
  {
    lightModes.erase(_hour);
  }
  void setLightModes(char* _lightModes)
  {
    if (_lightModes == "") {
      return;
    }

    DynamicJsonDocument doc(1024);
    deserializeJson(doc, _lightModes);

    std::map<std::string, int> newModes;

    JsonObject obj = doc.as<JsonObject>();

    for (JsonPair fullMode : obj) {
      newModes[(char*) fullMode.key().c_str()] = fullMode.value().as<const int>();
    }

    lightModes = newModes;
  }
  int getPinState()
  {
    return digitalRead(pin);
  }
  void handleLightModes()
  {
    if (lightModes.size() == 0) {
      on();
      return;
    }

    if (lightModes.size() == 1) {
      std::map<std::string,int>::iterator firstEl = lightModes.begin(); // get first element and enable
      changeLightMode(firstEl -> second);
      return;
    }

    int currH = 16; // bcd2dec(tByte[2]); // TODO: replace with mByte
    int currM = 0; // bcd2dec(tByte[1]);

    int prevMode = -1;
    int prevH = 0;
    int prevM = 0;

    if (lightModes.size() > 1) {
      std::map<std::string,int>::iterator lastEl = lightModes.end(); // last element to compare with first
      --lastEl;
      prevMode = lastEl -> second;
      prevH = std::stoi(getValue(lastEl -> first, ':', 0));
      prevM = std::stoi(getValue(lastEl -> first, ':', 1));
    }

    for (std::map<std::string,int>::iterator it = lightModes.begin(); it != lightModes.end(); ++it) {
      int modeMode = it->second;
      int modeHour = std::stoi(getValue(it->first, ':', 0));
      int modeMinutes = std::stoi(getValue(it->first, ':', 1));

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

    if (!scheduleEnabled) {
      on();
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
  scheduleEnabled1 = 0;
  scheduleEnabled2 = 0;
  scheduleEnabled3 = 0;
  scheduleEnabled4 = 0;
  light1 = "";
  light2 = "";
  light3 = "";
  light4 = "";
}

int writeWord(int addrOffset, std::string strToWrite)
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

int readWord(int addrOffset, std::string *strToRead)
{
  int newStrLen = EEPROM.read(addrOffset);
  char data[newStrLen + 1];
  for (int i = 0; i < newStrLen; i++)
  {
    data[i] = EEPROM.read(addrOffset + 1 + i);
  }
  data[newStrLen] = '\0';
  *strToRead = std::string(data);
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

  EEPROM.get(address, scheduleEnabled1);
  address += 6;
  EEPROM.get(address, scheduleEnabled2);
  address += 6;
  EEPROM.get(address, scheduleEnabled3);
  address += 6;
  EEPROM.get(address, scheduleEnabled4);
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

void restoreLastSockets()
{
  socket1.restoreSocket(en1,(char *) light1.c_str());
  socket2.restoreSocket(en2,(char *) light2.c_str());
  socket3.restoreSocket(en3,(char *) light3.c_str());
  socket4.restoreSocket(en4,(char *) light4.c_str());

  // update switch states by ids defined in yaml file
  id(s1_enable_switch).publish_state(socket1.getEnabled());
  id(s2_enable_switch).publish_state(socket2.getEnabled());
  id(s3_enable_switch).publish_state(socket3.getEnabled());
  id(s4_enable_switch).publish_state(socket4.getEnabled());

  id(s1_scheduleEnabled_switch).publish_state(socket1.getScheduleEnabled());
  id(s2_scheduleEnabled_switch).publish_state(socket2.getScheduleEnabled());
  id(s3_scheduleEnabled_switch).publish_state(socket3.getScheduleEnabled());
  id(s4_scheduleEnabled_switch).publish_state(socket4.getScheduleEnabled());
}

void updateEEPROMVariablesWithSocketValues()
{
  clearEEPromData();
  en1 = socket1.getEnabled();
  en2 = socket2.getEnabled();
  en3 = socket3.getEnabled();
  en4 = socket4.getEnabled();
  scheduleEnabled1 = socket1.getScheduleEnabled();
  scheduleEnabled2 = socket2.getScheduleEnabled();
  scheduleEnabled3 = socket3.getScheduleEnabled();
  scheduleEnabled4 = socket4.getScheduleEnabled();
  light1 = socket1.getStringifiedLightModes();
  light2 = socket2.getStringifiedLightModes();
  light3 = socket3.getStringifiedLightModes();
  light4 = socket4.getStringifiedLightModes();
}

void saveSettingsToEEPROM()
{
  for (int i = 0; i < EEPROM_SIZE; i++) {
    EEPROM.write(i, '\0');
  }
  EEPROM.commit();
  delay(500);

  int address = 0;
  EEPROM.put(address, en1);
  address += 6;
  EEPROM.put(address, en2);
  address += 6;
  EEPROM.put(address, en3);
  address += 6;
  EEPROM.put(address, en4);
  address += 6;

  EEPROM.put(address, scheduleEnabled1);
  address += 6;
  EEPROM.put(address, scheduleEnabled2);
  address += 6;
  EEPROM.put(address, scheduleEnabled3);
  address += 6;
  EEPROM.put(address, scheduleEnabled4);
  address += 6;

  int addressOffset;
  addressOffset = writeWord(address, light1);
  addressOffset = writeWord(addressOffset, light2);
  addressOffset = writeWord(addressOffset, light3);
  addressOffset = writeWord(addressOffset, light4);
}

Socket getSocketById(int id)
{
  switch (id) {
    case 1:
      return socket1;
    case 2:
      return socket2;
    case 3:
      return socket3;
    case 4:
      return socket4;
    default:
      return socket1; // defaults to socket1 for now
  }
}

void setSocketFullSchedule(int id, char *lightModes)
{
  switch (id) {
    case 1:
      socket1.setLightModes(lightModes);
      break;
    case 2:
      socket2.setLightModes(lightModes);
      break;
    case 3:
      socket3.setLightModes(lightModes);
      break;
    case 4:
      socket4.setLightModes(lightModes);
      break;
    default:
      ESP_LOGD("custom", "Wrong socket id specified!");
      break;
  }
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
}

void setSocketNewScheduleEnabled(int id, bool scheduleEnabled)
{
  switch (id) {
    case 1:
      socket1.setScheduleEnabled(scheduleEnabled);
      break;
    case 2:
      socket2.setScheduleEnabled(scheduleEnabled);
      break;
    case 3:
      socket3.setScheduleEnabled(scheduleEnabled);
      break;
    case 4:
      socket4.setScheduleEnabled(scheduleEnabled);
      break;
    default:
      ESP_LOGD("custom", "Wrong socket id specified!");
      break;
  }
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
}

void setSocketSchedule(int id, char *hour, int mode)
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
    TextSensor *socket1_modes_sensor = new TextSensor();
    TextSensor *socket2_modes_sensor = new TextSensor();
    TextSensor *socket3_modes_sensor = new TextSensor();
    TextSensor *socket4_modes_sensor = new TextSensor();

    void setup() override {}

    void update() override {
      socket1_modes_sensor->publish_state(socket1.getStringifiedLightModes());
      socket2_modes_sensor->publish_state(socket2.getStringifiedLightModes());
      socket3_modes_sensor->publish_state(socket3.getStringifiedLightModes());
      socket4_modes_sensor->publish_state(socket4.getStringifiedLightModes());
    };
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
    void setup() override {}
    void write_state(bool state) override {
      setSocketNewEnabled(id, state);
      publish_state(state);
    }
};

class SocketScheduleEnabledSwitch : public Component, public Switch {
  private:
    int id;
  public:
    SocketScheduleEnabledSwitch(int id)
    {
      this->id = id;
      init(false);
    }
    void init(bool _state)
    {
      write_state(_state);
    }
    void setup() override {}
    void write_state(bool state) override {
      setSocketNewScheduleEnabled(id, state);
      publish_state(state);
    }
};

class MyCustomComponent : public PollingComponent, public CustomAPIDevice {
  public:
    float get_setup_priority() const override { return esphome::setup_priority::LATE; }
    MyCustomComponent() : PollingComponent(5000) {}
    void on_schedule_change(int id, std::string modes) {
      ESP_LOGD("custom", "Updating socket data");
      setSocketFullSchedule(id, const_cast<char*>(modes.c_str()));
    }

    void on_schedule_remove(int id, std::string hour) {
      ESP_LOGD("custom", "Removing socket schedule");
      removeSocketSchedule(id, const_cast<char*>(hour.c_str()));
    }

    void on_schedule_add(int id, std::string hour, int mode) {
      ESP_LOGD("custom", "Adding socket schedule");
      setSocketSchedule(id, const_cast<char*>(hour.c_str()), mode);
    }

    void on_device_reset() {
      clearEEPromData();
      saveSettingsToEEPROM();
      ESP.restart();
    }

    void setup() override
    {
      register_service(&MyCustomComponent::on_device_reset, "reset_device_memory");

      register_service(&MyCustomComponent::on_schedule_change, "update_schedule",
        {"socket_num", "modes"});

      register_service(&MyCustomComponent::on_schedule_remove, "remove_schedule",
        {"socket_num", "hour"});

      register_service(&MyCustomComponent::on_schedule_add, "add_schedule",
        {"socket_num", "hour", "mode"});

      EEPROM.begin(EEPROM_SIZE);

      while (!Serial)
      {}
      Wire.begin();
      delay(10);
      configTime("TZ_Europe_Warsaw", "pool.ntp.org");
      readEEPromData();
      restoreLastSockets();
      delay(5000);
    }

    void update() override
    {
        getRTCdatetime();
        updateEEPROMVariablesWithSocketValues();
        saveSettingsToEEPROM(); // save values for later use
        setSocketsState();  // update sockets with new values

        updateRTC();
    }
};

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
  <head>
    <title>ESP32 WEB SERVER</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <link rel="icon" href="data:,">
    <style>
      html {font-family: Arial; display: inline-block; text-align: center;}
      body {margin: 0; background-color: #202324; color: #d1d1d1;}
      .socket-row:nth-child(odd) {background-color: #26292a;}
      .socket-component {display: flex; flex-direction: row; align-items: center; flex-wrap: wrap; margin: 0 auto; padding-top: 15px; padding-bottom: 15px; max-width: 480px;}
      .socket-name {padding-right: 5px; margin: 0 auto; margin-bottom: 10px;}
      .socket-name > span {font-weight: bold; margin-left: 2px;}
      .socket-options label, .socket-options input {cursor: pointer;}
      .socket-options input[type="checkbox"] {appearance: none; -webkit-appearance: none; height: 13px; width: 13px; position: relative; transition: 0.10s; background-color: #b11a1a; text-align: center; font-weight: 600; color: white; border-radius: 3px; outline: none;}
      .socket-options input[type="checkbox"] {float: right; margin-left: 10px;}
      .socket-options input[type="checkbox"]:checked {background-color: #0E9700;}
      .light-modes {display: flex; flex-direction: column; align-items: center; margin: 0 auto;}
      .light-modes div {border: 1px solid black; background: #fff; border-radius: 5px; padding: 2px; max-width:  300px; display: flex; flex-direction: row;}
      .light-modes div span {flex: 1; display: flex; align-items: center;}
      .light-modes input {max-width: 100px; border-width: 0 1px 0 0px;}
      .light-modes select {border: none;}
      .light-modes button {width: 25px; height: 25px; border: 0; color: #b11a1a; font-weight: bold; line-height: 10px; cursor: pointer;}
    </style>
  </head>
  <body>
    <h2>Aquarium control</h2>
    <div class="sockets">
      <div class="socket-row">
        <div class="socket-component">
          <div class="socket-name">
            <span>Socket 1</span>
            <div class="socket-options">
              %SOCKET1OPTIONS%
            </div>
          </div>
          <div class="light-modes">
            %SOCKET1MODES%
          </div>
        </div>
      </div>
      <div class="socket-row">
        <div class="socket-component">
          <div class="socket-name">
            <span>Socket 2</span>
            <div class="socket-options">
              %SOCKET2OPTIONS%
            </div>
          </div>
          <div class="light-modes">
            %SOCKET2MODES%
          </div>
        </div>
      </div>
      <div class="socket-row">
        <div class="socket-component">
          <div class="socket-name">
            <span>Socket 3</span>
            <div class="socket-options">
              %SOCKET3OPTIONS%
            </div>
          </div>
          <div class="light-modes">
            %SOCKET3MODES%
          </div>
        </div>
      </div>
      <div class="socket-row">
        <div class="socket-component">
          <div class="socket-name">
            <span>Socket 4</span>
            <div class="socket-options">
              %SOCKET4OPTIONS%
            </div>
          </div>
          <div class="light-modes">
            %SOCKET4MODES%
          </div>
        </div>
      </div>
    </div>
    <script>
      function toggleCheckbox(element) {
        var xhr = new XMLHttpRequest();
        if(element.checked){ xhr.open("GET", "/update?output="+element.id+"&state=1", true); }
        else { xhr.open("GET", "/update?output="+element.id+"&state=0", true); }
        xhr.send();
      }
      function openPortal() {
        var xhr = new XMLHttpRequest();
        xhr.open("GET", "/wifi", true);
        xhr.send();
      }
    </script>
  </body>
  </html>
  )rawliteral";

String returnOptionsHTML(int id){
  Socket socketById = getSocketById(id);
  String options = "<hr><div><label>Enabled<input type=\"checkbox\"";
  if (socketById.getEnabled()) {
    options += " checked";
  }
  options += "/></label></div><hr><div><label>Schedule<input type=\"checkbox\"";
  if (socketById.getScheduleEnabled()){
    options += " checked";
  }
  options += "/></label></div>";

  return options;
}

String renderModeHTML(int id, int mode, String time) {
  String modeHTML = "";
  modeHTML += "<div><span><input type=\"time\" value=\"" + time + "\"/></span><span><select>";
  modeHTML += "<option value=\"0\">OFF</option>";
  modeHTML += "<option value=\"1\">ON</option>";
  modeHTML += "<option value=\"2\">Evening (Aquael)</option>";
  modeHTML += "<option value=\"3\">Night (Aquael)</option>";
  modeHTML += "</select></span><button>x</button></div>";
  return modeHTML;
}

String returnModesHTML(int id){
  Socket socketById = getSocketById(id);
  String modesHTML = "";
  std::map<std::string, int> lightModes = socketById.getLightModes();
  
    for (std::map<std::string,int>::iterator it = lightModes.begin(); it != lightModes.end(); ++it) {
      int modeMode = it->second;
      String modeTime = String(it->first.c_str());

      modesHTML += renderModeHTML(id, modeMode, modeTime);
    }

  return modesHTML;
}

String processor(const String& var){
    if (var == "SOCKET1OPTIONS")
      return returnOptionsHTML(1);
    if (var == "SOCKET2OPTIONS")
      return returnOptionsHTML(2);
    if (var == "SOCKET3OPTIONS")
      return returnOptionsHTML(3);
    if (var == "SOCKET4OPTIONS")
      return returnOptionsHTML(4);
    if (var == "SOCKET1MODES")
      return returnModesHTML(1);
    if (var == "SOCKET2MODES")
      return returnModesHTML(2);
    if (var == "SOCKET3MODES")
      return returnModesHTML(3);
    if (var == "SOCKET4MODES")
      return returnModesHTML(4);
  return String();
}

AsyncWebServer server(80);
bool active;
class WebComponent : public Component, public AsyncWebHandler { // use own web server, esphome server results with OOM
  public:
    String header;
    bool captive;
    float get_setup_priority() const override { return esphome::setup_priority::LATE; }

    void setup() override
    {
      while(captive_portal::global_captive_portal->is_active()){
        ESP_LOGD("custom", "Waiting for captive to be closed");
      }

      ESP_LOGD("custom", "Starting server");

      server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send_P(200, "text/html", index_html, processor);
      });
      server.on("/sockets/update", HTTP_GET, [](AsyncWebServerRequest *request){
        int params = request->params();
        if (!request->hasParam("id")) {
          return;
        }
        if (request->hasParam("enabled")) {
          setSocketNewEnabled(
            std::stoi(request->getParam("id")->value().c_str()),
            std::stoi(request->getParam("enabled")->value().c_str())
          );
        }
        if (request->hasParam("scheduleEnabled")) {
          setSocketNewScheduleEnabled(
            std::stoi(request->getParam("id")->value().c_str()),
            std::stoi(request->getParam("scheduleEnabled")->value().c_str())
          );
        }
      });
      server.on("/sockets/schedule/remove", HTTP_GET, [](AsyncWebServerRequest *request){
        int params = request->params();
        if (request->hasParam("id") && request->hasParam("hour")) {
          removeSocketSchedule(
            std::stoi(request->getParam("id")->value().c_str()),
            const_cast<char*> (request->getParam("hour")->value().c_str())
          );
        }
      });
      server.on("/sockets/schedule/set", HTTP_GET, [](AsyncWebServerRequest *request){
        int params = request->params();
        if (request->hasParam("id") && request->hasParam("hour")  && request->hasParam("mode")) {
          setSocketSchedule(
            std::stoi(request->getParam("id")->value().c_str()),
            const_cast<char*> (request->getParam("hour")->value().c_str()),
            std::stoi(request->getParam("mode")->value().c_str())
          );
        }
      });
      server.on("/wifi", HTTP_GET, [](AsyncWebServerRequest *request){ // doesn't work. Captive doesn't start.
        ESP_LOGD("custom", "Starting wifi portal");
        request->send(200, "text/plain", "OK");
        server.end();
        captive_portal::global_captive_portal->start(); // starts captive portal
      });
      
      server.begin();
    }

    void loop() override
    {}
};
