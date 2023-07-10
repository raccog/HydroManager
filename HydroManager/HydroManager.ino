// Hydroponic Manager - Ryan Cohen, 2023
// Version 0.3.0
//
// Use 40mL per Liter of pH Up/Down mix
//
// Features:
//  Auto pH Stabilizer
//  Manual pH pulses via HTTP
//  pH reads via HTTP
//  Float sensor to detect potential overflows
//  Logging via UART
//  Change settings via HTTP
//  Persistent settings in FLASH
//  Disable/enable system with button during maintenance
//  NTP client for timesync
//  mDNS hostname for HTTP access
//
// Pins:
//  GPIO14 - pH Up Pump
//  GPIO12 - pH Down Pump
//  GPIO5 - Overflow Sensor
//  GPIO4 - System Disable Button
//  GPIO0 - System Disabled LED
//  ADC0 - Atlas pH Sensor
//
// Changes for future versions:
//  * Log events to server via HTTP (events include errors and pump pulses)
//  * Refill mode (refill from second reservoir); needs 3 extra GPIO pins that the ESP8266 does not have
//  * Re-enable SSD1306 as status display, including a display disable button; needs 3 more extra pins
//  * Utilize RTOS for multithreading; 1 core for essential stuff, other core for HTTP server
//  * Display time until next pH pulse/refill
//  * I2C ADC module
//  * PPM sensor
//  * BME280 temp/humidity sensor
//
// Changelog:
//
// Version 0.3.0
//  * Add '/json/mailbox.json' endpoint
//  * Use a buffer to store recent pump pulse events

#include <cmath>
#include <limits.h>

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include <CRC32.h>
#include <EEPROM.h>
#include <NTPClient.h>
#include <ESP8266mDNS.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiUdp.h>
#include <Wire.h>

#include "ph_grav.h"

//---------------------
// Type declarations
//---------------------

// Setting types
enum Type {
  BOOL = 1,
  ULONG = 2
};

// Setting details
struct SettingDetails {
  String name;
  enum Type type;
  bool has_range;
  unsigned long min_range;
  unsigned long max_range;
  void *value;
};

enum RefillPumpMode {
  REFILL_OFF = 1,
  REFILL_ON = 2,
  REFILL_CIRCULATE = 3
};

// All hydroponic monitor Settings
struct Settings {
  unsigned long magic;
  unsigned long version;
  bool autoPh;
  enum RefillPumpMode refillMode;
  unsigned long phCheckInterval; // 5 minutes in seconds
  unsigned long phPumpDoseLength;  // 1000 milliseconds
  unsigned long refillDoseLength;    // 60 seconds
  uint32_t crc32;
};

enum PumpType {
  PH_DOWN = 1,
  PH_UP = 2,
  REFILL = 3
};

struct PumpPulseEvent {
  unsigned long timestamp;
  enum PumpType type;
  unsigned long length;
  bool interrupted;
};

//---------------------
// Constants
//---------------------

// Uncomment this to reset the EEPROM settings
//#define RESET_EEPROM

// Wifi Info
#define STASSID "***REMOVED***"
#define STAPSK "***REMOVED***"

// Pins
const int PUMP0 = 14;
const int PUMP1 = 12;
const int PUMP2 = 13;

const int PUMP_PH_UP = PUMP0;
const int PUMP_PH_DOWN = PUMP1;
const int PUMP_REFILL = PUMP2;

const int OVERFLOW_SENSOR = 5;
const int DISABLE_BUTTON = 4;
const int DISABLED_LED = 0;

const unsigned long PH_CHECK_INTERVAL_MIN = 30;
const unsigned long PH_CHECK_INTERVAL_MAX = 12 * 60 * 60;
const unsigned long PH_PUMP_DOSE_LENGTH_MIN = 200;
const unsigned long PH_PUMP_DOSE_LENGTH_MAX = 10000;
const unsigned long REFILL_DOSE_LENGTH_MIN = 5;
const unsigned long REFILL_DOSE_LENGTH_MAX = 70;

// Status display settings
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET (-1)

// EEPROM starting address
const int EEPROM_START = 1024;
// Use up this many bytes for the settings in case the struct expands in the future
const int SETTINGS_SIZE = 128;

// Settings magic number and current version
const unsigned long SETTINGS_MAGIC = 0xdae46bfa;
const unsigned long SETTINGS_VERSION = 2;

