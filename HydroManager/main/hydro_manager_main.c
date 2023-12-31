/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <inttypes.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <driver/i2c.h>
#include <ads111x.h>
#include <bmp280.h>
#include <ssd1306.h>
#include <nvs_flash.h>
#include <esp_netif.h>
#include <esp_netif_sntp.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_http_server.h>
#include <cJSON.h>

//#include <esp_system.h>

//---------------------
// Type Definitions
//---------------------

struct StructVersion {
    uint8_t major;
    uint8_t minor;
};

struct SystemSettings {
    uint32_t magic;
    struct StructVersion version;
    uint8_t auto_ph;
    uint8_t refill_mode;
    uint32_t ph_stabilize_interval;
    uint32_t ph_dose_length;
    uint32_t refill_dose_length;
};

enum SystemCommandType {
    CMD_READING_REQUEST,
    CMD_SETTINGS_UPDATE,
};

struct SystemCommand {
    enum SystemCommandType cmd_type;
    union {
        struct SystemSettings updated_settings;
    };
};

struct SensorReading {
    time_t timestamp;
    float ph;
    float temp;
    float humidity;
    uint32_t tds;
};

struct SystemResponse {
    enum SystemCommandType cmd_type;
    union {
        struct SensorReading reading;
    };
};

struct PhCalibration {
    float ph_7;
    float ph_4;
    float ph_10;
};

//------------------
// Pin definitions
//------------------

// I2C0 is used for sensors (ADS1115 and BME280)
#define I2C0_PORT 0
#define I2C0_FREQ_HZ (100 * 1000) // 100kHz
#define I2C0_SDA 26
#define I2C0_SCL 27

// I2C1 is used for display (SSD1306)
#define I2C1_PORT 1
#define I2C1_FREQ_HZ (100 * 1000) // 100kHz
#define I2C1_SDA 23
#define I2C1_SCL 22

//---------------
// Constants
//---------------

// Auto ph mode values
#define AUTO_PH_OFF 0
#define AUTO_PH_ON 1

// Refill mode values
#define REFILL_OFF 0
#define REFILL_ON 1
#define REFILL_CIRCULATE 2

// Local timezone
#define TIMEZONE "EST5EDT" 

// NTP server address
#define NTP_SERVER_ADDR "pool.ntp.org"

// Timeout when waiting for NTP response
#define NTP_TIMEOUT (pdMS_TO_TICKS(30000))

// I2C Address for ADS1115 when ADDR is connected to GND
#define ADS1115_ADDR ADS111X_ADDR_GND
// Use +-4.096v gain; there will be no signals above 3.3v or below 0v
#define ADS1115_GAIN ADS111X_GAIN_4V096

// Readings from the ADS1115 should take a maximux of 40 milliseconds
#define ADS1115_TIMEOUT (pdMS_TO_TICKS(40 * 2))

// I2C address for BME280 when SDO is connected to GND
#define BME280_ADDR BMP280_I2C_ADDRESS_0

// Readings from the BME280 should take a maximum of 10 milliseconds
#define BME280_TIMEOUT (pdMS_TO_TICKS(10 * 2))

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

#define WIFI_SSID CONFIG_HYDRO_MANAGER_SSID
#define WIFI_PASSWORD CONFIG_HYDRO_MANAGER_PASSWORD

// Max number of WiFi connection attempts
#define MAX_WIFI_RETRIES 10

// Stack size for each task
#define STACK_SIZE 2048

// Tag used for ESP logging functions
const char *TAG = "HydroManager";

//------------------
// Global Variables
//------------------

// I2C used for sensors
i2c_dev_t i2c0_dev = {0};
// I2C used for display
i2c_config_t i2c1_conf = {
    .mode = I2C_MODE_MASTER,
    .sda_io_num = I2C1_SDA,
    .sda_pullup_en = GPIO_PULLUP_ENABLE,
    .scl_io_num = I2C1_SCL,
    .scl_pullup_en = GPIO_PULLUP_ENABLE,
    .master.clk_speed = I2C1_FREQ_HZ,
    .clk_flags = I2C_SCLK_SRC_FLAG_FOR_NOMAL
};
// BME280 temp/humidity/pressure sensor
bmp280_t bme280_dev = {0};
bmp280_params_t bme280_params = {0};
// SSD1306 display
ssd1306_handle_t ssd1306_dev = NULL;

