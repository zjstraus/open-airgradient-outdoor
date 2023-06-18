#include "esp_types.h"
#include "esp_event.h"
#include "esp_err.h"
#include "driver/uart.h"

typedef struct {
    uint16_t pm_1_0;
    uint16_t pm_2_5;
    uint16_t pm_10_0;
} pms5003_concentration_t;

typedef struct {
    pms5003_concentration_t standard;
    pms5003_concentration_t atmospheric;

    uint16_t raw_pm_0_3;
    uint16_t raw_pm_0_5;
    uint16_t raw_pm_1_0;
    uint16_t raw_pm_2_5;

    int16_t temperature;
    uint16_t humidity;
    uint16_t voc;
} pms5003T_reading_t;

typedef enum {
    MODE_ACTIVE,
    MODE_PASSIVE
} pms5003_mode_t;

typedef enum {
    SLEEP_AWAKE,
    SLEEP_SLEEP
} pms5003_sleep_t;

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

typedef void *pms5003_handle_t;

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

ESP_EVENT_DECLARE_BASE(ESP_PMS5003_EVENT);

pms5003_handle_t pms5003_init(const pms5003_config_t *config);
esp_err_t pms5003_deinit(pms5003_handle_t pms_handle);
esp_err_t pms5003_add_handler(pms5003_handle_t pms_handle, esp_event_handler_t event_handler, void *handler_args);
esp_err_t pms5003_remove_handler(pms5003_handle_t pms_handle, esp_event_handler_t event_handler);