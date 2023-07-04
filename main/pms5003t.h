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

#include "esp_types.h"
#include "esp_event.h"
#include "esp_err.h"
#include "driver/uart.h"

/**
 * Particle concentrations in ug/m3
 */
typedef struct {
    uint16_t pm_1_0; /*!< PM1.0 */
    uint16_t pm_2_5; /*!< PM2.5 */
    uint16_t pm_10_0; /*!< PM10.0 */
} pms5003_concentration_t;

/**
 * Individual reading off the sensor
 */
typedef struct {
    pms5003_concentration_t standard; /*!< Concentration at standard particle */
    pms5003_concentration_t atmospheric; /*!< Concentration under atmospheric conditions */

    uint16_t raw_pm_0_3; /*!< Raw number of particles larger than 0.3um in 0.1L of air */
    uint16_t raw_pm_0_5; /*!< Raw number of particles larger than 0.5um in 0.1L of air */
    uint16_t raw_pm_1_0; /*!< Raw number of particles larger than 1.0um in 0.1L of air */
    uint16_t raw_pm_2_5; /*!< Raw number of particles larger than 2.5um in 0.1L of air */

    int16_t temperature; /*!< Temperature (in tenths of a degree C) */
    uint16_t humidity; /*!< Relative Humidity (in tenths of a percent) */
    uint16_t voc; /*!< */

    char *sensor_id; /*!< Sensor name to report against */
} pms5003T_reading_t;

/**
 * Operation mode of the sensor
 */
typedef enum {
    MODE_ACTIVE, /*!< Sensor will automatically send a reading every second, default at startup */
    MODE_PASSIVE /*!< Sensor must be polled for data */
} pms5003_mode_t;

/**
 * Sleep state of the sensor
 */
typedef enum {
    SLEEP_AWAKE, /*!< Sensor is active (fan is on), default at startup */
    SLEEP_SLEEP /*!< Sensor is asleep (fan is off) */
} pms5003_sleep_t;

/**
 * Target PMS5003T sensor configuration
 */
typedef struct {
    struct {
        uart_port_t uart_port;
        uint32_t rx_pin;
        uint32_t tx_pin;
        uint32_t baud_rate;
        uart_word_length_t data_bits;
        uart_parity_t parity;
        uart_stop_bits_t stop_bits;
        uint32_t event_queue_size;
    } uart;
} pms5003_config_t;


#define PMS5003_CONFIG_DEFAULT()              \
{                                             \
    .uart = {                                 \
        .uart_port = UART_NUM_1,              \
        .rx_pin = UART_PIN_NO_CHANGE,         \
        .tx_pin = UART_PIN_NO_CHANGE,         \
        .baud_rate = 9600,                    \
        .data_bits = UART_DATA_8_BITS,        \
        .parity = UART_PARITY_DISABLE,        \
        .stop_bits = UART_STOP_BITS_1,        \
        .event_queue_size = 16                \
    }                                         \
}

ESP_EVENT_DECLARE_BASE(PMS5003_EVENT);

/**
 * Incoming events from the sensor
 */
typedef enum {
    PMS5003T_READING /*!< New reading has arrived */
} pms5003_event_id_t;

/**
 * Pointer to an initialized PMS5003 driver instance
 */
typedef void *pms5003_handle_t;

/**
 * @brief Do memory allocations and initial setup for a sensor connection
 * @param config connection configuration
 * @return pointer to PMS5003T instance
 */
pms5003_handle_t pms5003_init(const pms5003_config_t *config);

/**
 * @brief In passive mode, send a message prompting the sensor for a new reading
 * @param pms_handle pointer to PMS5003T instance
 */
void pms5003_request_read(pms5003_handle_t pms_handle);

/**
 * @brief Send a sleep state change message to the sensor
 * @details Sensor data readings will not be valid while in sleep mode or for 30s after waking
 * @param pms_handle pointer to PMS5003T instance
 * @param state sleep state to request
 */
void pms5003_request_sleep(pms5003_handle_t pms_handle, pms5003_sleep_t state);

/**
 * @brief Send a mode change message to the sensor
 * @param pms_handle pointer to PMS5003T instance
 * @param mode operational mode to request
 */
void pms5003_request_mode(pms5003_handle_t pms_handle, pms5003_mode_t mode);

/**
 * @brief Clear and clean up any allocations made for sensor instance
 * @param pms_handle pointer to PMS5003T instance
 * @return
 *  - ESP_OK: free'd sucessfully
 *  - other: failure cleaning up OS construct(s)
 */
esp_err_t pms5003_deinit(pms5003_handle_t pms_handle);

/**
 * @brief Attach a handler to the event loop for sensor readings
 * @param pms_handle pointer to PMS5003T instance
 * @param event_handler handler function
 * @param handler_args additional args to pass along with event to handler
 * @return passed through from esp_event_handler_register_with
 */
esp_err_t pms5003_add_handler(pms5003_handle_t pms_handle, esp_event_handler_t event_handler, void *handler_args);

/**
 * @brief Deattach a handler from the event loop
 * @param pms_handle pointer to PMS5003T instance
 * @param event_handler handler function
 * @return passed through from esp_event_handler_unregister_with
 */
esp_err_t pms5003_remove_handler(pms5003_handle_t pms_handle, esp_event_handler_t event_handler);