// pH Range
const float MAX_PH = 6.5f;
const float MIN_PH = 5.5f;
// Value is accurate within ~0.2 pH
const float PH_ACC = 0.2f;

// Disable button delay
const unsigned long DISABLE_DELAY = 5;

// Size of the pump pulse event buffer
const size_t MAX_PUMP_PULSE_EVENTS = 5;

//---------------------
// Global variables
//---------------------

// System settings
struct Settings settings {
  .magic = SETTINGS_MAGIC,
  .version = SETTINGS_VERSION,
  .autoPh = true,
  .refillMode = REFILL_OFF,
  .phCheckInterval = 5 * 60,  // 5 minutes in seconds
  .phPumpDoseLength = 1000,   // 1000 milliseconds
  .refillDoseLength = 60,     // 60 seconds
};

// Details for some settings values
struct SettingDetails settingsDetails[] = {
  {"phCheckInterval", ULONG, true, PH_CHECK_INTERVAL_MIN, PH_CHECK_INTERVAL_MAX, (void *)&settings.phCheckInterval},
  {"phPumpDoseLength", ULONG, true, PH_PUMP_DOSE_LENGTH_MIN, PH_PUMP_DOSE_LENGTH_MAX, (void *)&settings.phPumpDoseLength},
  {"refillDoseLength", ULONG, true, 5, 70, (void *)&settings.refillDoseLength}
};

// Atlas pH Sensor
Gravity_pH pH = Gravity_pH(A0);

// Status display
//Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// NTP client
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", -14400, 60000);

// HTTP Server
ESP8266WebServer server(80);

// Timestamps
unsigned long lastPhCheck = 0;
unsigned long lastDisable = 0;

// Allow for temporary disabling of the entire system or just the status screen
bool systemEnabled = true;
bool statusDisplayEnabled = false;

// Buffer of recent pump pulse events
// TODO: Add a fallback in case the buffer fills up
struct PumpPulseEvent recentPumpPulses[MAX_PUMP_PULSE_EVENTS];
size_t numPulseEvents = 0;

//---------------------
// HTML files
//---------------------

// HTML
const String readForm = "<!DOCTYPE html>\
<html>\
<head>\
    <title>Current pH</title>\
</head>\
<body>\
    <h1>Current pH</h1>\
    <h2>%s</h2>\
    <p>%3.2f pH</p>\
</body>\
</html>";
const String autoPulseForm = "<!DOCTYPE html>\
<html>\
<head>\
    <title>Auto Pulse pH</title>\
</head>\
<body>\
    <form method=\"post\" action=\"api/auto_pulse\">\
        <button type=\"submit\">Auto Pulse</button>\
    </form>\
</body>\
</html>";
const String pulseForm = "<!DOCTYPE html>\
<html>\
<head>\
    <title>Pulse pH</title>\
</head>\
<body>\
    <form method=\"post\" action=\"api/pulse\">\
        <label for=\"pump\">pH Up/Down:</label>\
        <select id=\"pump\" name=\"pump\">\
            <option value=\"1\">pH Down</option>\
            <option value=\"2\">pH Up</option>\
        </select>\
        <br>\
        <label for=\"pulseLen\">Legnth of pulse in milliseconds:</label>\
        <input type=\"number\" id=\"pulseLen\" name=\"pulseLen\" value=\"%lu\" min=\"%lu\" max=\"%lu\" required>\
        <br>\
        <button type=\"submit\">Pulse</button>\
    </form>\
</body>\
</html>";
const String settingsForm = "<!DOCTYPE html>\
<html>\
<head>\
    <title>Settings</title>\
</head>\
<body>\
    <form method=\"post\" action=\"api/write_settings\">\
        <label for=\"autoPh\">Auto pH (default: %s) :</label>\
        <input type=\"checkbox\" id=\"autoPh\" name=\"autoPh\" %s>\
        <br>\
        <label for=\"refillMode\">Circulate Reservoir (default: %s) :</label>\
        <input type=\"checkbox\" id=\"refillMode\" name=\"refillMode\" %s>\
        <br>\
        <label for=\"phCheckInterval\">phCheckInterval (default: %lu) :</label>\
        <input type=\"number\" id=\"phCheckInterval\" name=\"phCheckInterval\" value=\"%lu\" min=\"%lu\" max=\"%lu\">\
        <br>\
        <label for=\"phPumpDoseLength\">phPumpDoseLength (default: %lu) :</label>\
        <input type=\"number\" id=\"phPumpDoseLength\" name=\"phPumpDoseLength\" value=\"%lu\" min=\"%lu\" max=\"%lu\">\
        <br>\
        <label for=\"refillDoseLength\">refillDoseLength (default: %lu) :</label>\
        <input type=\"number\" id=\"refillDoseLength\" name=\"refillDoseLength\" value=\"%lu\" min=\"%lu\" max=\"%lu\">\
        <br>\
        <button type=\"submit\">Submit Settings</button>\
        <br>\
    </form>\
    <form action=\"/api/save_settings\" method=\"post\">\
        <button type=\"submit\">Save Settings as Default</button>\
    </form>\
</body>\
</html>";

