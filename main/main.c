#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "sdkconfig.h"
#include "pms5003t.h"


static void gps_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
}

void app_main(void)
{
    pms5003_config_t config1 = PMS5003_CONFIG_DEFAULT();
    config1.uart.rx_pin = 0;
    config1.uart.tx_pin = 1;
    config1.uart.uart_port = 1;

    pms5003_handle_t pms5003_handle_1 = pms5003_init(&config1);
    pms5003_add_handler(pms5003_handle_1, gps_event_handler, NULL);

    pms5003_config_t config2 = PMS5003_CONFIG_DEFAULT();
    config2.uart.uart_port = 0;

    pms5003_handle_t pms5003_handle_2 = pms5003_init(&config2);
    pms5003_add_handler(pms5003_handle_2, gps_event_handler, NULL);


    vTaskDelay(10000 / portTICK_PERIOD_MS);
}
