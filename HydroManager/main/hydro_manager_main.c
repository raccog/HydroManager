/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define STACK_SIZE 1024

void core0_loop(void *pvParameters) {
    TickType_t lastWakeTime = xTaskGetTickCount();
    for (;;) {
        printf("Hello world!\n");
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(1000));
    }
}

void core1_loop(void *pvParameters) {
    TickType_t lastWakeTime = xTaskGetTickCount();
    for (;;) {
        printf("Goodbye world!\n");
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(1000));
    }
}

void app_main(void)
{
    xTaskCreate(&core0_loop, "core0_loop", STACK_SIZE, NULL, 1, NULL);
    xTaskCreate(&core1_loop, "core1_loop", STACK_SIZE, NULL, 1, NULL);
}