//---------------------
// Settings functions
//---------------------

bool settingsRefillModeIsValid(enum RefillPumpMode refillMode) {
  switch (refillMode) {
    case REFILL_OFF:
    case REFILL_ON:
    case REFILL_CIRCULATE:
      return true;
    default:
      return false;
  }
}

// Returns true if `settings` is valid
bool settingsIsValid(struct Settings checkSettings) {
  return (!(checkSettings.magic != SETTINGS_MAGIC
      || checkSettings.version != SETTINGS_VERSION
      || isOutOfRange(checkSettings.phCheckInterval, settingsDetails[0].min_range, settingsDetails[0].max_range)
      || isOutOfRange(checkSettings.phPumpDoseLength, settingsDetails[1].min_range, settingsDetails[1].max_range)
      || isOutOfRange(checkSettings.refillDoseLength, settingsDetails[2].min_range, settingsDetails[2].max_range)
      || !settingsRefillModeIsValid(settings.refillMode)
      || checkSettings.crc32 != crc32((uint8_t *)&checkSettings, sizeof(checkSettings) - sizeof(uint32_t))));
}

// Returns true if the value is out of range
bool isOutOfRange(unsigned long value, unsigned long valMin, unsigned long valMax) {
  return (value < valMin || value > valMax);
}

//---------------------
// Pump control functions
//---------------------

// Pulse one of the pumps for `len` milliseconds
void pulsePump(int pump, unsigned long len) {
  // Ensure the pump is a ph pump
  if (!(pump == PUMP_PH_UP || pump == PUMP_PH_DOWN)) {
    return;
  }

  // Ensure the system is not disabled and overflow sensor is not set
  if (!systemEnabled || digitalRead(OVERFLOW_SENSOR) == HIGH) {
    return;
  }

  bool interrupted = false;
  
  digitalWrite(pump, LOW);
  unsigned long i;
  for (i = 0; i < len; ++i) {
    // Disable pump if about to overflow
    if (digitalRead(OVERFLOW_SENSOR) == HIGH) {
      digitalWrite(pump, HIGH);
      interrupted = true;
      break;
    }
    delay(1);
  }
  digitalWrite(pump, HIGH);

  // Add to event buffer
  struct PumpPulseEvent pulseEvent = {
    .timestamp = timeClient.getEpochTime(),
    .type = (pump == PUMP_PH_DOWN) ? PH_DOWN : PH_UP,
    .length = i,
    .interrupted = interrupted
  };

  if (numPulseEvents < MAX_PUMP_PULSE_EVENTS) {
    recentPumpPulses[numPulseEvents] = pulseEvent;
    numPulseEvents += 1;
  }

  // LOGGER
  Serial.print(timeClient.getFormattedTime());
  Serial.print(", pulse ");
  if (pump == PUMP_PH_UP) {
    Serial.print("up ");
  } else {
    Serial.print("down ");
  }
  Serial.print(settings.phPumpDoseLength);
  Serial.println(" ms");
}

// Pulse one of the pH pumps for `phPumpDoseLength` milliseconds
void pulsePhPump(int pump) {
  pulsePump(pump, settings.phPumpDoseLength); 
}

// Read the pH sensor to see if the pH is in range.
// If not in range, pulse one of the pH pumps to get back in range.
void bringPhInRange() {
  // Ensure the system is not disabled
  if (!systemEnabled) {
    return;
  }

  // Pulse with pH up or down if pH is not in range
  float currentPh = pH.read_ph();
  if (currentPh < MIN_PH + PH_ACC) {
    pulsePhPump(PUMP_PH_UP);
  } else if (currentPh > MAX_PH - PH_ACC) {
    pulsePhPump(PUMP_PH_DOWN);
  }

  // LOGGER
  Serial.print(timeClient.getFormattedTime());
  Serial.print(", ");
  Serial.print(currentPh);
  Serial.println("pH");
}

