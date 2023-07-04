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

#pragma once

#include "esp_event.h"

#define STATS_COLLECTOR_TASK_LIST_SIZE (32)

ESP_EVENT_DECLARE_BASE(STATS_COLLECTOR_EVENT);

/**
 * Collector thrown events
 */
typedef enum {
    TASK_STATE /*!< State of a task */
} stats_collector_event_id_t;

/**
 * Pointer to an initialized stats collector instance
 */
typedef void *stats_collector_handle_t;

stats_collector_handle_t stats_collector_init(esp_event_loop_handle_t event_loop);