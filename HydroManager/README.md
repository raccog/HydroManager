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

# Hydroponic Manager Version 1.0 Plan

This will be a large change, as I plan to switch the microcontroller from an ESP8266 to
an ESP32. This gives the advantages of many more pins and also 2 cores instead of 1.

I also plan to move away from Arduino and use the Espressif ESP32 SDK instead. This will
require an entire rewrite of the hydroponic manager system. It will also require me to
replace some libraries that don't work without Arduino; such as a JSON parser. I also
plan to use an RTOS to prevent any one task from locking out the entire system.

## Bill Of Materials

Here is a list of all the hardware used for the HydroManager Version 1.0:

* ESP32-WROOM Development Board
* 12v DC Wall Adapter
* 12v to 5v Buck Converter
* USB-C Power Cable
* 4-channel, 5v Relay Module
* 3x 12v Diaphragm Water Pumps
* ADS1115 4-channel ADC to I2C Module
* Atlas Scientific Gravity Analog pH Kit (can also use the isolated pH board)
* TDS Meter
* 4x Water Level Sensors
* Red LED + 330 Ohm Resistor
* 2x Push Buttons
* 128x64 Pixel OLED with SSD1306 Driver

## Hydroponic Manager Program Description

Version 1.0 of the Hydroponic Manager runs on an ESP32-WROOM and uses the ESP32-SDK
by Espressif. With two cores on the ESP32, 1 will be used for the main sensor and pump
tasks and the other will be used for the HTTP server, a status display, and connected
to a few input buttons.

### Pins

The following pins are used as defaults. They will be able to be changed using
the esp-idf menuconfig.

* ADS1115 SDA: GPIO13 (I2C0)
* ADS1115 SCL: GPIO12 (I2C0)
* BME280 SDA: GPIO13 (I2C0)
* BME280 SCL: GPIO12 (I2C0)
* SSD1306 SDA: GPIO14 (I2C1)
* SSD1306 SCL: GPIO27 (I2C1)
* PUMP0 Relay: GPIO26
* PUMP1 Relay: GPIO25
* PUMP2 Relay: GPIO33
* System Status LED: GPIO32
* System Toggle Button: GPIO35
* Display Toggle Buttoin: GPIO34
* Overflow Sensor 0: GPIO15
* Overflow Sensor 1: GPIO2
* Refill Sensor: GPIO4
* External Reservoir Sensor: GPIO16

### Types

The following types have been declared for this program:

#### Version

A versioning scheme that can be used in different structures.

```c
// sizeof(ManagerVersion) == 2
struct ManagerVersion {
    uint8_t major;
    uint8_t minor;
};
```

* major - Major version number
* minor - Minor version number

#### ManagerSettings

Global settings for the manager. These can be changed during runtime.

```c
// sizeof(ManagerSettings) == 24
struct ManagerSettings {
    uint32_t magic;
    struct ManagerVersion version;
    uint8_t auto_ph;
    uint8_t refill_mode;
    uint32_t ph_stabilize_interval;
    uint32_t ph_dose_length;
    uint32_t refill_dose_length;
    uint32_t crc32;
};
```

* `magic` - A constant to verify that the structure is valid. Currently `0xc0ffee15`.
* `version` - The current structure version.
* `auto_ph` - A boolean that is 1 when the system should automatically stabilize ph.
* `refill_mode` - Has 3 options: Off, Refill, and Circulate.
* `ph_stabilize_interval` - Milliseconds between ph stabilization attempts.
* `ph_dose_length` - Length of a single ph pump dose in milliseconds.
* `refill_dose_length` - Length of a single refill pump dose in milliseconds.
* `crc32` - A CRC32 checksum to ensure that a settings structure is valid.

#### PumpPulseEvent

A recording of an event where one of the pumps was pulsed by the system.

```c
// sizeof(PumpPulseEvent) == 24
struct PumpPulseEvent {
    uint32_t magic;
    struct ManagerVersion version;
    uint16_t pump_id;
    uint64_t timestamp;
    uint32_t pulse_length;
    uint8_t was_interrupted;
    uint8_t was_automatic;
    uint16_t crc16;
};
```

* `magic` - A constant to verify that the structure is valid. Currently `0xface0ff1`.
* `version` - The current structure version.
* `pump_id` - The id of the pump that was activated.
* `timestamp` - A 64-bit timestamp of when this event occurred.
* `pulse_length` - The number of milliseconds that this pump was activated for.
* `was_interrupted` - 1 if the pump pulse was interrupted by the overflow sensor.
* `was_automatic` - 1 if the pump was automatically activated by the manager.
* `crc16` - A CRC16 checksum to ensure the structure is valid.