//---------------------
// HTTP functions
//---------------------

void httpHandleUnavailable() {
  digitalWrite(LED_BUILTIN, LOW);
  server.send(503, "text/plain", "System is currently disabled.");
  digitalWrite(LED_BUILTIN, HIGH);
}

// Sends the client the current time in JSON
void httpHandleRoot() {
  // Ensure the system is not disabled
  if (!systemEnabled) {
    httpHandleUnavailable();
    return;
  }
  
  digitalWrite(LED_BUILTIN, LOW);

  // Create JSON
  StaticJsonDocument<16> doc;
  doc["time"] = timeClient.getEpochTime();

  // Send to client
  char buffer[32 + 1];
  serializeJson(doc, &buffer, 32);
  server.send(200, "application/json", buffer);

  // LOGGER
  Serial.print("/: ");
  serializeJson(doc, Serial);
  Serial.println();
  
  digitalWrite(LED_BUILTIN, HIGH);
}

// Sends the client the current pH in JSON
void httpHandleRead() {
  // Ensure the system is not disabled
  if (!systemEnabled) {
    httpHandleUnavailable();
    return;
  }
  
  digitalWrite(LED_BUILTIN, LOW);
  
  // Create JSON
  StaticJsonDocument<48> doc;
  doc["time"] = timeClient.getEpochTime();
  doc["ph"] = round(pH.read_ph() * 100.0) / 100.0;
  
  // Send JSON to client
  char buffer[64 + 1];
  serializeJson(doc, &buffer, 64);
  server.send(200, "application/json", buffer);

  // LOGGER
  Serial.print("/api/read: ");
  serializeJson(doc, Serial);
  Serial.println();
  
  digitalWrite(LED_BUILTIN, HIGH);
}

// Sends the client an HTML document showing the current pH
void httpFormRead() {
  // Ensure the system is not disabled
  if (!systemEnabled) {
    httpHandleUnavailable();
    return;
  }
  
  digitalWrite(LED_BUILTIN, LOW);

  // Send HTML to client
  StreamString temp;
  temp.reserve(500);
  temp.printf(readForm.c_str(), timeClient.getFormattedTime(), round(pH.read_ph() * 100.0) / 100.0);
  server.send(200, "text/html", temp.c_str());

  // Create JSON
  StaticJsonDocument<48> doc;
  doc["time"] = timeClient.getEpochTime();
  doc["ph"] = round(pH.read_ph() * 100.0) / 100.0;

  // LOGGER
  Serial.print("/read: ");
  serializeJson(doc, Serial);
  Serial.println();
  
  digitalWrite(LED_BUILTIN, HIGH);
}

// Sends the client the current settings in JSON
void httpHandleSettings() {
  // Ensure the system is not disabled
  if (!systemEnabled) {
    httpHandleUnavailable();
    return;
  }
  
  digitalWrite(LED_BUILTIN, LOW);

  // Create JSON
  StaticJsonDocument<128> doc;
  doc["time"] = timeClient.getEpochTime();
  doc["autoPh"] = settings.autoPh;
  doc["refillMode"] = settings.refillMode;
  doc["phCheckInterval"] = settings.phCheckInterval;
  doc["phPumpDoseLength"] = settings.phPumpDoseLength;
  doc["refillDoseLength"] = settings.refillDoseLength;

  // Send JSON to client
  char buffer[256 + 1];
  serializeJson(doc, &buffer, 256);
  server.send(200, "application/json", buffer);

  // LOGGER
  Serial.print("/api/settings: ");
  serializeJson(doc, Serial);
  Serial.println();
  
  digitalWrite(LED_BUILTIN, HIGH);
}

