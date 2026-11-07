#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct wifi_manager *wifi_manager_handle_t;
typedef enum {
  WIFI_MANAGER_STOPPED,
  WIFI_MANAGER_CONNECTING,
  WIFI_MANAGER_WAITING_FOR_IP,
  WIFI_MANAGER_READY,
  WIFI_MANAGER_RETRY_WAIT,
  WIFI_MANAGER_FAILED
} wifi_manager_state_t;
typedef enum {
  WIFI_MANAGER_STORAGE_RAM,
  WIFI_MANAGER_STORAGE_FLASH
} wifi_manager_storage_t;
typedef struct {
  uint8_t ssid[32];
  size_t ssid_len;
  uint8_t password[63];
  size_t password_len;
  bool allow_open;
} wifi_manager_credentials_t;
typedef struct {
  uint32_t association_timeout_ms, ip_timeout_ms, retry_initial_ms,
      retry_max_ms, stable_reset_ms;
  bool ipv6_linklocal;
} wifi_manager_config_t;
typedef struct {
  wifi_manager_state_t state;
  uint32_t attempts;
  uint16_t disconnect_reason;
  esp_err_t last_error;
  uint32_t ipv4, netmask, gateway;
  uint64_t generation;
} wifi_manager_status_t;
typedef void (*wifi_manager_callback_t)(const wifi_manager_status_t *status,
                                        void *context);
/* Application initializes NVS/netif/default event loop. Callback runs on
 * worker; may get_status, but must not call lifecycle operations or wait_ready.
 * Start requires STOPPED; stop is idempotent. Destroy requires exclusive caller
 * access and no portal driver use. Timeout of wait_ready leaves recovery
 * running. NULL credentials selects saved FLASH credentials; absent credentials
 * returns ESP_ERR_NOT_FOUND. Explicit byte lengths preserve full 32-byte SSIDs;
 * after a RAM-configured run NULL credentials returns ESP_ERR_INVALID_STATE
 * until destroy/recreate; explicit credentials can restart directly.
 * embedded NUL bytes are unsupported in SSIDs and passphrases.
 * Lifecycle/wait operations must run from an application task, never the ESP
 * default event-loop task (they may wait for that loop to process a barrier).
 * IPv4 fields use ESP-IDF's network byte order representation.
 */
void wifi_manager_config_default(wifi_manager_config_t *config);
esp_err_t wifi_manager_create(const wifi_manager_config_t *config,
                              wifi_manager_callback_t callback, void *context,
                              wifi_manager_handle_t *out);
esp_err_t wifi_manager_start(wifi_manager_handle_t manager,
                             const wifi_manager_credentials_t *credentials,
                             wifi_manager_storage_t storage);
esp_err_t wifi_manager_wait_ready(wifi_manager_handle_t manager,
                                  uint32_t timeout_ms);
esp_err_t wifi_manager_get_status(wifi_manager_handle_t manager,
                                  wifi_manager_status_t *out);
esp_err_t wifi_manager_stop(wifi_manager_handle_t manager);
esp_err_t wifi_manager_destroy(wifi_manager_handle_t manager);
#ifdef __cplusplus
}
#endif
#endif
