# Hydroponic Manager

This set of software is used for managing a hydroponic system. This includes
keeping a reservoir's pH in range, refilling a reservoir, and logging data
and events via an HTTP server.

Currently, there is a single program; an Arduino sketch that implements most of the basic hydroponic manager functions.

## Problems of Version 0.2

The Arduino sketch's current version is 0.2. Here are the problems it faces:

* Not enough pins for all the necessary water sensors (uses an ESP8266)
* Events (such as adding pH down to a reservoir) are not logged yet
* The HTTP server takes a few seconds to respond and sometimes will drop an HTTP request
* mDNS sometimes will not respond to hostname request
* There are not enough pins for a status screen
* With only 1 core and no RTOS, the system could potentially get stuck on any of its tasks
* No PPM meter; there is only 1 ADC

All of these will be added in version 1.0

## Version 1.0 Plan

This will be a large change, as I plan to switch the microcontroller from an ESP8266 to
an ESP32. This gives the advantages of many more pins and also 2 cores instead of 1.

I also plan to move away from Arduino and use the Espressif ESP32 SDK instead. This will
require an entire rewrite of the hydroponic manager system. It will also require me to
replace some libraries that don't work without Arduino; such as a JSON parser. I also
plan to use an RTOS to prevent any one task from locking out the entire system.

## Hydroponic Logger Version 0.1 Plan

The hydroponic logger will be a Linux-hosted program that sends HTTP requests to the
hydroponic manager and logs data and events in a database.

Version 0.1 will be a Python program that implements the basic functions:

* Log pH and PPM to database every X minutes
* Log events such as pH pump pulses or settings changes
* Log errors that come from the hydroponic manager
* Provide a website frontend for viewing the data