// Sends the client an HTML document showing the current settings and allowing the client to edit them
void httpFormSettings() {
  // Ensure the system is not disabled
  if (!systemEnabled) {
    httpHandleUnavailable();
    return;
  }
  
  digitalWrite(LED_BUILTIN, LOW);

  // Get default settings from EEPROM
  struct Settings defaultSettings;
  EEPROM.get(EEPROM_START, defaultSettings);

  // Send HTML to client
  StreamString temp;
  temp.reserve(500);
  temp.printf(settingsForm.c_str(),
    defaultSettings.autoPh ? "true" : "false",
    settings.autoPh ? "checked" : "",
    (defaultSettings.refillMode == REFILL_CIRCULATE) ? "true" : "false",
    (settings.refillMode == REFILL_CIRCULATE) ? "checked" : "",
    defaultSettings.phCheckInterval,
    settings.phCheckInterval,
    PH_CHECK_INTERVAL_MIN,
    PH_CHECK_INTERVAL_MAX,
    defaultSettings.phPumpDoseLength,
    settings.phPumpDoseLength,
    PH_PUMP_DOSE_LENGTH_MIN,
    PH_PUMP_DOSE_LENGTH_MAX,
    defaultSettings.refillDoseLength,
    settings.refillDoseLength,
    REFILL_DOSE_LENGTH_MIN,
    REFILL_DOSE_LENGTH_MAX
  );
  server.send(200, "text/html", temp.c_str());

  // LOGGER
  Serial.println("/settings");
  
  digitalWrite(LED_BUILTIN, HIGH);
}

// Receives request from client to save current settings to EEPROM
void httpHandleSaveSettings() {
  // Ensure the system is not disabled
  if (!systemEnabled) {
    httpHandleUnavailable();
    return;
  }
  
  digitalWrite(LED_BUILTIN, LOW);

  // Save settings to EEPROM
  EEPROM.put(EEPROM_START, settings);
  EEPROM.commit();

  // Create JSON
  StaticJsonDocument<256> doc;
  doc["time"] = timeClient.getEpochTime();
  doc["autoPh"] = settings.autoPh;
  doc["refillMode"] = settings.refillMode;
  doc["phCheckInterval"] = settings.phCheckInterval;
  doc["phPumpDoseLength"] = settings.phPumpDoseLength;
  doc["refillDoseLength"] = settings.refillDoseLength;

  // Send JSON to client
  char buffer[512 + 1];
  serializeJson(doc, &buffer, 512);
  server.send(200, "application/json", buffer);

  // LOGGER
  Serial.print("/api/save_settings: ");
  serializeJson(doc, Serial);
  Serial.println();
  
  digitalWrite(LED_BUILTIN, HIGH);
}

// Receives updated settings from a client that are saved to RAM
void httpHandleWriteSettings() {
  // Ensure the system is not disabled
  if (!systemEnabled) {
    httpHandleUnavailable();
    return;
  }
  
  digitalWrite(LED_BUILTIN, LOW);

  // Save a settings backup in case the requested settings changes are invalid
  struct Settings backupSettings = settings;

  // Update current settings based on client request
  for (size_t i = 0; i < sizeof(settingsDetails) / sizeof(settingsDetails[0]); ++i) {
    if (server.hasArg(settingsDetails[i].name)) {
      String arg = server.arg(settingsDetails[i].name);
      if (settingsDetails[i].has_range && settingsDetails[i].type == ULONG) {
        unsigned long value = (unsigned long)arg.toInt();
        *(unsigned long *)settingsDetails[i].value = value;
      }
    }
  }
  settings.autoPh = server.hasArg("autoPh");
  settings.refillMode = server.hasArg("refillMode") ? REFILL_CIRCULATE : REFILL_OFF;

  // Update CRC32
  settings.crc32 = crc32((uint8_t *)&settings, sizeof(settings) - sizeof(uint32_t));

  // Successful settings change
  if (settingsIsValid(settings)) {
    // Update refill pump state for new settings
    digitalWrite(PUMP_REFILL, !(systemEnabled && settings.autoPh && settings.refillMode == REFILL_CIRCULATE));

    // Create JSON
    StaticJsonDocument<256> doc;
    doc["time"] = timeClient.getEpochTime();
    doc["autoPh"] = settings.autoPh;
    doc["refillMode"] = settings.refillMode;
    doc["phCheckInterval"] = settings.phCheckInterval;
    doc["phPumpDoseLength"] = settings.phPumpDoseLength;
    doc["refillDoseLength"] = settings.refillDoseLength;

    // Send JSON to client
    char buffer[512 + 1];
    serializeJson(doc, &buffer, 512);
    server.send(200, "application/json", buffer);

    // LOGGER
    Serial.print("/api/write_settings: ");
    serializeJson(doc, Serial);
    Serial.println();
  } else {
    // Revert if settings are invalid
    settings = backupSettings;
    server.send(500, "text/plain", "Invalid settings");
  }
  
  digitalWrite(LED_BUILTIN, HIGH);
}