### Constants

Here are all the global constants used in the program:

```c
// WIFI SSID
#define STASSID "WIFI-SSID"
// WIFI Password
#define STAPSK "WIFI-PASSWORD"

// Pump Pins
#define PUMP0 26
#define PUMP1 25
#define PUMP2 33
#define PUMP_PH_DOWN PUMP0
#define PUMP_PH_UP PUMP1
#define PUMP_REFILL PUMP2

// System Toggle Pins
#define SYS_TOGGLE_BUTTON 32
#define SYS_TOGGLE_LED 35

// Display Toggle Pin
#define DISPLAY_TOGGLE_BUTTON 34

// Water Level Sensor Pins
#define EXTERNAL_RESERVOIR_SENSOR 15
#define OVERFLOW_SENSOR0 2
#define OVERFLOW_SENSOR1 4
#define REFILL_SENSOR 16

// Valid ranges for settings fields
const uint32_t PH_STABILIZE_INTERVAL_MIN = 30000;       // 30 seconds
const uint32_t PH_STABILIZE_INTERVAL_MAX = 43200000;    // 12 hours
const uint32_t PH_DOSE_MIN = 200;           // 200 milliseconds
const uint32_t PH_DOSE_MAX = 10000;         // 10 seconds
const uint32_t REFILL_DOSE_MIN = 5000;      // 5 seconds
const uint32_t REFILL_DOSE_MAX = 70000;     // 70 seconds

// Refill Pump States
#define REFILL_OFF 1
#define REFILL_ON 2
#define REFILL_CIRCULATE 3

// Display Settings
#define DISPLAY_WIDTH 128
#define DISPLAY_HEIGHT 64

// Magic Values
const uint32_t SETTINGS_MAGIC = 0xc0ffee15;
const uint32_t PUMP_EVENT_MAGIC = 0xface0ff1;

// Current Structure Versions
const struct ManagerVersion SETTINGS_VERSION = {
    .major = 1,
    .minor = 0
};
const struct ManagerVersion PUMP_EVENT_VERSION = {
    .major = 1,
    .minor = 0
};

// Stable pH Range
const float PH_MIN = 5.5f;
const float PH_MAX = 6.5f;

// pH Sensor Standard Deviation
const float PH_STD_DEVIATION = 0.2f;

// Forced millisecond delay between system/display toggles
const uint32_t TOGGLE_DELAY = 5000; // 5 seconds
```

### Global Variables

These global variables can be changed during runtime. Extra care needs to be taken
when interacting with these variables.

It should be ensured that when writing to any of these variables, the other core
is not reading it at the same time. Some variables will be private for either
core 0 or core 1.

```c
// Global system settings. These can be changed using an HTTP POST endpoint.
// Core 0 cannot write to these settings.
struct ManagerSettings system_settings {
    .magic = SETTINGS_MAGIC,
    .version = SETTINGS_VERSION,
    .auto_ph = 1,
    .refill_mode = REFILL_OFF,
    .ph_stabilize_interval = 300000,    // 5 minutes
    .ph_dose_length = 1000,             // 1 second
    .refill_dose_length = 60000,        // 60 seconds
    .crc32 = 0  // CRC32 needs to be calculated during setup
};

// Timers
uint64_t last_ph_stabilize_attempt = 0;
uint64_t last_system_toggle = 0;

// On/Off State for Entire System
bool system_enabled = true;
// On/Off State for Status Display
bool display_enabled = false;

// Circular Buffer for Pump Events
struct PumpPulseEvent pump_event_cache[PUMP_EVENT_BUFFER_LENGTH];
size_t pump_event_cache_start = 0;
size_t pump_event_cache_end = 0
```

### Tasks

Tasks will be managed by the FreeRTOS scheduler. Each task will either run on
core0 or core1.

#### Core 0

* Stabilize pH
* Refill Reservoir
* Overflow Sensor Activated (ISR)
* Receive IPC from Core 1

IPC from core 1 can cause the following tasks:

* Update settings from Core 1
* Save settings to flash
* Send sensor data to core 1
* Send events to core 1
* Send logs to core 1

#### Core 1

* Update RTC using NTP
* System/Display Toggle Buttons (ISR)
* Update Display
* Check for HTTP requests

Check for HTTP requests can cause the following tasks:

* Send new settings to core 0
* Tell core 0 to save settings to flash
* Tell core 0 to send data/events/logs

