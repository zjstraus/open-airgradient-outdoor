| Supported Targets | | ESP32-C3 |  |
|-------------------|-|----------|--| 

# OpenAirgradient Outdoor

This is a custom firmware using ESP-IDF to drive the [Airgradient Open Air Outdoor 1.1 kit](https://www.airgradient.com/open-airgradient/instructions/diy-open-air-presoldered-v11/).

It publishes all data to MQTT and tries to drive the PMS5003 sensors in a way that maximizes lifespan.


## Requirements
* ESP-IDF 5.1


## Configuration
Uses Kconfig to set all configuration at build-time. Run `idf.py menuconfig` and find relevant options under "Airgradient Configuration".

## MQTT Update structure
* {configuration base path}/{sensor ID}/temperature - Temperature in deg C
* {configuration base path}/{sensor ID}/humidity - Relative humidity
* {configuration base path}/{sensor ID}/raw/0.3 - Number of particles bigger than 0.3um in 0.1L of air
* {configuration base path}/{sensor ID}/raw/0.5 - Number of particles bigger than 0.5um in 0.1L of air
* {configuration base path}/{sensor ID}/raw/1.0 - Number of particles bigger than 1.0um in 0.1L of air
* {configuration base path}/{sensor ID}/raw/2.5 - Number of particles bigger than 2.5um in 0.1L of air
* {configuration base path}/{sensor ID}/standard/pm1.0 - PM1.0 concentration (ug/m3) for standard particle
* {configuration base path}/{sensor ID}/standard/pm2.5 - PM2.5 concentration (ug/m3) for standard particle
* {configuration base path}/{sensor ID}/standard/pm10.0 - PM10.0 concentration (ug/m3) for standard particle
* {configuration base path}/{sensor ID}/atmospheric/pm1.0 - PM1.0 concentration (ug/m3) for atmospheric environment
* {configuration base path}/{sensor ID}/atmospheric/pm2.5 - PM2.5 concentration (ug/m3) for atmospheric environment
* {configuration base path}/{sensor ID}/atmospheric/pm10.0 - PM10.0 concentration (ug/m3) for atmospheric environment