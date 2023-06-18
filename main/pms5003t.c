#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "pms5003t.h"
#include "driver/uart.h"
#include "esp_types.h"
#include "esp_event.h"
#include "esp_err.h"


#define PMS5003_RUNTIME_PARSE_BUFFER_SIZE (2)
#define PMS5003_UART_RX_BUFFER_SIZE (256)
#define PMS5003_UART_TX_BUFFER_SIZE (256)
#define PMS5003_EVENT_LOOP_QUEUE_SIZE (8)

static const char *PMS5003_TAG = "PMS5003_parser";
ESP_EVENT_DEFINE_BASE(PMS5003_EVENT);

typedef struct {
    uart_port_t uart_port;
    uint8_t *buffer;
    esp_event_loop_handle_t event_loop_handle;
    TaskHandle_t task_handle;
    QueueHandle_t queue_handle;

    int read_len;
    uint16_t checksum;
    uint16_t message_len;
    int field_index;
    pms5003T_reading_t reading;

} pms5003_runtime_t;

static void pms5003_read_measurement(pms5003_runtime_t *pms5003_runtime) {
    pms5003_runtime->read_len = uart_read_bytes(pms5003_runtime->uart_port, pms5003_runtime->buffer, 1, 100 / portTICK_PERIOD_MS);
    if (pms5003_runtime->read_len && pms5003_runtime->buffer[0] == 0x42) {
        //check = 0x42;
        pms5003_runtime->read_len = uart_read_bytes(pms5003_runtime->uart_port, pms5003_runtime->buffer, 1, 100 / portTICK_PERIOD_MS);
        if (pms5003_runtime->read_len && pms5003_runtime->buffer[0] == 0x4d) {
            //check += 0x4d;
            pms5003_runtime->read_len = uart_read_bytes(pms5003_runtime->uart_port, pms5003_runtime->buffer, 2,
                                                        100 / portTICK_PERIOD_MS);
            if (pms5003_runtime->read_len == 2) {
                //check += data[0];
                //check += data[1];
                pms5003_runtime->message_len = (pms5003_runtime->buffer[0] << 8) | pms5003_runtime->buffer[1];
                pms5003_runtime->field_index = 0;
                pms5003_runtime->checksum = 0;
                while (pms5003_runtime->message_len > 2) {
                    pms5003_runtime->read_len = uart_read_bytes(pms5003_runtime->uart_port, pms5003_runtime->buffer, 2,
                                                                100 / portTICK_PERIOD_MS);
                    if (pms5003_runtime->read_len) {
                        pms5003_runtime->checksum += pms5003_runtime->buffer[0];
                        pms5003_runtime->checksum += pms5003_runtime->buffer[1];
                        switch (pms5003_runtime->field_index) {
                            case 0:
                                pms5003_runtime->reading.standard.pm_1_0 =
                                        (pms5003_runtime->buffer[0] << 8) | pms5003_runtime->buffer[1];
                                break;
                            case 1:
                                pms5003_runtime->reading.standard.pm_2_5 =
                                        (pms5003_runtime->buffer[0] << 8) | pms5003_runtime->buffer[1];
                                break;
                            case 2:
                                pms5003_runtime->reading.standard.pm_10_0 =
                                        (pms5003_runtime->buffer[0] << 8) | pms5003_runtime->buffer[1];
                                break;
                            case 3:
                                pms5003_runtime->reading.atmospheric.pm_1_0 =
                                        (pms5003_runtime->buffer[0] << 8) | pms5003_runtime->buffer[1];
                                break;
                            case 4:
                                pms5003_runtime->reading.atmospheric.pm_2_5 =
                                        (pms5003_runtime->buffer[0] << 8) | pms5003_runtime->buffer[1];
                                break;
                            case 5:
                                pms5003_runtime->reading.atmospheric.pm_10_0 =
                                        (pms5003_runtime->buffer[0] << 8) | pms5003_runtime->buffer[1];
                                break;
                            case 6:
                                pms5003_runtime->reading.raw_pm_0_3 =
                                        (pms5003_runtime->buffer[0] << 8) | pms5003_runtime->buffer[1];
                                break;
                            case 7:
                                pms5003_runtime->reading.raw_pm_0_5 =
                                        (pms5003_runtime->buffer[0] << 8) | pms5003_runtime->buffer[1];
                                break;
                            case 8:
                                pms5003_runtime->reading.raw_pm_1_0 =
                                        (pms5003_runtime->buffer[0] << 8) | pms5003_runtime->buffer[1];
                                break;
                            case 9:
                                pms5003_runtime->reading.raw_pm_2_5 =
                                        (pms5003_runtime->buffer[0] << 8) | pms5003_runtime->buffer[1];
                                break;
                            case 10:
                                pms5003_runtime->reading.temperature =
                                        (pms5003_runtime->buffer[0] << 8) | pms5003_runtime->buffer[1];
                                break;
                            case 11:
                                pms5003_runtime->reading.humidity =
                                        (pms5003_runtime->buffer[0] << 8) | pms5003_runtime->buffer[1];
                                break;
                            case 12:
                                pms5003_runtime->reading.voc =
                                        (pms5003_runtime->buffer[0] << 8) | pms5003_runtime->buffer[1];
                                break;
                        }
                        pms5003_runtime->field_index++;

                    }
                    pms5003_runtime->message_len -= 2;
                }
                pms5003_runtime->read_len = uart_read_bytes(pms5003_runtime->uart_port, pms5003_runtime->buffer, 2,
                                                            100 / portTICK_PERIOD_MS);
                if (pms5003_runtime->read_len) {
                    pms5003_runtime->checksum -= pms5003_runtime->buffer[0] << 0;
                    pms5003_runtime->checksum -= pms5003_runtime->buffer[1];
                }
                esp_event_post_to(pms5003_runtime->event_loop_handle, PMS5003_EVENT, PMS5003T_READING,
                                  &(pms5003_runtime->reading), sizeof(pms5003T_reading_t), 100 / portTICK_PERIOD_MS);
            }
        }
    }
}

