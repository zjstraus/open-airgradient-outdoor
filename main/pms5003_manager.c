/* Airgradient Outdoor Sensor V1.1 firmware using ESP-IDF
 * Copyright (C) 2023 Zach Strauss
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "pms5003_manager.h"
#include "pms5003t.h"
#include "esp_log.h"
#include "sdkconfig.h"



#define PMS5003_MANAGER_READCOUNT CONFIG_PMS5003_MANAGER_READ_COUNT
#define PMS5003_MANAGER_SPINUP_TICKS (CONFIG_PMS5003_MANAGER_SPINUP_TIME * 1000) / portTICK_PERIOD_MS
#define PMS5003_MANAGER_SLEEP_TICKS (CONFIG_PMS5003_MANAGER_SLEEP_TIME * 1000) / portTICK_PERIOD_MS

static const char *PMS5003_MANAGER_TAG = "PMS5003_manager";
ESP_EVENT_DEFINE_BASE(PMS5003_MANAGER_EVENT);

typedef struct {
    pms5003_handle_t sensor_handle;
    pms5003T_reading_t pending_reading;
    int remaining_reads;
    bool read_pending;
    TaskHandle_t task_handle;
    char *TAG;
} pms5003_manager_runtime_t;

static void pms5003_manager_clear_pending_reads(pms5003_manager_runtime_t *runtime) {
    runtime->remaining_reads = PMS5003_MANAGER_READCOUNT;
    runtime->read_pending = false;
    runtime->pending_reading.voc = 0;
    runtime->pending_reading.humidity = 0;
    runtime->pending_reading.temperature = 0;
    runtime->pending_reading.raw_pm_2_5 = 0;
    runtime->pending_reading.raw_pm_1_0 = 0;
    runtime->pending_reading.raw_pm_0_5 = 0;
    runtime->pending_reading.raw_pm_0_3 = 0;
    runtime->pending_reading.standard.pm_10_0 = 0;
    runtime->pending_reading.standard.pm_2_5 = 0;
    runtime->pending_reading.standard.pm_1_0 = 0;
    runtime->pending_reading.atmospheric.pm_10_0 = 0;
    runtime->pending_reading.atmospheric.pm_2_5 = 0;
    runtime->pending_reading.atmospheric.pm_1_0 = 0;
}

static void pms5003_manager_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id,
                                          void *event_data) {
    pms5003T_reading_t *pms5003T_reading = NULL;
    pms5003_manager_runtime_t *manager_runtime = (pms5003_manager_runtime_t *) event_handler_arg;
    if (event_base == PMS5003_EVENT) {
        switch (event_id) {
            case PMS5003T_READING:
                manager_runtime->read_pending = false;

                pms5003T_reading = (pms5003T_reading_t *) event_data;
                manager_runtime->pending_reading.voc += pms5003T_reading->voc;
                manager_runtime->pending_reading.humidity += pms5003T_reading->humidity;
                manager_runtime->pending_reading.temperature += pms5003T_reading->temperature;
                manager_runtime->pending_reading.raw_pm_2_5 += pms5003T_reading->raw_pm_2_5;
                manager_runtime->pending_reading.raw_pm_1_0 += pms5003T_reading->raw_pm_1_0;
                manager_runtime->pending_reading.raw_pm_0_5 += pms5003T_reading->raw_pm_0_5;
                manager_runtime->pending_reading.raw_pm_0_3 += pms5003T_reading->raw_pm_0_3;
                manager_runtime->pending_reading.standard.pm_10_0 += pms5003T_reading->standard.pm_10_0;
                manager_runtime->pending_reading.standard.pm_2_5 += pms5003T_reading->standard.pm_2_5;
                manager_runtime->pending_reading.standard.pm_1_0 += pms5003T_reading->standard.pm_1_0;
                manager_runtime->pending_reading.atmospheric.pm_10_0 += pms5003T_reading->atmospheric.pm_10_0;
                manager_runtime->pending_reading.atmospheric.pm_2_5 += pms5003T_reading->atmospheric.pm_2_5;
                manager_runtime->pending_reading.atmospheric.pm_1_0 += pms5003T_reading->atmospheric.pm_1_0;
                manager_runtime->remaining_reads--;
                break;
        }
    }
}

static void pms5003_manager_task_entry(void *arg) {
    pms5003_manager_runtime_t *runtime = (pms5003_manager_runtime_t *)arg;
    while (1) {
        pms5003_request_sleep(runtime->sensor_handle, SLEEP_AWAKE);
        vTaskDelay(PMS5003_MANAGER_SPINUP_TICKS);
        pms5003_manager_clear_pending_reads(runtime);
        while (runtime->remaining_reads > 0) {
            runtime->read_pending = true;
            pms5003_request_read(runtime->sensor_handle);
            while (runtime->read_pending == true) {
                vTaskDelay(250 / portTICK_PERIOD_MS);
            }
        }
        runtime->pending_reading.voc /= PMS5003_MANAGER_READCOUNT;
        runtime->pending_reading.humidity /= PMS5003_MANAGER_READCOUNT;
        runtime->pending_reading.temperature /= PMS5003_MANAGER_READCOUNT;
        runtime->pending_reading.raw_pm_2_5 /= PMS5003_MANAGER_READCOUNT;
        runtime->pending_reading.raw_pm_1_0 /= PMS5003_MANAGER_READCOUNT;
        runtime->pending_reading.raw_pm_0_5 /= PMS5003_MANAGER_READCOUNT;
        runtime->pending_reading.raw_pm_0_3 /= PMS5003_MANAGER_READCOUNT;
        runtime->pending_reading.standard.pm_10_0 /= PMS5003_MANAGER_READCOUNT;
        runtime->pending_reading.standard.pm_2_5 /= PMS5003_MANAGER_READCOUNT;
        runtime->pending_reading.standard.pm_1_0 /= PMS5003_MANAGER_READCOUNT;
        runtime->pending_reading.atmospheric.pm_10_0 /= PMS5003_MANAGER_READCOUNT;
        runtime->pending_reading.atmospheric.pm_2_5 /= PMS5003_MANAGER_READCOUNT;
        runtime->pending_reading.atmospheric.pm_1_0 /= PMS5003_MANAGER_READCOUNT;

        ESP_EARLY_LOGI(runtime->TAG,
                       "Standard (1.0: %d, 2.5: %d, 10.0: %d) Atmospheric (1.0: %d, 2.5: %d, 10.0: %d) Raw (0.3: %d, 0.5: %d, 1.0: %d, 2.5: %d) %d C %d %% %d",
                       runtime->pending_reading.standard.pm_1_0,
                       runtime->pending_reading.standard.pm_2_5,
                       runtime->pending_reading.standard.pm_10_0,
                       runtime->pending_reading.atmospheric.pm_1_0,
                       runtime->pending_reading.atmospheric.pm_2_5,
                       runtime->pending_reading.atmospheric.pm_10_0,
                       runtime->pending_reading.raw_pm_0_3,
                       runtime->pending_reading.raw_pm_0_5,
                       runtime->pending_reading.raw_pm_1_0,
                       runtime->pending_reading.raw_pm_2_5,
                       runtime->pending_reading.temperature,
                       runtime->pending_reading.humidity,
                       runtime->pending_reading.voc);
        pms5003_request_sleep(runtime->sensor_handle, SLEEP_SLEEP);
        vTaskDelay(PMS5003_MANAGER_SLEEP_TICKS);
    }
}

pms5003_manager_handle_t pms5003_manager_init(const pms5003_config_t *config, char *TAG) {
    pms5003_manager_runtime_t *runtime = calloc(1, sizeof(pms5003_manager_runtime_t));
    if (!runtime) {
        ESP_LOGE(PMS5003_MANAGER_TAG, "calloc for pms5003 manager runtime struct failed");
        goto error_struct;
    }
    runtime->TAG = TAG;

    runtime->sensor_handle = pms5003_init(config);
    if (!runtime->sensor_handle) {
        ESP_LOGE(PMS5003_MANAGER_TAG, "init of pms5003 sensor failed");
        goto error_struct;
    }

    pms5003_request_mode(runtime->sensor_handle, MODE_PASSIVE);
    pms5003_add_handler(runtime->sensor_handle, pms5003_manager_event_handler, runtime);

    BaseType_t taskErr = xTaskCreate(pms5003_manager_task_entry, "PMS5003_sensor_manager", configMINIMAL_STACK_SIZE, runtime,
                                     2, &runtime->task_handle);

    if (taskErr != pdTRUE) {
        ESP_LOGE(PMS5003_MANAGER_TAG, "pms5003 reader task creation failed");
        goto error_task_create;
    }

    ESP_LOGI(PMS5003_MANAGER_TAG, "Started PMS5003 manager task");

    return runtime;

    error_task_create:
    error_struct:
    free(runtime);
    return NULL;
}