// Send the client a 404 not found error
void httpHandleNotFound() {
  digitalWrite(LED_BUILTIN, LOW);
  server.send(404, "text/plain", "URI Not Found");
  digitalWrite(LED_BUILTIN, HIGH);
}

// Sends the client an HTML document to pulse one of the pH pumps for a specified time
void httpFormPulsePump() {
  // Ensure the system is not disabled
  if (!systemEnabled) {
    httpHandleUnavailable();
    return;
  }
  
  digitalWrite(LED_BUILTIN, LOW);

  // Send HTML to client
  StreamString temp;
  temp.reserve(500);
  temp.printf(pulseForm.c_str(), settings.phPumpDoseLength, PH_PUMP_DOSE_LENGTH_MIN, PH_PUMP_DOSE_LENGTH_MAX);
  server.send(200, "text/html", temp.c_str());

  // LOGGER
  Serial.println("/pulse");
  
  digitalWrite(LED_BUILTIN, HIGH);
}

// Receives a request from the client to pulse one of the pH pumps for the default time
void httpHandleAutoPulsePump() {
  // Ensure the system is not disabled
  if (!systemEnabled) {
    httpHandleUnavailable();
    return;
  }
  
  digitalWrite(LED_BUILTIN, LOW);

  // Create JSON
  StaticJsonDocument<128> doc;
  doc["time"] = timeClient.getEpochTime();
  doc["ph"] = round(pH.read_ph() * 100.0) / 100.0;

  // Ensure auto ph mode is not enabled
  if (!settings.autoPh) {
    // Send JSON to client
    bringPhInRange();
    char buffer[256 + 1];
    serializeJson(doc, &buffer, 256);
    server.send(200, "application/json", buffer);
  } else {
    server.send(500, "text/plain", "cannot auto pulse in auto ph mode");
  }

  // LOGGER
  Serial.print("/api/auto_pulse: ");
  serializeJson(doc, Serial);
  Serial.println();
  
  digitalWrite(LED_BUILTIN, HIGH);
}

// Sends the client an HTML document to pulse one of the pH pumps for the default time
void httpFormAutoPulsePump() {
  // Ensure the system is not disabled
  if (!systemEnabled) {
    httpHandleUnavailable();
    return;
  }
  
  digitalWrite(LED_BUILTIN, LOW);

  // Send HTML to client
  server.send(200, "text/html", autoPulseForm);
  
  // LOGGER
  Serial.println("/auto_pulse");
  
  digitalWrite(LED_BUILTIN, HIGH);
}

// Receives a request from the client to pulse one of the pH pumps for a specified time
void httpHandlePulsePump() {
  // Ensure the system is not disabled
  if (!systemEnabled) {
    httpHandleUnavailable();
    return;
  }
  
  digitalWrite(LED_BUILTIN, LOW);
  
  if (server.hasArg("pulseLen") && server.hasArg("pump")) {
    // Create JSON
    StaticJsonDocument<128> doc;
    doc["time"] = timeClient.getEpochTime();
    doc["pulseLen"] = (unsigned long)server.arg("pulseLen").toInt();
    doc["pump"] = server.arg("pump").toInt();
    doc["ph"] = round(pH.read_ph() * 100.0) / 100.0;

    if (doc["pump"] == 1 || doc["pump"] == 2) {
      
      if (!settings.autoPh) {
        // Pulse pump and send JSON to client
        pulsePump((doc["pump"] == 1) ? PUMP_PH_DOWN : PUMP_PH_UP, doc["pulseLen"]);
        char buffer[256 + 1];
        serializeJson(doc, &buffer, 256);
        server.send(200, "application/json", buffer);
      } else {
        server.send(500, "text/plain", "cannot pulse in auto ph mode");
      }
    
      Serial.print("/api/pulse: ");
      serializeJson(doc, Serial);
      Serial.println();
    }
  } else {
    server.send(500, "text/plain", "POST needs args pulseLen and pump");
  }
  digitalWrite(LED_BUILTIN, HIGH);
}