static void pms5003_task_entry(void *arg) {
    pms5003_runtime_t *pms5003_runtime = (pms5003_runtime_t *)arg;
    uart_event_t event;
    while (1) {
        if (xQueueReceive(pms5003_runtime->queue_handle, &event, pdMS_TO_TICKS(1000))) {
            switch (event.type) {
                case UART_DATA:
                    pms5003_read_measurement(pms5003_runtime);
                    break;
                case UART_FIFO_OVF:
                    ESP_LOGW(PMS5003_TAG, "%d HW FIFO Overflow", pms5003_runtime->uart_port);
                    uart_flush(pms5003_runtime->uart_port);
                    xQueueReset(pms5003_runtime->queue_handle);
                    break;
                case UART_BUFFER_FULL:
                    ESP_LOGW(PMS5003_TAG, "%d Ring Buffer Full", pms5003_runtime->uart_port);
                    uart_flush(pms5003_runtime->uart_port);
                    xQueueReset(pms5003_runtime->queue_handle);
                    break;
                case UART_BREAK:
                    ESP_LOGW(PMS5003_TAG, "%d Rx Break", pms5003_runtime->uart_port);
                    break;
                case UART_PARITY_ERR:
                    ESP_LOGE(PMS5003_TAG, "%d Parity Error", pms5003_runtime->uart_port);
                    break;
                case UART_FRAME_ERR:
                    ESP_LOGE(PMS5003_TAG, "%d Frame Error", pms5003_runtime->uart_port);
                    break;
                default:
                    ESP_LOGW(PMS5003_TAG, "%d unknown uart event type: %d", pms5003_runtime->uart_port, event.type);
                    break;
            }
        }
        esp_event_loop_run(pms5003_runtime->event_loop_handle, pdMS_TO_TICKS(50));
    }
    vTaskDelete(NULL);
}