// Global system settings
struct SystemSettings g_system_settings = {
    .magic = 0xc0ffee15,
    .version = {
        .major = 1,
        .minor = 0
    },
    .auto_ph = AUTO_PH_ON,
    .refill_mode = REFILL_OFF,
    .ph_stabilize_interval = 30 * 60 * 1000,    // 30 minutes
    .ph_dose_length = 1000,                     // 1 second
    .refill_dose_length = 30 * 1000             // 30 seconds
};

// Global pH calibration
struct PhCalibration g_ph_cal = {
    .ph_7 = 1500.0f,
    .ph_4 = 2030.0f,
    .ph_10 = 975.0f,
};

// FreeRTOS event group to signal when we are connected
EventGroupHandle_t g_wifi_event_group;

// Queue of system commands
QueueHandle_t g_system_command_queue;

// Queue of system responses; one for each async worker and one
// for a single synchronous worker
QueueHandle_t g_system_response_queue;

// Current number of WiFi connection attempts
int g_wifi_retried = 0;

// Mutex for reading from ADC
SemaphoreHandle_t g_adc_mutex;

// Mutex for reading from BME280
SemaphoreHandle_t g_bme280_mutex;

// Much of this code is based off the examples in https://github.com/espressif/esp-idf/tree/master/examples
//
// * Wifi connection - https://github.com/espressif/esp-idf/blob/4fc2e5cb95/examples/wifi/getting_started/station/main/station_example_main.c
// * HTTP server - https://github.com/espressif/esp-idf/blob/master/examples/protocols/http_server/async_handlers/main/main.c

//------------------------
// WiFi Functions
//------------------------

void wifi_system_event_handler(void *arg, esp_event_base_t event_base,
        int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        // Connect to AP when WiFi station has started
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        // Try to reconnect to WiFi when it disconnects
        if (g_wifi_retried < MAX_WIFI_RETRIES) {
            esp_wifi_connect();
            g_wifi_retried += 1;
            ESP_LOGI(TAG, "Trying to reconnect to AP");
        } else {
            // Signal if WiFi fails to disconnect many times in a row
            xEventGroupSetBits(g_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG, "Failed to reconnect to the AP");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        // Signal if IP address was assigned
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        g_wifi_retried = 0;
        xEventGroupSetBits(g_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// TODO: Find better way to signal if device is currently connected to WiFi AP
void wifi_init() {
    // Initialize WiFi event group
    g_wifi_event_group = xEventGroupCreate();

    // Initialize networking stack
    ESP_ERROR_CHECK(esp_netif_init());

    // Start default event loop for system events
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Start default WiFi station
    esp_netif_create_default_wifi_sta();

    // Initialize WiFi
    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_config));

    // Register WiFi system event handlers
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                ESP_EVENT_ANY_ID, &wifi_system_event_handler, NULL,
                &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                IP_EVENT_STA_GOT_IP, &wifi_system_event_handler, NULL,
                &instance_got_ip));

    // Finish initializing WiFi station
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD
        }
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "WiFi station initialized");

    // Wait for AP to assign IP
    EventBits_t bits = xEventGroupWaitBits(g_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to AP");
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to AP");
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}

//------------------
// Sensor Functions
//------------------