// Sends the client the current pH in JSON
void httpHandleJsonMailbox() {
  // Ensure the system is not disabled
  if (!systemEnabled) {
    httpHandleUnavailable();
    return;
  }
  
  digitalWrite(LED_BUILTIN, LOW);
  
  // Create JSON
  StaticJsonDocument<256> doc;
  doc["time"] = timeClient.getEpochTime();
  doc["ph"] = round(pH.read_ph() * 100.0) / 100.0;
  for (size_t i = 0; i < numPulseEvents; ++i) {
    struct PumpPulseEvent *event = &recentPumpPulses[i];
    doc["pulse_events"][i]["time"] = event->timestamp;
    doc["pulse_events"][i]["type"] = event->type;
    doc["pulse_events"][i]["len"] = event->length;
    doc["pulse_events"][i]["interrupt"] = event->interrupted;
  }
  
  // Send JSON to client
  char buffer[256 + 1];
  serializeJson(doc, &buffer, 256);
  server.send(200, "application/json", buffer);

  // Clear buffer of recent events
  numPulseEvents = 0;

  // LOGGER
  Serial.print("/json/mailbox.json: ");
  serializeJson(doc, Serial);
  Serial.println();
  
  digitalWrite(LED_BUILTIN, HIGH);
}

//---------------------
// OLED display functions
//---------------------

//void displayReadings(float ph, int ppm) {
//  display.setTextSize(2);
//  display.setCursor(0, 32);
//  display.print(" pH: ");
//  display.println(ph);
//  display.setCursor(0, 49);
//  display.print(" PPM: ");
//  display.println("");  // TODO: Display ppm when available
//}
//
//void displaySettings() {
//  if (settings.autoPh) {
//    display.fillCircle(10, 7, 7, 1);
//  } else {
//    display.drawCircle(10, 7, 7, 1);
//  }
//    display.drawCircle(118, 7, 7, 1);
//    display.drawCircle(74, 7, 7, 1);
//}
//
//void initDisplay() {
//  display.clearDisplay();
//  display.setTextColor(WHITE);
//  display.setTextSize(2);
//  display.println("  A     R");
//  display.setCursor(54, 0);
//  display.print("C");
//  display.setCursor(92, 16);
//  display.setTextSize(1);
//  display.print("REFILL");
//  display.setCursor(0, 16);
//  display.print("AUTOpH");
//  display.setCursor(37, 20);
//  display.print("CIRCULATE");
//
//  displayReadings(pH.read_ph(), 0); // TODO: Display ppm when available
//  displaySettings();
//
//  display.display();
//}
//
//void updateDisplay() {
//    display.setTextSize(2);
//    display.setFont(NULL);
//    display.setCursor(0, 32);
//    display.println(" pH: 6.19");
//    display.setCursor(0, 49);
//    display.println(" PPM: 660");
//    display.drawCircle(10, 7, 7, 1);
//    display.drawCircle(118, 7, 7, 1);
//    display.drawCircle(74, 7, 7, 1);
//    display.setTextSize(1);
//    display.display();
//}

