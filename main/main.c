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
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"

#include "nvs_flash.h"
#include "driver/uart.h"

#include "sdkconfig.h"

#include "pms5003t.h"
#include "pms5003_manager.h"
#include "stats_collector.h"
#include "mqtt_client.h"

static const char *TAG = "openair_outdoor";

#define OAG_WIFI_SSID      CONFIG_ESP_WIFI_SSID
#define OAG_WIFI_PASS      CONFIG_ESP_WIFI_PASSWORD
#define OAG_WIFI_MAXIMUM_RETRY  (5)

#if CONFIG_ESP_WPA3_SAE_PWE_HUNT_AND_PECK
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HUNT_AND_PECK
#define OAG_H2E_IDENTIFIER ""
#elif CONFIG_ESP_WPA3_SAE_PWE_HASH_TO_ELEMENT
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HASH_TO_ELEMENT
#define OAG_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#elif CONFIG_ESP_WPA3_SAE_PWE_BOTH
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_BOTH
#define OAG_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#endif

#if CONFIG_ESP_WIFI_AUTH_OPEN
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN
#elif CONFIG_ESP_WIFI_AUTH_WEP
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WEP
#elif CONFIG_ESP_WIFI_AUTH_WPA_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WAPI_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WAPI_PSK
#endif

#define WIFI_CONNECTED_EVENT BIT0
#define WIFI_FAIL_EVENT BIT1

static EventGroupHandle_t wifi_event_group;
static esp_mqtt_client_handle_t mqtt_client;

static int wifi_retry_count = 0;

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (wifi_retry_count < OAG_WIFI_MAXIMUM_RETRY) {
            esp_wifi_connect();
            wifi_retry_count++;
            ESP_LOGI(TAG, "Reconnecting to wifi");
        } else {
            xEventGroupSetBits(wifi_event_group, WIFI_FAIL_EVENT);
        }
        ESP_LOGI(TAG, "Wifi connection failed");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI(TAG, "Connected to wifi with IP Address:" IPSTR, IP2STR(&event->ip_info.ip));
        wifi_retry_count = 0;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_EVENT);
    }
}

void wifi_init_sta(void) {
    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = {
            .sta = {
                    .ssid = OAG_WIFI_SSID,
                    .password = OAG_WIFI_PASS,
                    /* Authmode threshold resets to WPA2 as default if password matches WPA2 standards (pasword len => 8).
                     * If you want to connect the device to deprecated WEP/WPA networks, Please set the threshold value
                     * to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and set the password with length and format matching to
                     * WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.
                     */
                    .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
                    .sae_pwe_h2e = ESP_WIFI_SAE_MODE,
                    .sae_h2e_identifier = OAG_H2E_IDENTIFIER,
            },
    };

    /* Start Wi-Fi in station mode */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
                                           WIFI_CONNECTED_EVENT | WIFI_FAIL_EVENT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    if (bits & WIFI_CONNECTED_EVENT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 OAG_WIFI_SSID, OAG_WIFI_PASS);
    } else if (bits & WIFI_FAIL_EVENT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 OAG_WIFI_SSID, OAG_WIFI_SSID);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}


static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32 "", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT Connected");
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT Disconnected");
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
                log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
                log_error_if_nonzero("captured as transport's socket errno",  event->error_handle->esp_transport_sock_errno);
                ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
            }
            break;
        default:
            ESP_LOGI(TAG, "Other MQTT event id:%d", event->event_id);
            break;
    }
}

static void mqtt_init()
{
    esp_mqtt_client_config_t mqtt_config = {
            .broker.address.uri = CONFIG_MQTT_TARGET_URL,
            .credentials.username = CONFIG_MQTT_USERNAME,
            .credentials.authentication.password = CONFIG_MQTT_PASSOWRD,
    };

    mqtt_client = esp_mqtt_client_init(&mqtt_config);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
}

