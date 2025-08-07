![sensoria logo](assets/sensoria-logo.svg)

# sensoria sensor-eink

Powered by Espressif-IDF S3 and built in around epdiy Eink controller, sensoria has all it needs to measure and show an information dashboard

## Requirements

sensoria 1.0 version of the sensor uses a modified [epdiy v7 raw eink controller](https://github.com/vroland/epdiy-hardware/).
Attached by a QWIIC connector, with standard GND,VSS,SDA,SCL pins, there is a SCD40 or SCD41 CO2 sensor from Sensiria.

## How to build

Just clone this repository and it's submodules:

    git clone --recursive https://github.com/sensoria-iot/sensor-eink.git

And flash this code from root folder in your S3 PCB controller

    idf.py flash monitor

## On boarding and WiFi provision

In order to provide WiFi to your sensor you need to download [ESP-Rainmaker](https://play.google.com/store/apps/details?id=com.espressif.rainmaker) APP either from Google play store or [AppStore](https://apps.apple.com/us/app/esp-rainmaker/id1497491540) if you use iPhone devices. 

Please scan the QR code shown either on the Serial console or in the Eink display. 
And select your 2.4 Ghz WiFi internet access point. 

## AI analisis provided by sensoria

sensoria mission is to turn real-time sensor data into actionable business insights. That way you can make your team work better with ideal ambient conditions.

**Proactive Detection**

Detects and anticipates issues before they become costly problems for your business.

**Real-time Analysis**

Compares your office conditions to ideal standards instantly for optimal performance.

Please join our early access to test it as soon as possible:

https://sensoria.cat/#contact


## Team 

[martinberlin](https://github.com/martinberlin) ESP-IDF Firmware / Eink controllers 

[santi-tonelli](https://github.com/santiTonelliPunta) AI expert / UI UX design 
