# Hydroponic Manager

The Hydroponic Manager is a program that runs continuously on a microcontroller. It performs
the following functions:

* Stabilize pH in a specified range
* Provide sensor data and events over HTTP

Eventually, it will also be able to refill a hydroponic reservoir when it's water level is
getting low. The current HydroManager is implemented on an ESP8266, which does not have
enough pins for this. See the next section for more details.

## Problems of Version 0.x

* Not enough pins for all the necessary water sensors (uses an ESP8266)
* The HTTP server takes a few seconds to respond and sometimes will drop an HTTP request
* mDNS sometimes will not respond to hostname request
* There are not enough pins for a status screen
* With only 1 core and no RTOS, the system could potentially get stuck on any of its tasks
* No PPM meter; there is only 1 ADC
* pH range is hardcoded
* pH measurement error is also hardcoded

All of these will be added in version 1.0

## Version 1.0 Plan

This will be a large change, as I plan to switch the microcontroller from an ESP8266 to
an ESP32. This gives the advantages of many more pins and also 2 cores instead of 1.

I also plan to move away from Arduino and use the Espressif ESP32 SDK instead. This will
require an entire rewrite of the hydroponic manager system. It will also require me to
replace some libraries that don't work without Arduino; such as a JSON parser. I also
plan to use an RTOS to prevent any one task from locking out the entire system.

