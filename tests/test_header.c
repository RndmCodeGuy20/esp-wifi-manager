#include "wifi_manager.h"

/* Compile this translation unit as C to protect the public C-compatible API. */
void check_public_header(wifi_manager_handle_t manager)
{
    wifi_manager_config_t config;
    wifi_manager_status_t status;
    wifi_manager_config_default(&config);
    (void)wifi_manager_get_status(manager, &status);
}
