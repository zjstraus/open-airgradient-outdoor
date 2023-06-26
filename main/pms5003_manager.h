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