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

static const char *PMS5003_TAG = "PMS5003T_parser";
ESP_EVENT_DEFINE_BASE(ESP_PMS5003_EVENT);

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

} esp_pms5003_t;

static void pms5003_task_entry(void *arg) {
    esp_pms5003_t *esp_pms5003 = (esp_pms5003_t *)arg;
    uart_event_t event;
    while (1) {
        if (xQueueReceive(esp_pms5003->queue_handle, &event, pdMS_TO_TICKS(100))) {
            switch (event.type) {
                case UART_DATA:
                    esp_pms5003->read_len = uart_read_bytes(esp_pms5003->uart_port, esp_pms5003->buffer, 1, 100 / portTICK_PERIOD_MS);
                    if (esp_pms5003->read_len && esp_pms5003->buffer[0] == 0x42) {
                        //check = 0x42;
                        esp_pms5003->read_len = uart_read_bytes(esp_pms5003->uart_port, esp_pms5003->buffer, 1, 100 / portTICK_PERIOD_MS);
                        if (esp_pms5003->read_len && esp_pms5003->buffer[0] == 0x4d) {
                            //check += 0x4d;
                            esp_pms5003->read_len = uart_read_bytes(esp_pms5003->uart_port, esp_pms5003->buffer, 2,
                                                                    100 / portTICK_PERIOD_MS);
                            if (esp_pms5003->read_len == 2) {
                                //check += data[0];
                                //check += data[1];
                                esp_pms5003->message_len = (esp_pms5003->buffer[0] << 8) | esp_pms5003->buffer[1];
                                esp_pms5003->field_index = 0;
                                esp_pms5003->checksum = 0;
                                while (esp_pms5003->message_len > 2) {
                                    esp_pms5003->read_len = uart_read_bytes(esp_pms5003->uart_port, esp_pms5003->buffer, 2,
                                                                            100 / portTICK_PERIOD_MS);
                                    if (esp_pms5003->read_len) {
                                        esp_pms5003->checksum += esp_pms5003->buffer[0];
                                        esp_pms5003->checksum += esp_pms5003->buffer[1];
                                        switch (esp_pms5003->field_index) {
                                            case 0:
                                                esp_pms5003->reading.standard.pm_1_0 =
                                                        (esp_pms5003->buffer[0] << 8) | esp_pms5003->buffer[1];
                                                break;
                                            case 1:
                                                esp_pms5003->reading.standard.pm_2_5 =
                                                        (esp_pms5003->buffer[0] << 8) | esp_pms5003->buffer[1];
                                                break;
                                            case 2:
                                                esp_pms5003->reading.standard.pm_10_0 =
                                                        (esp_pms5003->buffer[0] << 8) | esp_pms5003->buffer[1];
                                                break;
                                            case 3:
                                                esp_pms5003->reading.atmospheric.pm_1_0 =
                                                        (esp_pms5003->buffer[0] << 8) | esp_pms5003->buffer[1];
                                                break;
                                            case 4:
                                                esp_pms5003->reading.atmospheric.pm_2_5 =
                                                        (esp_pms5003->buffer[0] << 8) | esp_pms5003->buffer[1];
                                                break;
                                            case 5:
                                                esp_pms5003->reading.atmospheric.pm_10_0 =
                                                        (esp_pms5003->buffer[0] << 8) | esp_pms5003->buffer[1];
                                                break;
                                            case 6:
                                                esp_pms5003->reading.raw_pm_0_3 =
                                                        (esp_pms5003->buffer[0] << 8) | esp_pms5003->buffer[1];
                                                break;
                                            case 7:
                                                esp_pms5003->reading.raw_pm_0_5 =
                                                        (esp_pms5003->buffer[0] << 8) | esp_pms5003->buffer[1];
                                                break;
                                            case 8:
                                                esp_pms5003->reading.raw_pm_1_0 =
                                                        (esp_pms5003->buffer[0] << 8) | esp_pms5003->buffer[1];
                                                break;
                                            case 9:
                                                esp_pms5003->reading.raw_pm_2_5 =
                                                        (esp_pms5003->buffer[0] << 8) | esp_pms5003->buffer[1];
                                                break;
                                            case 10:
                                                esp_pms5003->reading.temperature =
                                                        (esp_pms5003->buffer[0] << 8) | esp_pms5003->buffer[1];
                                                break;
                                            case 11:
                                                esp_pms5003->reading.humidity =
                                                        (esp_pms5003->buffer[0] << 8) | esp_pms5003->buffer[1];
                                                break;
                                            case 12:
                                                esp_pms5003->reading.voc =
                                                        (esp_pms5003->buffer[0] << 8) | esp_pms5003->buffer[1];
                                                break;
                                        }
                                        esp_pms5003->field_index++;

                                    }
                                    esp_pms5003->message_len -= 2;
                                }
                                esp_pms5003->read_len = uart_read_bytes(esp_pms5003->uart_port, esp_pms5003->buffer, 2,
                                                                        100 / portTICK_PERIOD_MS);
                                if (esp_pms5003->read_len) {
                                    esp_pms5003->checksum -= esp_pms5003->buffer[0] << 0;
                                    esp_pms5003->checksum -= esp_pms5003->buffer[1];
                                }
                                ESP_EARLY_LOGI(PMS5003_TAG,
                                               "%d: Standard (1.0: %d, 2.5: %d, 10.0: %d) Atmospheric (1.0: %d, 2.5: %d, 10.0: %d) Raw (0.3: %d, 0.5: %d, 1.0: %d, 2.5: %d) %d C %d %% %d",
                                               esp_pms5003->uart_port,
                                               esp_pms5003->reading.standard.pm_1_0,
                                               esp_pms5003->reading.standard.pm_2_5,
                                               esp_pms5003->reading.standard.pm_10_0,
                                               esp_pms5003->reading.atmospheric.pm_1_0,
                                               esp_pms5003->reading.atmospheric.pm_2_5,
                                               esp_pms5003->reading.atmospheric.pm_10_0,
                                               esp_pms5003->reading.raw_pm_0_3, esp_pms5003->reading.raw_pm_0_5,
                                               esp_pms5003->reading.raw_pm_1_0, esp_pms5003->reading.raw_pm_2_5,
                                               esp_pms5003->reading.temperature, esp_pms5003->reading.humidity,
                                               esp_pms5003->reading.voc);
                            }
                        }
                    }
                    break;
                case UART_FIFO_OVF:
                    ESP_LOGW(PMS5003_TAG, "%d HW FIFO Overflow", esp_pms5003->uart_port);
                    uart_flush(esp_pms5003->uart_port);
                    xQueueReset(esp_pms5003->queue_handle);
                    break;
                case UART_BUFFER_FULL:
                    ESP_LOGW(PMS5003_TAG, "%d Ring Buffer Full", esp_pms5003->uart_port);
                    uart_flush(esp_pms5003->uart_port);
                    xQueueReset(esp_pms5003->queue_handle);
                    break;
                case UART_BREAK:
                    ESP_LOGW(PMS5003_TAG, "%d Rx Break", esp_pms5003->uart_port);
                    break;
                case UART_PARITY_ERR:
                    ESP_LOGE(PMS5003_TAG, "%d Parity Error", esp_pms5003->uart_port);
                    break;
                case UART_FRAME_ERR:
                    ESP_LOGE(PMS5003_TAG, "%d Frame Error", esp_pms5003->uart_port);
                    break;
                default:
                    ESP_LOGW(PMS5003_TAG, "%d unknown uart event type: %d", esp_pms5003->uart_port, event.type);
                    break;
            }
        }
        esp_event_loop_run(esp_pms5003->event_loop_handle, pdMS_TO_TICKS(50));
    }
    vTaskDelete(NULL);
}


