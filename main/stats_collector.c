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

#include "stats_collector.h"

#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_types.h"
#include "esp_event.h"
#include "esp_err.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "Stats collector";
ESP_EVENT_DEFINE_BASE(STATS_COLLECTOR_EVENT);

typedef struct {
    TaskStatus_t *task_status_buffer;
    esp_event_loop_handle_t event_loop;
    TaskHandle_t task_handle;
} stats_collector_runtime_t;

static void stats_collector_task_entry(void *arg) {
#if configUSE_TRACE_FACILITY
    stats_collector_runtime_t *runtime = (stats_collector_runtime_t *) arg;
    while (1) {
        vTaskDelay(30000 / portTICK_PERIOD_MS);
        int currentTasks = uxTaskGetSystemState(runtime->task_status_buffer,
                                                STATS_COLLECTOR_TASK_LIST_SIZE,
                                                NULL);
        for (int task_index = 0; task_index < currentTasks; task_index++) {
            ESP_LOGI(TAG, "(%d) %s - %d | Stack: %lu | runtime: %lu",
                     runtime->task_status_buffer[task_index].xTaskNumber,
                     runtime->task_status_buffer[task_index].pcTaskName,
                     runtime->task_status_buffer[task_index].eCurrentState,
                     runtime->task_status_buffer[task_index].usStackHighWaterMark,
                     runtime->task_status_buffer[task_index].ulRunTimeCounter);
        }
    }
#endif
}

stats_collector_handle_t stats_collector_init(esp_event_loop_handle_t event_loop)
{
#if configUSE_TRACE_FACILITY
    stats_collector_runtime_t *runtime = calloc(1, sizeof(stats_collector_runtime_t));
    if (!runtime) {
        ESP_LOGE(TAG, "calloc for stats_collector runtime struct failed");
        goto error_struct;
    }

    runtime->event_loop = event_loop;

    runtime->task_status_buffer = calloc(STATS_COLLECTOR_TASK_LIST_SIZE, sizeof(TaskStatus_t));
    if (!runtime->task_status_buffer) {
        ESP_LOGE(TAG, "calloc for stats_collector runtime buffer failed");
        goto error_buffer;
    }


    BaseType_t taskErr = xTaskCreate(stats_collector_task_entry, "stats_collector", 2048, runtime,
                                     2, &runtime->task_handle);

    if (taskErr != pdTRUE) {
        ESP_LOGE(TAG, "stats collector task creation failed");
        goto error_task_create;
    }

    ESP_LOGI(TAG, "Started stats collector task");
    return runtime;

    error_task_create:
    error_buffer:
    free(runtime->task_status_buffer);
    error_struct:
    free(runtime);
    return NULL;
#endif
    return NULL;
}