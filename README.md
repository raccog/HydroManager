# Hydroponic Manager

This set of software is used for managing a hydroponic system. This includes
keeping a reservoir's pH in range, refilling a reservoir, and logging data
and events via an HTTP server.

Currently, there is a single program; an Arduino sketch that implements most of the basic
hydroponic manager functions.

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

* Log pH and PPM to SQL database every X minutes
* Log events such as pH pump pulses or settings changes
* Log errors that come from the hydroponic manager
* Provide a website frontend for viewing the data

All logs are currently structured in JSON. The hydro manager will keep a buffer for all
recent logs. When requested via HTTP, it will send those logs to another server where
they can be saved to a database.

Every X minutes, the hydro logger will make two HTTP requests (maybe combine them?) to the
hydro manager; one for the current data readings and another for the log buffer. It will
then add these data points to a database.

As a frontend, I plan to run a Highcharts website with graphs for each sensor.

## Logger Structure Version 0.1

This initial version of the logger structure is formatted with JSON. It might be
inefficient in storage space, but that shouldn't matter in normal circumstances.
If the ESP8266/ESP32 runs out of RAM and cannot dump its logs to a larger server,
they could temporarily dump the logs to the 4MB flash memory.

Note that each of the following JSON logs also include two common fields that are
omitted in the examples. These are "time" and "msg", which hold the log's timestamp
and readable message, respectively. The "time" field is a 64-bit timestamp. The
"msg" field is a string with no required structure; this can be useful for manually
reading the logs.

There are 4 possible logging levels:

* DEBUG
* INFO
* DATA
* ERROR

Most of them are self explanatory. The DATA level is used for events that can be useful
for data graphs, such as ph pulses.

### Pump Pulse

One of the pumps (ph up/down or refill) was pulsed.

```json
{
    "lvl": "DATA",
    "type": "pump",
    "pump": "ph-down",
    "len": 1200,
}
```

Values for "pump" are:

* "ph-down"
* "ph-up"
* "refill"

### Stabilize pH

The pH sensor was read to determine whether the pH was in range and stabilize it with
the pH pumps.

```json
{
    "lvl": "INFO",
    "type": "stabilize-ph",
    "ph": 6.12,
}
```

### System Disable

The system was disabled/enabled.

```json
{
    "lvl": "DATA",
    "type": "system-toggle",
    "enabled": true,
}
```

### Overflow Sensor Activated

```json
{
    "lvl": "DATA",
    "type": "overflow",
}
```

### HTTP Request

A client made an HTTP request.

NOTE: As there will be many HTTP requests, this will probably take up too much space.
      It will likely be removed.

```json
{
    "lvl": "INFO",
    "type": "http-request",
    "uri": "/read",
    "response": 200,
}
```

### Update Settings

The settings were updated.

```json
{
    "lvl": "INFO",
    "type": "update-settings",
    "settings": /* Settings struct here */,
}
```

### Save Settings

The settings were saved as defaults to flash memory.

```json
{
    "lvl": "INFO",
    "type": "save-settings",
    "settings": /* Settings struct here */,
}
```

### System Startup

The system started up.

```json
{
    "lvl": "INFO",
    "type": "startup",
}
```

### NTP Failure

The time could not be retrieved from an NTP server.

```json
{
    "lvl": "ERROR",
    "type": "ntp",
}
```

### pH Meter Setup Failure

The pH meter failed to be setup.

```json
{
    "lvl": "ERROR",
    "type": "ph-meter-setup",
}
```

### mDNS Failure

mDNS failed to start.

```json
{
    "lvl": "ERROR",
    "type": "mDNS",
}
```

### Failed to Load Settings

The settings could not be loaded from flash.

```json
{
    "lvl": "ERROR",
    "type": "load-settings",
}
```