pms5003_handle_t pms5003_init(const pms5003_config_t *config) {
    esp_pms5003_t *esp_pms5003 = calloc(1, sizeof(esp_pms5003_t));
    if (!esp_pms5003) {
        ESP_LOGE(PMS5003_TAG, "calloc for pms5003 runtime struct failed");
        goto error_struct;
    }

    esp_pms5003->buffer = calloc(1, PMS5003_RUNTIME_PARSE_BUFFER_SIZE);
    if (!esp_pms5003->buffer) {
        ESP_LOGE(PMS5003_TAG, "calloc for pms5003 runtime buffer failed");
        goto error_buffer;
    }

    esp_pms5003->uart_port = config->uart.uart_port;
    uart_config_t uart_config = {
            .baud_rate = config->uart.baud_rate,
            .data_bits = config->uart.data_bits,
            .parity = config->uart.parity,
            .stop_bits = config->uart.stop_bits,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
            .source_clk = UART_SCLK_DEFAULT,
    };

    if (uart_driver_install(esp_pms5003->uart_port, PMS5003_UART_RX_BUFFER_SIZE, PMS5003_UART_TX_BUFFER_SIZE, config->uart.event_queue_size, &esp_pms5003->queue_handle, 0) != ESP_OK) {
        ESP_LOGE(PMS5003_TAG, "install uart driver failed");
        goto error_uart_install;
    }
    if (uart_param_config(esp_pms5003->uart_port, &uart_config) != ESP_OK) {
        ESP_LOGE(PMS5003_TAG, "uart config failed");
        goto error_uart_config;
    }
    if (uart_set_pin(esp_pms5003->uart_port, config->uart.tx_pin, config->uart.rx_pin,
                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
        ESP_LOGE(PMS5003_TAG, "uart pin config failed");
        goto error_uart_config;
    }

    uart_flush(esp_pms5003->uart_port);

    esp_event_loop_args_t event_loop_args = {
            .queue_size = PMS5003_EVENT_LOOP_QUEUE_SIZE,
            .task_name = NULL
    };

    if (esp_event_loop_create(&event_loop_args, &esp_pms5003->event_loop_handle) != ESP_OK) {
        ESP_LOGE(PMS5003_TAG, "event loop creation failed");
        goto error_events;
    }

    BaseType_t taskErr = xTaskCreate(pms5003_task_entry, "PMS5003_sensor", 1024, esp_pms5003,
                                     2, &esp_pms5003->task_handle);

    if (taskErr != pdTRUE) {
        ESP_LOGE(PMS5003_TAG, "pms5003 reader task creation failed");
        goto error_task_create;
    }

    ESP_LOGI(PMS5003_TAG, "Started PMS5003 task");
    return esp_pms5003;

    error_task_create:
        esp_event_loop_delete(esp_pms5003->event_loop_handle);
    error_events:
    error_uart_install:
        uart_driver_delete(esp_pms5003->uart_port);
    error_uart_config:
    error_buffer:
        free(esp_pms5003->buffer);
    error_struct:
        free(esp_pms5003);
    return NULL;
}

esp_err_t pms5003_deinit(pms5003_handle_t pms_handle) {
    esp_pms5003_t *esp_pms5003 = (esp_pms5003_t *)pms_handle;
    vTaskDelete(esp_pms5003->task_handle);
    esp_event_loop_delete(esp_pms5003->event_loop_handle);
    esp_err_t err = uart_driver_delete(esp_pms5003->uart_port);
    free(esp_pms5003->buffer);
    free(esp_pms5003);
    return err;
}

esp_err_t pms5003_add_handler(pms5003_handle_t pms_handle, esp_event_handler_t event_handler, void *handler_args) {
    esp_pms5003_t *esp_pms5003 = (esp_pms5003_t *)pms_handle;
    return esp_event_handler_register_with(esp_pms5003->event_loop_handle, ESP_PMS5003_EVENT, ESP_EVENT_ANY_ID,
                                           event_handler, handler_args);
}

esp_err_t pms5003_remove_handler(pms5003_handle_t pms_handle, esp_event_handler_t event_handler) {
    esp_pms5003_t *esp_pms5003 = (esp_pms5003_t *)pms_handle;
    return esp_event_handler_unregister_with(esp_pms5003->event_loop_handle, ESP_PMS5003_EVENT, ESP_EVENT_ANY_ID, event_handler);
}