//---------------------
// Arduino functions
//---------------------

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  // Start serial device
  Serial.begin(19200);
  while (!Serial) {
    continue;
  }
  Serial.println();

  // Connect to WiFi network
  Serial.print("Connecting to WiFi...");
  WiFi.begin(STASSID, STAPSK);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Connected, IP Address: ");
  Serial.println(WiFi.localIP());

  // Connect to NTP server
  timeClient.begin();
  if (timeClient.update()) {
    Serial.print(timeClient.getFormattedTime());
    Serial.println(", NTP setup complete");
  } else {
    Serial.println("Failed to get time from NTP server");
  }

  // Setup pH meter
  if (pH.begin()) {
    Serial.print(timeClient.getFormattedTime());
    Serial.println(", pH meter setup complete");
  } else {
    Serial.print(timeClient.getFormattedTime());
    Serial.println(", pH meter failed to setup");
  }
  
  // Setup pump pins
  pinMode(PUMP0, OUTPUT);
  digitalWrite(PUMP0, HIGH);
  pinMode(PUMP1, OUTPUT);
  digitalWrite(PUMP1, HIGH);
  pinMode(PUMP2, OUTPUT);
  digitalWrite(PUMP2, HIGH);

  // Setup float sensor pins
  pinMode(OVERFLOW_SENSOR, INPUT_PULLUP);

  // Setup buttons
  pinMode(DISABLE_BUTTON, INPUT_PULLUP);
  pinMode(DISABLED_LED, OUTPUT);
  digitalWrite(DISABLED_LED, LOW);

  // Start mDNS to resolve hostname
  const String HOSTNAME = "hydro";
  if (!MDNS.begin(HOSTNAME)) {
    Serial.println("Error setting up MDNS responder!");
    while (1) { delay(1000); }
  }
  Serial.print(timeClient.getFormattedTime());
  Serial.print(", mDNS responder started with hostname ");
  Serial.println(HOSTNAME);

  // Setup HTTP server
  server.on("/", HTTP_GET, httpHandleRoot);
  server.on("/read", HTTP_GET, httpFormRead);
  server.on("/pulse", HTTP_GET, httpFormPulsePump);
  server.on("/auto_pulse", HTTP_GET, httpFormAutoPulsePump);
  server.on("/settings", HTTP_GET, httpFormSettings);
  server.on("/api/read", HTTP_GET, httpHandleRead);
  server.on("/api/settings", HTTP_GET, httpHandleSettings);
  server.on("/api/write_settings", HTTP_POST, httpHandleWriteSettings);
  server.on("/api/save_settings", HTTP_POST, httpHandleSaveSettings);
  server.on("/api/pulse", HTTP_POST, httpHandlePulsePump);
  server.on("/api/auto_pulse", HTTP_POST, httpHandleAutoPulsePump);
  server.on("/json/mailbox.json", HTTP_GET, httpHandleJsonMailbox);
  server.onNotFound(httpHandleNotFound);
  server.begin();
  Serial.print(timeClient.getFormattedTime());
  Serial.println(", HTTP server started");

  // Initialize non-volatile storage for stuff such as settings or logs
  // Start directly after the 1KB used for pH calibration settings
  EEPROM.begin(EEPROM_START + SETTINGS_SIZE);

  // Load saved settings
  struct Settings initialSettings;
  EEPROM.get(EEPROM_START, initialSettings);
  
  // Ensure loaded settings are not out of range
  // Resort to defaults if any are
  if (!settingsIsValid(initialSettings)) {
    settings.crc32 = crc32((uint8_t *)&settings, sizeof(settings) - sizeof(uint32_t));
    EEPROM.put(EEPROM_START, settings);
    EEPROM.commit();
    Serial.print("Loaded settings from EEPROM are invalid. Using defaults: ");
  } else {
    settings = initialSettings;
    Serial.print("Loaded Settings: ");
  }

  // Print out loaded settings to serial
  StaticJsonDocument<256> doc;
  doc["time"] = timeClient.getEpochTime();
  doc["autoPh"] = settings.autoPh;
  doc["refillMode"] = settings.refillMode;
  doc["phCheckInterval"] = settings.phCheckInterval;
  doc["phPumpDoseLength"] = settings.phPumpDoseLength;
  doc["refillDoseLength"] = settings.refillDoseLength;
  serializeJson(doc, Serial);
  Serial.println();

  // Turn on refill pump if in continuous mode
  // TODO: This is a bad idea; easy to make mistakes and spill water. FIXIT somehow
  if (settings.autoPh && settings.refillMode == REFILL_CIRCULATE) {
    digitalWrite(PUMP_REFILL, LOW);
  }

  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
//  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
//    Serial.println("SSD1306 allocation failed");
//  } else {
//    initDisplay();
//  }
  
  digitalWrite(LED_BUILTIN, HIGH);
}

void loop() {
  // Update NTP
  timeClient.update();

  // Update mDNS
  MDNS.update();

  // Handle any HTTP requests
  server.handleClient();

  // Check if disable button is pressed
  unsigned long currentTime = timeClient.getEpochTime();
  if (currentTime - lastDisable > DISABLE_DELAY && digitalRead(DISABLE_BUTTON) == LOW) {
    // Disable or enable system
    lastDisable = currentTime;
    systemEnabled = !systemEnabled;
    digitalWrite(DISABLED_LED, !systemEnabled);

    // Update refill pump
    digitalWrite(PUMP_REFILL, !(systemEnabled && settings.autoPh && settings.refillMode == REFILL_CIRCULATE));

    // LOGGER
    Serial.print(timeClient.getFormattedTime());
    Serial.print(": SYSTEM ");
    Serial.println((systemEnabled) ? "ENABLED" : "DISABLED");
  }

  // Disable pumps if overflow sensor is set
  if (digitalRead(OVERFLOW_SENSOR) == HIGH) {
    digitalWrite(PUMP0, HIGH);
    digitalWrite(PUMP1, HIGH);
    digitalWrite(PUMP2, HIGH);
  } else if (systemEnabled && settings.autoPh && currentTime - lastPhCheck > settings.phCheckInterval) {
    // Ensure ph is in range
    lastPhCheck = currentTime;

    bringPhInRange();
  }
}
