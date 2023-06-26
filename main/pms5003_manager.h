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

#ifndef H_PMS5003T_MANAGER
#define H_PMS5003T_MANAGER

#include "pms5003t.h"

typedef void *pms5003_manager_handle_t;


ESP_EVENT_DECLARE_BASE(PMS5003_MANAGER_EVENT);
typedef enum {
    PMS5003T_MANAGER_READING
} pms5003_manager_event_id_t;

pms5003_manager_handle_t pms5003_manager_init(const pms5003_config_t *config, char *TAG);

#endif