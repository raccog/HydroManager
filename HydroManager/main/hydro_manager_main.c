/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <inttypes.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <driver/i2c.h>
#include <ads111x.h>
#include <bmp280.h>
#include "ssd1306.h"
#include <nvs_flash.h>
#include <esp_netif.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_wifi.h>

//#include <esp_http_server.h>
//#include <esp_system.h>

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

// I2C Address for ADS1115 when ADDR is connected to GND
#define ADS1115_ADDR ADS111X_ADDR_GND
// Use +-4.096v gain; there will be no signals above 3.3v or below 0v
#define ADS1115_GAIN ADS111X_GAIN_4V096

// I2C address for BME280 when SDO is connected to GND
#define BME280_ADDR BMP280_I2C_ADDRESS_0

// Stack size for each task
#define STACK_SIZE 2048

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

// FreeRTOS handles
SemaphoreHandle_t printf_sem;
// FreeRTOS event group to signal when we are connected
EventGroupHandle_t g_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

#define WIFI_SSID CONFIG_HYDRO_MANAGER_SSID
#define WIFI_PASSWORD CONFIG_HYDRO_MANAGER_PASSWORD

// Current number of WiFi connection attempts
int g_wifi_retried = 0;
// Max number of WiFi connection attempts
#define MAX_WIFI_RETRIES 10

#define TASK_PRINTF(...) \
{ \
    xSemaphoreTake(printf_sem, pdMS_TO_TICKS(1000)); \
    printf(__VA_ARGS__); \
    xSemaphoreGive(printf_sem); \
}

const char *TAG = "HydroManager";

float ads1115_read(int mux) {
    if (mux < 0 || mux > 3) {
        TASK_PRINTF("ads1115_read: invalid mux (%d)\n", mux);
    }

    // Start conversion with selected mux
    ESP_ERROR_CHECK(ads111x_set_input_mux(&i2c0_dev, ADS111X_MUX_0_GND + mux));
    ESP_ERROR_CHECK(ads111x_start_conversion(&i2c0_dev));

    // Wait for conversion
    bool busy = true;
    while (busy) {
        ESP_ERROR_CHECK(ads111x_is_busy(&i2c0_dev, &busy));
    }

    // Read conversion
    int16_t raw = 0;
    ESP_ERROR_CHECK(ads111x_get_value(&i2c0_dev, &raw));
    float voltage = ads111x_gain_values[ADS1115_GAIN] / ADS111X_MAX_VALUE * raw;

    TASK_PRINTF("ADS1115 A%d: raw (%d), %.04f volts\n", mux, raw, voltage);

    return voltage;
}

void core0_loop(void *pvParameters) {
    for (;;) {
        ads1115_read(0);
        ads1115_read(1);
        float pressure, temp, humidity;
        ESP_ERROR_CHECK(bmp280_read_float(&bme280_dev, &temp, &pressure, &humidity));
        TASK_PRINTF("T: %.2f C, P: %.2f Pa, H: %.2f %%\n", temp, pressure, humidity);
        TASK_PRINTF("-----------------------------\n");
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void core1_loop(void *pvParameters) {
    TickType_t lastWakeTime = xTaskGetTickCount();
    for (;;) {
        //TASK_PRINTF("Goodbye core %d!\n", xPortGetCoreID());
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(1000));
    }
}

void handle_wifi_got_ip(void *arg, esp_event_base_t event_base,
        int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (g_wifi_retried < MAX_WIFI_RETRIES) {
            esp_wifi_connect();
            g_wifi_retried += 1;
            TASK_PRINTF("Retry to connect to the AP.\n");
        } else {
            xEventGroupSetBits(g_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        g_wifi_retried = 0;
        xEventGroupSetBits(g_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void initialize_hardware() {
    // Initialize printf semaphore
    printf_sem = xSemaphoreCreateBinary();
    if (printf_sem == NULL) {
        printf("Failed to create printf semaphore\n");
    }

    // Initialize WiFi event group
    g_wifi_event_group = xEventGroupCreate();

    // Initialize flash storage
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    // Init WiFi
    // Taken from ESP-IDF getting started station example:
    // https://github.com/espressif/esp-idf/blob/4fc2e5cb95abb1e7339f11929bc9e916a9c3d15a/examples/wifi/getting_started/station/main/station_example_main.c
    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));

    // Setup WiFi event handlers
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                ESP_EVENT_ANY_ID, &handle_wifi_got_ip, NULL,
                &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                IP_EVENT_STA_GOT_IP, &handle_wifi_got_ip, NULL,
                &instance_got_ip));

    // Connect to WiFi
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
        }
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    printf("WiFi initialized.\n");
    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(g_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap");
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to ap");
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }

    // Initialize I2C0
    ESP_ERROR_CHECK(i2cdev_init());
    printf("I2C0 initialized.\n");

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
    printf("ADS1115 initialized.\n");

    // Initialize BME280 temp/humidity/pressure sensor
    ESP_ERROR_CHECK(bmp280_init_desc(&bme280_dev, BME280_ADDR, I2C0_PORT, I2C0_SDA,
                I2C0_SCL));
    ESP_ERROR_CHECK(bmp280_init_default_params(&bme280_params));
    ESP_ERROR_CHECK(bmp280_init(&bme280_dev, &bme280_params));
    printf("BME280 initialized.\n");

    // Initialize I2C1
    ESP_ERROR_CHECK(i2c_param_config(I2C1_PORT, &i2c1_conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C1_PORT, i2c1_conf.mode, 0, 0, 0));
    printf("I2C1 initialized.\n");

    // Initialize SSD1306 display
    ssd1306_dev = ssd1306_create(I2C1_PORT, SSD1306_I2C_ADDRESS);
    ESP_ERROR_CHECK(ssd1306_refresh_gram(ssd1306_dev));
    ssd1306_clear_screen(ssd1306_dev, 0x00);
    printf("SSD1306 initialized.\n");

    // Test SSD1306 by displaying a string
    char data_str[10] = {0};
    sprintf(data_str, "C STR");
    ssd1306_draw_string(ssd1306_dev, 70, 16, (const uint8_t *)data_str, 16, 1);
    ESP_ERROR_CHECK(ssd1306_refresh_gram(ssd1306_dev));
}

void app_main(void)
{
    initialize_hardware();

    xTaskCreatePinnedToCore(&core0_loop, "core0_loop", STACK_SIZE, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(&core1_loop, "core1_loop", STACK_SIZE, NULL, 1, NULL, 1);
}