esp_err_t adc_read(int mux, int16_t *raw_out) {
    if (mux < 0 || mux > 3) {
        ESP_LOGE(TAG, "adc_read: invalid mux (%d)", mux);
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Start ADC reading from mux %d", mux);

    if (xSemaphoreTake(g_adc_mutex, ADS1115_TIMEOUT) == pdFALSE) {
        return ESP_ERR_TIMEOUT;
    }

    ESP_ERROR_CHECK(ads111x_set_input_mux(&i2c0_dev, ADS111X_MUX_0_GND + mux));
    ESP_ERROR_CHECK(ads111x_start_conversion(&i2c0_dev));

    bool busy = true;
    while (busy) {
        ESP_ERROR_CHECK(ads111x_is_busy(&i2c0_dev, &busy));
    }

    ESP_ERROR_CHECK(ads111x_get_value(&i2c0_dev, raw_out));

    xSemaphoreGive(g_adc_mutex);

    ESP_LOGI(TAG, "Finished ADC reading on mux %d; got raw value %d", mux, *raw_out);

    return ESP_OK;
}

float adc_raw_to_volts(int16_t raw) {
    return ads111x_gain_values[ADS1115_GAIN] / ADS111X_MAX_VALUE * raw;
}

esp_err_t bme280_read(float *temp, float *humidity) {
    ESP_LOGI(TAG, "Start BME280 reading");

    if (xSemaphoreTake(g_bme280_mutex, BME280_TIMEOUT) == pdFALSE) {
        return ESP_ERR_TIMEOUT;
    }

    float _pressure;
    ESP_ERROR_CHECK(bmp280_read_float(&bme280_dev, temp, &_pressure, humidity));

    xSemaphoreGive(g_bme280_mutex);

    ESP_LOGI(TAG, "Finished BME280 reading: T(%f C), H(%f %%)", *temp, *humidity);

    return ESP_OK;
}

//------------------------
// HTTP Server Functions
//------------------------

esp_err_t handle_http_api_readings(httpd_req_t *req) {
    ESP_LOGI(TAG, "/api/readings.json");

    // 15 second timeout
    const TickType_t timeout = pdMS_TO_TICKS(15000);

    // Send system command to core 0
    struct SystemCommand cmd = {
        .cmd_type = CMD_READING_REQUEST,
    };
    if (xQueueSendToBack(g_system_command_queue, &cmd, timeout) == errQUEUE_FULL) {
        ESP_LOGE(TAG, "System command queue is full");
        return ESP_ERR_TIMEOUT;
    }

    // Wait for sensor reading from core 0
    struct SystemResponse response;
    if (xQueueReceive(g_system_response_queue, &response, timeout) == pdFALSE) {
        ESP_LOGE(TAG, "Timeout while waiting for system response");
        return ESP_ERR_TIMEOUT;
    }

    if (response.cmd_type != CMD_READING_REQUEST) {
        ESP_LOGE(TAG, "System response is an unexepcted type");
        return ESP_ERR_INVALID_STATE;
    }

    // Serialize JSON response
    cJSON *json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "time", time(NULL));
    cJSON_AddNumberToObject(json, "ph", response.reading.ph);
    cJSON_AddNumberToObject(json, "tds", response.reading.tds);
    cJSON_AddNumberToObject(json, "temp", response.reading.temp);
    cJSON_AddNumberToObject(json, "humidity", response.reading.humidity);
    char *json_response = cJSON_Print(json);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_response, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

httpd_handle_t start_http_server() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    // Set HTTP server to run only on core 1
    config.core_id = 1;

    httpd_handle_t server = NULL;

    // Start http server
    ESP_ERROR_CHECK(httpd_start(&server, &config));

    const httpd_uri_t api_readings_uri = {
        .uri = "/api/readings.json",
        .method = HTTP_GET,
        .handler = handle_http_api_readings,
    };

    // Register URI handlers
    httpd_register_uri_handler(server, &api_readings_uri);

    ESP_LOGI(TAG, "HTTP server started.");

    return server;
}

esp_err_t stop_http_server(httpd_handle_t server) {
    return httpd_stop(server);
}

void refresh_sntp() {
    // Connect to SNTP server
    if (esp_netif_sntp_sync_wait(NTP_TIMEOUT) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get system time from SNTP server");
        return;
    }

    time_t timestamp;
    time(&timestamp);
    char strftime_buf[64];
    struct tm timeinfo;

    localtime_r(&timestamp, &timeinfo);
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGI(TAG, "Datetime: %s", strftime_buf);
}

void wifi_connect_handler(void *arg, esp_event_base_t event_base,
        int32_t event_id, void *event_data) {
    // Get time from SNTP server
    refresh_sntp();

    // Start HTTP server
    httpd_handle_t *server = (httpd_handle_t *)arg;
    if (*server == NULL) {
        ESP_LOGI(TAG, "Starting HTTP server");
        *server = start_http_server();
    }
}

void wifi_disconnect_handler(void *arg, esp_event_base_t event_base,
        int32_t event_id, void *event_data) {
    httpd_handle_t *server = (httpd_handle_t *)arg;
    if (*server) {
        ESP_LOGI(TAG, "Stopping HTTP server");
        if (stop_http_server(*server) == ESP_OK) {
            *server = NULL;
        } else {
            ESP_LOGE(TAG, "Failed to stop HTTP server");
        }
    }
}

