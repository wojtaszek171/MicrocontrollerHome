# ESP8266 microcontroller driver
Smart home ESP8266 driver. It controls power sockets basing on settings from database. Additionally it checks parameters and send through API into database(humidity, pressure, temperature).

This project requires ESP8266 microcontroller with connected modules:
- Real time clock (DS1302)
- BME280 sensor (pressure, humidity, temperature)
- 4 relays module (for my project I created power sockets controllable with relays)
- Hermetic temperature sensor (DS1820)

# API:
- getSockets.php - fetch database values about power sockets settings
- setSensor.php - send sensor value to database

# Driver ESP1
- Fetching time from web and save value to real time clock module (stores time when no internet connection between restarts)
- Fetching sockets settings (enabled, startTime, stopTime) and save to EEPROM (stores settings when no internnet connection between restarts)
- Reading sensors values (2 temperature, pressure, humidity, temperature) and sending to database
- Switching power sockets basing on current time from web/clock module and settings from database/eeprom.

For my purpose I created additional project Smart home (https://github.com/wojtaszek171/SmartHome) which display all values and settings.

Built prototype: 

![Prototype](https://github.com/wojtaszek171/MicrocontrollerHome/blob/readme-images/prototype-min.jpg)