pms5003_handle_t pms5003_init(const pms5003_config_t *config) {
    pms5003_runtime_t *pms5003_runtime = calloc(1, sizeof(pms5003_runtime_t));
    if (!pms5003_runtime) {
        ESP_LOGE(PMS5003_TAG, "calloc for pms5003 runtime struct failed");
        goto error_struct;
    }

    pms5003_runtime->buffer = calloc(1, PMS5003_RUNTIME_PARSE_BUFFER_SIZE);
    if (!pms5003_runtime->buffer) {
        ESP_LOGE(PMS5003_TAG, "calloc for pms5003 runtime buffer failed");
        goto error_buffer;
    }

    pms5003_runtime->uart_port = config->uart.uart_port;
    uart_config_t uart_config = {
            .baud_rate = config->uart.baud_rate,
            .data_bits = config->uart.data_bits,
            .parity = config->uart.parity,
            .stop_bits = config->uart.stop_bits,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
            .source_clk = UART_SCLK_DEFAULT,
    };

    if (uart_driver_install(pms5003_runtime->uart_port, PMS5003_UART_RX_BUFFER_SIZE, PMS5003_UART_TX_BUFFER_SIZE, config->uart.event_queue_size, &pms5003_runtime->queue_handle, 0) != ESP_OK) {
        ESP_LOGE(PMS5003_TAG, "install uart driver failed");
        goto error_uart_install;
    }
    if (uart_param_config(pms5003_runtime->uart_port, &uart_config) != ESP_OK) {
        ESP_LOGE(PMS5003_TAG, "uart config failed");
        goto error_uart_config;
    }
    if (uart_set_pin(pms5003_runtime->uart_port, config->uart.tx_pin, config->uart.rx_pin,
                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
        ESP_LOGE(PMS5003_TAG, "uart pin config failed");
        goto error_uart_config;
    }

    uart_flush(pms5003_runtime->uart_port);

    esp_event_loop_args_t event_loop_args = {
            .queue_size = PMS5003_EVENT_LOOP_QUEUE_SIZE,
            .task_name = NULL
    };

    if (esp_event_loop_create(&event_loop_args, &pms5003_runtime->event_loop_handle) != ESP_OK) {
        ESP_LOGE(PMS5003_TAG, "event loop creation failed");
        goto error_events;
    }

    BaseType_t taskErr = xTaskCreate(pms5003_task_entry, "PMS5003_sensor", configMINIMAL_STACK_SIZE, pms5003_runtime,
                                     2, &pms5003_runtime->task_handle);

    if (taskErr != pdTRUE) {
        ESP_LOGE(PMS5003_TAG, "pms5003 reader task creation failed");
        goto error_task_create;
    }

    ESP_LOGI(PMS5003_TAG, "Started PMS5003 task");
    return pms5003_runtime;

    error_task_create:
        esp_event_loop_delete(pms5003_runtime->event_loop_handle);
    error_events:
    error_uart_install:
        uart_driver_delete(pms5003_runtime->uart_port);
    error_uart_config:
    error_buffer:
        free(pms5003_runtime->buffer);
    error_struct:
        free(pms5003_runtime);
    return NULL;
}

esp_err_t pms5003_deinit(pms5003_handle_t pms_handle) {
    pms5003_runtime_t *pms5003_runtime = (pms5003_runtime_t *)pms_handle;
    vTaskDelete(pms5003_runtime->task_handle);
    esp_event_loop_delete(pms5003_runtime->event_loop_handle);
    esp_err_t err = uart_driver_delete(pms5003_runtime->uart_port);
    free(pms5003_runtime->buffer);
    free(pms5003_runtime);
    return err;
}

esp_err_t pms5003_add_handler(pms5003_handle_t pms_handle, esp_event_handler_t event_handler, void *handler_args) {
    pms5003_runtime_t *pms5003_runtime = (pms5003_runtime_t *)pms_handle;
    return esp_event_handler_register_with(pms5003_runtime->event_loop_handle, PMS5003_EVENT, ESP_EVENT_ANY_ID,
                                           event_handler, handler_args);
}

esp_err_t pms5003_remove_handler(pms5003_handle_t pms_handle, esp_event_handler_t event_handler) {
    pms5003_runtime_t *pms5003_runtime = (pms5003_runtime_t *)pms_handle;
    return esp_event_handler_unregister_with(pms5003_runtime->event_loop_handle, PMS5003_EVENT, ESP_EVENT_ANY_ID, event_handler);
}