//-----------------------------
// System Command Functions
//-----------------------------

void system_send_reading() {
    ESP_LOGI(TAG, "Sending system reading to queue");

    int16_t ph_raw, tds_raw;
    ESP_ERROR_CHECK(adc_read(0, &ph_raw));
    ESP_ERROR_CHECK(adc_read(1, &tds_raw));

    float ph = adc_raw_to_volts(ph_raw) * 4.0f;
    float tds = adc_raw_to_volts(tds_raw) * 1000.0f;

    float temp, humidity;
    ESP_ERROR_CHECK(bme280_read(&temp, &humidity));
    // TODO: Figure out how to round floats without using division.
    // ESP32s implement floating point division in software and it is not precise at all.
    // This makes rounding to even 1 or 2 decimal digits impossible using a simple round()
    // implementation.
    struct SystemResponse response = {
        .cmd_type = CMD_READING_REQUEST,
        .reading = {
            .ph = ph,
            .tds = (uint32_t)tds,
            .temp = temp,
            .humidity = humidity
        }
    };
    const TickType_t timeout = 10;
    if (xQueueSendToBack(g_system_response_queue, &response, timeout) == errQUEUE_FULL) {
        ESP_LOGE(TAG, "Failed to send system response; queue full");
        return;
    }
}

//--------------------------------------------------
// Initialization, Event loops, and Main Function
//--------------------------------------------------

void initialize_hardware() {
    // Initialize flash storage
    ESP_ERROR_CHECK(nvs_flash_init());

    // Initialize WiFi; blocks until IP is assigned
    // TODO: How should this system function when disconnected from WiFi?
    wifi_init();

    // Initialize I2C0
    ESP_ERROR_CHECK(i2cdev_init());
    ESP_LOGI(TAG, "I2C0 initialized.");

    // Initialize ADS1115 ADC module with the following configuration:
    //  * Single shot mode
    //  * 128 samples per second
    //  * A0-GND mux
    //  * +-4.096 volt range
    ESP_ERROR_CHECK(ads111x_init_desc(&i2c0_dev, ADS1115_ADDR, I2C0_PORT, I2C0_SDA,
                I2C0_SCL));
    i2c0_dev.cfg.master.clk_speed = I2C0_FREQ_HZ;   // Ensure I2C frequency is set
    ESP_ERROR_CHECK(ads111x_set_mode(&i2c0_dev, ADS111X_MODE_SINGLE_SHOT));
    ESP_ERROR_CHECK(ads111x_set_data_rate(&i2c0_dev, ADS111X_DATA_RATE_128));
    ESP_ERROR_CHECK(ads111x_set_input_mux(&i2c0_dev, ADS111X_MUX_0_GND));
    ESP_ERROR_CHECK(ads111x_set_gain(&i2c0_dev, ADS1115_GAIN));
    ESP_LOGI(TAG, "ADS1115 initialized.");

    // Initialize BME280 temp/humidity/pressure sensor
    ESP_ERROR_CHECK(bmp280_init_desc(&bme280_dev, BME280_ADDR, I2C0_PORT, I2C0_SDA,
                I2C0_SCL));
    ESP_ERROR_CHECK(bmp280_init_default_params(&bme280_params));
    ESP_ERROR_CHECK(bmp280_init(&bme280_dev, &bme280_params));
    ESP_LOGI(TAG, "BME280 initialized.");

    // Initialize I2C1
    ESP_ERROR_CHECK(i2c_param_config(I2C1_PORT, &i2c1_conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C1_PORT, i2c1_conf.mode, 0, 0, 0));
    ESP_LOGI(TAG, "I2C1 initialized.");

    // Initialize SSD1306 display
    ssd1306_dev = ssd1306_create(I2C1_PORT, SSD1306_I2C_ADDRESS);
    ESP_ERROR_CHECK(ssd1306_refresh_gram(ssd1306_dev));
    ssd1306_clear_screen(ssd1306_dev, 0x00);
    ESP_LOGI(TAG, "SSD1306 initialized.");

    // Test SSD1306 by displaying a string
    char data_str[10] = {0};
    sprintf(data_str, "C STR");
    ssd1306_draw_string(ssd1306_dev, 70, 16, (const uint8_t *)data_str, 16, 1);
    ESP_ERROR_CHECK(ssd1306_refresh_gram(ssd1306_dev));

    // Open flash storage handler
    nvs_handle_t nvs_handle;
    ESP_ERROR_CHECK(nvs_open("HydroManager", NVS_READWRITE, &nvs_handle));

    // Try to retrieve pH meter calibration
    size_t ph_cal_size = sizeof(struct PhCalibration);
    esp_err_t ph_cal_flash_result = nvs_get_blob(nvs_handle, "PhCalibration",
            (void *)&g_ph_cal, &ph_cal_size);
    if (ph_cal_flash_result != ESP_OK) {
        // Write default ph calibration values if they dont exist
        ESP_ERROR_CHECK(nvs_set_blob(nvs_handle, "PhCalibration",
                    (const void *)&g_ph_cal, sizeof(struct PhCalibration)));
        ESP_LOGI(TAG, "Cannot load ph calibration; Wrote default to flash");
    } else {
        ESP_LOGI(TAG, "Loaded ph calibration");
    }

    // Try to retrieve system settings
    size_t system_settings_size = sizeof(struct SystemSettings);
    esp_err_t system_settings_result = nvs_get_blob(nvs_handle, "SystemSettings",
            (void *)&g_system_settings, &system_settings_size);
    ESP_ERROR_CHECK(system_settings_result);
    if (system_settings_result != ESP_OK) {
        // Write default system settings if they dont exist
        ESP_ERROR_CHECK(nvs_set_blob(nvs_handle, "SystemSettings",
                    (const void *)&g_system_settings, sizeof(struct SystemSettings)));
        ESP_LOGI(TAG, "Cannot load system settings; Wrote default to flash");
    } else {
        ESP_LOGI(TAG, "Loaded system settings");
    }

    nvs_close(nvs_handle);
}