static char *mqtt_topic_buffer[256];
static char *mqtt_payload_buffer[256];
static void sensor_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == PMS5003_MANAGER_EVENT) {
        switch (event_id) {
            case PMS5003T_MANAGER_READING:
                pms5003T_reading_t * pms5003T_reading = (pms5003T_reading_t *) event_data;

                sprintf(mqtt_topic_buffer, "%s%s/temperature", CONFIG_MQTT_BASE_PATH, pms5003T_reading->sensor_id);
                sprintf(mqtt_payload_buffer, "%f", (pms5003T_reading->temperature / 10.0));
                esp_mqtt_client_enqueue(mqtt_client, mqtt_topic_buffer, mqtt_payload_buffer, 0, 0, 0, true);

                sprintf(mqtt_topic_buffer, "%s%s/humidity", CONFIG_MQTT_BASE_PATH, pms5003T_reading->sensor_id);
                sprintf(mqtt_payload_buffer, "%f", (pms5003T_reading->humidity / 10.0));
                esp_mqtt_client_enqueue(mqtt_client, mqtt_topic_buffer, mqtt_payload_buffer, 0, 0, 0, true);

                sprintf(mqtt_topic_buffer, "%s%s/raw/0.3", CONFIG_MQTT_BASE_PATH, pms5003T_reading->sensor_id);
                sprintf(mqtt_payload_buffer, "%d", pms5003T_reading->raw_pm_0_3);
                esp_mqtt_client_enqueue(mqtt_client, mqtt_topic_buffer, mqtt_payload_buffer, 0, 0, 0, true);

                sprintf(mqtt_topic_buffer, "%s%s/raw/0.5", CONFIG_MQTT_BASE_PATH, pms5003T_reading->sensor_id);
                sprintf(mqtt_payload_buffer, "%d", pms5003T_reading->raw_pm_0_5);
                esp_mqtt_client_enqueue(mqtt_client, mqtt_topic_buffer, mqtt_payload_buffer, 0, 0, 0, true);

                sprintf(mqtt_topic_buffer, "%s%s/raw/1.0", CONFIG_MQTT_BASE_PATH, pms5003T_reading->sensor_id);
                sprintf(mqtt_payload_buffer, "%d", pms5003T_reading->raw_pm_1_0);
                esp_mqtt_client_enqueue(mqtt_client, mqtt_topic_buffer, mqtt_payload_buffer, 0, 0, 0, true);

                sprintf(mqtt_topic_buffer, "%s%s/raw/2.5", CONFIG_MQTT_BASE_PATH, pms5003T_reading->sensor_id);
                sprintf(mqtt_payload_buffer, "%d", pms5003T_reading->raw_pm_2_5);
                esp_mqtt_client_enqueue(mqtt_client, mqtt_topic_buffer, mqtt_payload_buffer, 0, 0, 0, true);

                sprintf(mqtt_topic_buffer, "%s%s/standard/pm1.0", CONFIG_MQTT_BASE_PATH, pms5003T_reading->sensor_id);
                sprintf(mqtt_payload_buffer, "%d", pms5003T_reading->standard.pm_1_0);
                esp_mqtt_client_enqueue(mqtt_client, mqtt_topic_buffer, mqtt_payload_buffer, 0, 0, 0, true);

                sprintf(mqtt_topic_buffer, "%s%s/standard/pm2.5", CONFIG_MQTT_BASE_PATH, pms5003T_reading->sensor_id);
                sprintf(mqtt_payload_buffer, "%d", pms5003T_reading->standard.pm_2_5);
                esp_mqtt_client_enqueue(mqtt_client, mqtt_topic_buffer, mqtt_payload_buffer, 0, 0, 0, true);

                sprintf(mqtt_topic_buffer, "%s%s/standard/pm10.0", CONFIG_MQTT_BASE_PATH, pms5003T_reading->sensor_id);
                sprintf(mqtt_payload_buffer, "%d", pms5003T_reading->standard.pm_10_0);
                esp_mqtt_client_enqueue(mqtt_client, mqtt_topic_buffer, mqtt_payload_buffer, 0, 0, 0, true);

                sprintf(mqtt_topic_buffer, "%s%s/atmospheric/pm1.0", CONFIG_MQTT_BASE_PATH, pms5003T_reading->sensor_id);
                sprintf(mqtt_payload_buffer, "%d", pms5003T_reading->atmospheric.pm_1_0);
                esp_mqtt_client_enqueue(mqtt_client, mqtt_topic_buffer, mqtt_payload_buffer, 0, 0, 0, true);

                sprintf(mqtt_topic_buffer, "%s%s/atmospheric/pm2.5", CONFIG_MQTT_BASE_PATH, pms5003T_reading->sensor_id);
                sprintf(mqtt_payload_buffer, "%d", pms5003T_reading->atmospheric.pm_2_5);
                esp_mqtt_client_enqueue(mqtt_client, mqtt_topic_buffer, mqtt_payload_buffer, 0, 0, 0, true);

                sprintf(mqtt_topic_buffer, "%s%s/atmospheric/pm10.0", CONFIG_MQTT_BASE_PATH, pms5003T_reading->sensor_id);
                sprintf(mqtt_payload_buffer, "%d", pms5003T_reading->atmospheric.pm_10_0);
                esp_mqtt_client_enqueue(mqtt_client, mqtt_topic_buffer, mqtt_payload_buffer, 0, 0, 0, true);

                break;
        }
    }
}

void app_main(void) {
    /* Initialize NVS partition */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        /* NVS partition was truncated
         * and needs to be erased */
        ESP_ERROR_CHECK(nvs_flash_erase());

        /* Retry nvs_flash_init */
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    wifi_init_sta();
    mqtt_init();

    esp_event_loop_args_t event_loop_args = {
            .queue_size = 32,
            .task_name = NULL
    };
    esp_event_loop_handle_t main_events;
    if (esp_event_loop_create(&event_loop_args, &main_events) != ESP_OK) {
        ESP_LOGE(TAG, "main event loop creation failed");
        esp_restart();
    }

    esp_event_handler_register_with(main_events, ESP_EVENT_ANY_BASE, ESP_EVENT_ANY_ID,
                                    sensor_event_handler, NULL);

    #if configUSE_TRACE_FACILITY
    stats_collector_init(NULL);
    #endif

    pms5003_config_t config1 = PMS5003_CONFIG_DEFAULT();
    config1.uart.rx_pin = 0;
    config1.uart.tx_pin = 1;
    config1.uart.uart_port = UART_NUM_1;

//    pms5003_handle_t pms5003_handle_1 = pms5003_init(&config1);
//    pms5003_add_handler(pms5003_handle_1, pms5003_event_handler, NULL);

    pms5003_manager_handle_t pms5003_handle_1 = pms5003_manager_init(&config1, "SENS1", main_events);


    pms5003_config_t config2 = PMS5003_CONFIG_DEFAULT();
    config2.uart.uart_port = UART_NUM_0;

//    pms5003_handle_t pms5003_handle_2 = pms5003_init(&config2);
//    pms5003_add_handler(pms5003_handle_2, pms5003_event_handler, NULL);
    pms5003_manager_handle_t pms5003_handle_2 = pms5003_manager_init(&config2, "SENS0", main_events);

    while (1) {
        esp_event_loop_run(main_events, pdMS_TO_TICKS(50));
    }
}
