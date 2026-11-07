#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "wifi_manager.h"

static void state_changed(const wifi_manager_status_t *status, void *context)
{
    (void)context;
    ESP_LOGI("station", "state=%d attempts=%lu reason=%u error=%s",
             (int)status->state, (unsigned long)status->attempts,
             status->disconnect_reason, esp_err_to_name(status->last_error));
}

void app_main(void)
{
    /* NVS recovery belongs to the application. Do not erase unrelated settings
       automatically if initialization fails. */
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_manager_config_t config;
    wifi_manager_config_default(&config);
    config.ipv6_linklocal = true;
    wifi_manager_handle_t manager = NULL;
    ESP_ERROR_CHECK(wifi_manager_create(&config, state_changed, NULL, &manager));

    esp_err_t err = wifi_manager_start(manager, NULL, WIFI_MANAGER_STORAGE_FLASH);
    if (err == ESP_ERR_NOT_FOUND) {
        /* Example placeholders only. Real products should provision secrets. */
        wifi_manager_credentials_t credentials = {0};
        const char *ssid = "example-network";
        const char *password = "example-password";
        credentials.ssid_len = strlen(ssid);
        credentials.password_len = strlen(password);
        memcpy(credentials.ssid, ssid, credentials.ssid_len);
        memcpy(credentials.password, password, credentials.password_len);
        err = wifi_manager_start(manager, &credentials, WIFI_MANAGER_STORAGE_RAM);
    }
    ESP_ERROR_CHECK(err);
    err = wifi_manager_wait_ready(manager, 30000);
    if (err == ESP_ERR_TIMEOUT) {
        ESP_LOGW("station", "startup wait expired; recovery continues in background");
    } else {
        ESP_ERROR_CHECK(err);
    }
    /* Keep manager alive for recovery. To hand the initialized driver to an
       APSTA provisioning portal, call wifi_manager_stop() from this task first.
       Stop the portal before restarting; destroy only after all driver users exit. */
}