void initialize_resources() {
    // Create mutex for ADC
    g_adc_mutex = xSemaphoreCreateMutex();
    if (g_adc_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex for ADC");
    }

    // Create mutex for BME280
    g_bme280_mutex = xSemaphoreCreateMutex();
    if (g_bme280_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex for BME280");
    }

    // Create queue of system commands
    g_system_command_queue = xQueueCreate(1, sizeof(struct SystemCommand));
    if (g_system_command_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create queue for system commands");
    }

    // Create queue of system responses
    g_system_response_queue = xQueueCreate(1, sizeof(struct SystemResponse));
    if (g_system_response_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create queue for system responses");
    }
}

void initialize_networking() {
    // Setup handlers to start HTTP server when WiFi connects and stop it when WiFi
    // disconnects
    httpd_handle_t http_server;
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                &wifi_connect_handler, &http_server));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                &wifi_disconnect_handler, &http_server));

    // Get initial time from SNTP server
    esp_sntp_config_t sntp_config = ESP_NETIF_SNTP_DEFAULT_CONFIG(NTP_SERVER_ADDR);
    setenv("TZ", TIMEZONE, 1);
    tzset();
    ESP_ERROR_CHECK(esp_netif_sntp_init(&sntp_config));
    refresh_sntp();

    // Initialize HTTP server for the first time
    http_server = start_http_server();
}

void system_control_task(void *pvParameters) {
    for (;;) {
        // Check for system command
        if (uxQueueMessagesWaiting(g_system_command_queue) > 0) {
            // Try to get command from queue
            struct SystemCommand cmd;
            const TickType_t timeout = 10;
            if (xQueueReceive(g_system_command_queue, &cmd, timeout) == pdFALSE) {
                ESP_LOGE(TAG, "Timeout while receiving system command");
                continue;
            }

            ESP_LOGI(TAG, "Received system command");

            // Handle command
            switch (cmd.cmd_type) {
                case CMD_READING_REQUEST:
                    system_send_reading();
                    break;
                default:
                    ESP_LOGE(TAG, "Unexpected system command type");
                    break;
            }
        }

        vTaskDelay(10);
    }
}

void app_main(void)
{
    initialize_hardware();
    initialize_resources();
    initialize_networking();

    xTaskCreatePinnedToCore(&system_control_task, "system_control", STACK_SIZE, NULL, 1, NULL, 0);
}
