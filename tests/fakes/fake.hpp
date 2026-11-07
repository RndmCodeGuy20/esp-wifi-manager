#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
using esp_err_t = int;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_TIMEOUT 0x107
#define ESP_ERR_WIFI_NOT_INIT 0x3001
using wifi_mode_t = int;
esp_err_t esp_wifi_get_mode(wifi_mode_t *);
#define ESP_ERR_WIFI_NOT_CONNECT 0x300f
using esp_event_base_t = const char *;
#define ESP_EVENT_DEFINE_BASE(x) esp_event_base_t x = #x
extern esp_event_base_t WIFI_EVENT, IP_EVENT;
#define ESP_EVENT_ANY_ID -1
#define WIFI_EVENT_STA_CONNECTED 1
#define WIFI_EVENT_STA_DISCONNECTED 2
#define IP_EVENT_STA_GOT_IP 3
#define IP_EVENT_STA_LOST_IP 4
struct esp_ip4_addr_t {
  uint32_t addr;
};
struct esp_netif_ip_info_t {
  esp_ip4_addr_t ip, netmask, gw;
};
struct esp_netif_t {
  int dummy;
};
struct esp_netif_config_t {};
#define ESP_NETIF_DEFAULT_WIFI_STA()                                           \
  esp_netif_config_t {}
struct ip_event_got_ip_t {
  esp_netif_t *esp_netif;
  esp_netif_ip_info_t ip_info;
};
struct wifi_event_sta_disconnected_t {
  uint16_t reason;
};
struct wifi_init_config_t {};
#define WIFI_INIT_CONFIG_DEFAULT()                                             \
  wifi_init_config_t {}
enum wifi_auth_mode_t { WIFI_AUTH_OPEN = 0, WIFI_AUTH_WPA2_PSK = 3 };
struct wifi_config_t {
  struct {
    uint8_t ssid[32];
    uint8_t password[64];
    struct {
      int8_t rssi;
      wifi_auth_mode_t authmode;
    } threshold;
  } sta;
};
#define WIFI_IF_STA 0
#define WIFI_MODE_STA 1
#define WIFI_STORAGE_FLASH 1
#define WIFI_STORAGE_RAM 0
using esp_event_handler_t = void (*)(void *, esp_event_base_t, int32_t, void *);
using esp_event_handler_instance_t = void *;
esp_err_t esp_event_handler_instance_register(esp_event_base_t, int32_t,
                                              esp_event_handler_t, void *,
                                              esp_event_handler_instance_t *);
esp_err_t esp_event_handler_instance_unregister(esp_event_base_t, int32_t,
                                                esp_event_handler_instance_t);
esp_err_t esp_event_post(esp_event_base_t, int32_t, const void *, size_t,
                         uint32_t);
esp_err_t esp_wifi_init(const wifi_init_config_t *);
esp_err_t esp_wifi_deinit();
esp_netif_t *esp_netif_new(const esp_netif_config_t *);
void esp_netif_destroy(esp_netif_t *);
esp_err_t esp_netif_attach_wifi_station(esp_netif_t *);
esp_err_t esp_wifi_set_default_wifi_sta_handlers();
esp_err_t esp_wifi_clear_default_wifi_driver_and_handlers(void *);
esp_err_t esp_netif_create_ip6_linklocal(esp_netif_t *);
esp_err_t esp_wifi_set_storage(int);
esp_err_t esp_wifi_get_config(int, wifi_config_t *);
esp_err_t esp_wifi_set_config(int, wifi_config_t *);
esp_err_t esp_wifi_set_mode(int);
esp_err_t esp_wifi_start();
esp_err_t esp_wifi_stop();
esp_err_t esp_wifi_connect();
esp_err_t esp_wifi_disconnect();
uint32_t esp_random();
int64_t esp_timer_get_time();
using BaseType_t = int;
using TickType_t = uint32_t;
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define portMAX_DELAY UINT32_MAX
#ifndef FAKE_TICK_PERIOD_MS
#define FAKE_TICK_PERIOD_MS 1
#endif
#define pdMS_TO_TICKS(x) ((x) / FAKE_TICK_PERIOD_MS)
struct Task;
struct Queue;
struct Semaphore;
using TaskHandle_t = Task *;
using QueueHandle_t = Queue *;
using SemaphoreHandle_t = Semaphore *;
BaseType_t xTaskCreate(void (*)(void *), const char *, uint32_t, void *, int,
                       TaskHandle_t *);
void vTaskDelete(void *);
TaskHandle_t xTaskGetCurrentTaskHandle();
void xTaskNotifyGive(TaskHandle_t);
uint32_t ulTaskNotifyTake(int, uint32_t);
void vTaskDelay(uint32_t);
QueueHandle_t xQueueCreate(size_t, size_t);
BaseType_t xQueueSend(QueueHandle_t, const void *, uint32_t);
BaseType_t xQueueReceive(QueueHandle_t, void *, uint32_t);
void xQueueReset(QueueHandle_t);
void vQueueDelete(QueueHandle_t);
SemaphoreHandle_t xSemaphoreCreateBinary();
BaseType_t xSemaphoreTake(SemaphoreHandle_t, uint32_t);
void xSemaphoreGive(SemaphoreHandle_t);
void vSemaphoreDelete(SemaphoreHandle_t);
namespace fake {
extern std::atomic<int> connects, stops, resources;
extern std::atomic<int> delay_calls, zero_tick_delays;
extern std::atomic<int64_t> time;
extern std::atomic<int> connect_error, ipv6_error, stop_error, disconnect_error,
    random_calls;
extern std::atomic<bool> reject_once;
extern int storage;
extern wifi_config_t saved, active;
extern esp_netif_t *netif;
void emit(esp_event_base_t, int32_t, void *);
void fail(int);
void reset();
void advance(uint32_t);
} // namespace fake
