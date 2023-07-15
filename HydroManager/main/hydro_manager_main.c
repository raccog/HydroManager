/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <inttypes.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <driver/i2c.h>
#include <ads111x.h>
#include <bmp280.h>

//------------------
// Pin definitions
//------------------

// I2C0 is used for sensors (ADS1115 and BME280)
#define I2C0_PORT 0
#define I2C0_FREQ_HZ (100 * 1000) // 100kHz
#define I2C0_SDA 26
#define I2C0_SCL 27

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
// BME280 temp/humidity/pressure sensor
bmp280_t bme280_dev = {0};
bmp280_params_t bme280_params = {0};

SemaphoreHandle_t printf_sem;

#define TASK_PRINTF(...) \
{ \
    xSemaphoreTake(printf_sem, pdMS_TO_TICKS(1000)); \
    printf(__VA_ARGS__); \
    xSemaphoreGive(printf_sem); \
}

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

void initialize_hardware() {
    // Initialize printf semaphore
    printf_sem = xSemaphoreCreateBinary();
    if (printf_sem == NULL) {
        printf("Failed to create printf semaphore\n");
    }

    // Initialize I2C0
    ESP_ERROR_CHECK(i2cdev_init());

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

    // Initialize BME280 temp/humidity/pressure sensor
    ESP_ERROR_CHECK(bmp280_init_desc(&bme280_dev, BME280_ADDR, I2C0_PORT, I2C0_SDA,
                I2C0_SCL));
    ESP_ERROR_CHECK(bmp280_init_default_params(&bme280_params));
    ESP_ERROR_CHECK(bmp280_init(&bme280_dev, &bme280_params));

    printf("ADS1115 initialized.\n");
}

void app_main(void)
{
    initialize_hardware();

    xTaskCreatePinnedToCore(&core0_loop, "core0_loop", STACK_SIZE, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(&core1_loop, "core1_loop", STACK_SIZE, NULL, 1, NULL, 1);
}
