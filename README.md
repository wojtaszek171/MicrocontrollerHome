# ESP8266 microcontroller driver
Smart home ESP8266 driver. It controls power sockets basing on settings from database. Additionally it checks parameters and send through API into database(humidity, room temperature, water temperature).

## This project is working with
- https://github.com/wojtaszek171/personalApi
- https://github.com/wojtaszek171/MicrocontrollerHome
- https://github.com/wojtaszek171/pwojtaszko-design

## Required modules:
- ESP8266 NodeMCU
- Real time clock (DS1302)
- hdc1080 sensor (humidity, temperature)
- 4 relays module (for my project I created power sockets controllable with relays)
- Hermetic temperature sensor (DS1820)

## API:
- getSockets.php - fetch database values about power sockets settings
- setSensor.php - send sensor value to database

## Driver ESP1
- Fetching time from web and save value to real time clock module (stores time when no internet connection between restarts)
- Fetching sockets settings (enabled, lightModes) and save to EEPROM (stores settings when no internnet connection between restarts)
- Reading sensors values (hdc1080, DS1820) and sending to database
- Switching power sockets basing on current time from web/clock module and settings from database/eeprom.
- Access point functionality to provide host and WiFi information
- Support for Aquael day and night LED light modes switching (3 light modes: night, day, evening)

## Driver ESP2
- Reading sensors values (hdc1080, bme280) and sending to database

## Driver ESP3
- Reading sensors values (hdc1080) and sending to database

For my purpose I created additional project Smart home (https://github.com/wojtaszek171/SmartHome) which display all values and settings. It contains also administration panel for changing sockets settings.

## Built prototype: 

![Prototype](https://github.com/wojtaszek171/MicrocontrollerHome/blob/readme-images/1.jpg)
![Prototype](https://github.com/wojtaszek171/MicrocontrollerHome/blob/readme-images/2.jpg)
![Prototype](https://github.com/wojtaszek171/MicrocontrollerHome/blob/readme-images/3.jpg)
![Prototype](https://github.com/wojtaszek171/MicrocontrollerHome/blob/readme-images/4.jpg)
![Prototype](https://github.com/wojtaszek171/MicrocontrollerHome/blob/readme-images/5.jpg)
![Prototype](https://github.com/wojtaszek171/MicrocontrollerHome/blob/readme-images/6.jpg)
![Prototype](https://github.com/wojtaszek171/MicrocontrollerHome/blob/readme-images/7.jpg)
![Prototype](https://github.com/wojtaszek171/MicrocontrollerHome/blob/readme-images/8.png)

