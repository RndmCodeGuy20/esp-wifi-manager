#include "wifi_manager.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "policy.hpp"
#include <atomic>
#include <cstring>
#include <mutex>
#include <new>

ESP_EVENT_DEFINE_BASE(WIFI_MANAGER_BARRIER);
namespace {
std::mutex singleton_mutex;
bool singleton = false;
uint64_t now_ms() { return static_cast<uint64_t>(esp_timer_get_time()) / 1000; }
wifi_policy::Config policy_config(const wifi_manager_config_t &c) {
  return {c.association_timeout_ms, c.ip_timeout_ms, c.retry_initial_ms,
          c.retry_max_ms, c.stable_reset_ms};
}
struct Message {
  wifi_policy::Event event;
  uint64_t generation;
  uint16_t reason;
  esp_netif_ip_info_t ip;
};
} // namespace
struct wifi_manager {
  wifi_manager_config_t config;
  wifi_policy::Policy policy;
  wifi_manager_callback_t callback;
  void *context;
  std::mutex lifecycle, status_mutex;
  wifi_manager_status_t status{};
  esp_netif_t *netif = nullptr;
  QueueHandle_t queue = nullptr;
  SemaphoreHandle_t done = nullptr, barrier = nullptr, exited = nullptr;
  TaskHandle_t worker = nullptr;
  esp_event_handler_instance_t wifi_handler = nullptr, ip_handler = nullptr,
                               barrier_handler = nullptr;
  std::atomic<uint64_t> generation{0};
  std::atomic<bool> accepting{false}, overflow{false};
  std::atomic<int> command{0}; // 1 start, 2 stop, 3 exit
  esp_err_t result = ESP_OK;
  bool ram_credentials_used = false;
  bool initialized = false, attached = false, handlers = false,
       driver_started = false, pending_disconnect = false;
  uint64_t disconnect_deadline = 0;
  esp_err_t observable_error = ESP_OK;
  esp_netif_ip_info_t ip{};
  explicit wifi_manager(const wifi_manager_config_t &c,
                        wifi_manager_callback_t cb, void *ctx)
      : config(c), policy(policy_config(c)), callback(cb), context(ctx) {}
};
namespace {
void bridge(void *arg, esp_event_base_t base, int32_t id, void *data) {
  auto *m = static_cast<wifi_manager *>(arg);
  if (base == WIFI_MANAGER_BARRIER) {
    xSemaphoreGive(m->barrier);
    return;
  }
  if (!m->accepting.load())
    return;
  Message msg{};
  msg.generation = m->generation.load();
  if (base == WIFI_EVENT) {
    if (id == WIFI_EVENT_STA_CONNECTED)
      msg.event = wifi_policy::Event::associated;
    else if (id == WIFI_EVENT_STA_DISCONNECTED) {
      msg.event = wifi_policy::Event::disconnected;
      msg.reason = static_cast<wifi_event_sta_disconnected_t *>(data)->reason;
    } else
      return;
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    auto *e = static_cast<ip_event_got_ip_t *>(data);
    if (e->esp_netif != m->netif)
      return;
    msg.event = wifi_policy::Event::got_ip;
    msg.ip = e->ip_info;
  } else if (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP) {
    // IDF 5.4 posts ip_event_got_ip_t for lost IPv4 too.
    if (!data || static_cast<ip_event_got_ip_t *>(data)->esp_netif != m->netif)
      return;
    msg.event = wifi_policy::Event::lost_ip;
  } else
    return;
  if (xQueueSend(m->queue, &msg, 0) != pdTRUE)
    m->overflow = true;
  xTaskNotifyGive(m->worker);
}
esp_err_t fence(wifi_manager *m) {
  esp_err_t e =
      esp_event_post(WIFI_MANAGER_BARRIER, 0, nullptr, 0, portMAX_DELAY);
  if (e != ESP_OK)
    return e;
  return xSemaphoreTake(m->barrier, portMAX_DELAY) == pdTRUE ? ESP_OK
                                                             : ESP_FAIL;
}
void publish(wifi_manager *m) {
  const auto &s = m->policy.snapshot();
  wifi_manager_status_t status{};
  status.state = static_cast<wifi_manager_state_t>(s.state);
  status.attempts = s.attempts;
  status.generation = s.generation;
  status.last_error = m->observable_error ? m->observable_error : s.error;
  status.disconnect_reason = s.reason;
  if (s.has_ip) {
    status.ipv4 = m->ip.ip.addr;
    status.netmask = m->ip.netmask.addr;
    status.gateway = m->ip.gw.addr;
  } else
    m->ip = {};
  {
    std::lock_guard<std::mutex> lock(m->status_mutex);
    m->status = status;
  }
  if (m->callback)
    m->callback(&status, m->context);
}
uint32_t random_sample(wifi_manager *m, uint64_t now) {
  auto s = m->policy.snapshot();
  uint32_t failures = s.failures == UINT32_MAX ? UINT32_MAX : s.failures + 1;
  if (s.state == wifi_policy::State::ready && s.deadline && now >= s.deadline)
    failures = 1;
  uint32_t ceiling =
      wifi_policy::retry_ceiling(policy_config(m->config), failures);
  uint64_t width = uint64_t(ceiling) - ceiling / 2 + 1;
  uint64_t limit = (UINT64_C(1) << 32) / width * width;
  uint32_t r;
  do {
    r = esp_random();
  } while (uint64_t(r) >= limit);
  return r;
}
void apply(wifi_manager *m, wifi_policy::Event event, int error = 0,
           uint16_t reason = 0) {
  uint64_t now = now_ms();
  auto a = m->policy.apply({event, m->policy.generation(), now,
                            random_sample(m, now), error, reason});
  m->generation = m->policy.generation();
  if (a.disconnect) {
    m->pending_disconnect = true;
    m->disconnect_deadline = now > UINT64_MAX - m->config.association_timeout_ms
                                 ? UINT64_MAX
                                 : now + m->config.association_timeout_ms;
    esp_err_t e = esp_wifi_disconnect();
    if (e == ESP_ERR_WIFI_NOT_CONNECT) {
      m->pending_disconnect = false;
      e = fence(m);
      xQueueReset(m->queue);
    }
    if (e != ESP_OK) {
      m->observable_error = e;
      m->pending_disconnect = false;
      m->policy.apply(
          {wifi_policy::Event::fatal, m->policy.generation(), now, 0, e, 0});
      publish(m);
      return;
    }
  }
  if (a.connect) {
    esp_err_t e = esp_wifi_connect();
    if (e != ESP_OK)
      apply(m, wifi_policy::Event::connect_failed, e);
  }
  if (a.changed)
    publish(m);
}
void worker(void *arg) {
  auto *m = static_cast<wifi_manager *>(arg);
  for (;;) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10) ? pdMS_TO_TICKS(10) : 1);
    int cmd = m->command.exchange(0);
    if (cmd == 3) {
      xSemaphoreGive(m->exited);
      vTaskDelete(nullptr);
      return;
    }
    if (cmd == 2) {
      m->accepting = false;
      esp_err_t e = m->driver_started ? esp_wifi_stop() : ESP_OK;
      if (e == ESP_OK)
        m->driver_started = false;
      esp_err_t f = fence(m);
      xQueueReset(m->queue);
      m->overflow = false;
      m->pending_disconnect = false;
      m->observable_error = e != ESP_OK ? e : f;
      apply(m, wifi_policy::Event::stop);
      publish(m);
      m->result = e != ESP_OK ? e : f;
      xSemaphoreGive(m->done);
      continue;
    }
    if (cmd == 1) {
      m->observable_error = ESP_OK;
      m->accepting = true;
      apply(m, wifi_policy::Event::start);
      m->result = ESP_OK;
      xSemaphoreGive(m->done);
    }
    if (m->overflow.exchange(false)) {
      m->observable_error = ESP_ERR_NO_MEM;
      // Reconcile via a deliberate driver stop/start, fencing all lost events.
      m->accepting = false;
      esp_err_t e = esp_wifi_stop();
      if (e == ESP_OK)
        m->driver_started = false;
      esp_err_t f = fence(m);
      xQueueReset(m->queue);
      m->pending_disconnect = false;
      if (e == ESP_OK)
        e = f;
      if (e == ESP_OK) {
        e = esp_wifi_start();
        if (e == ESP_OK)
          m->driver_started = true;
      }
      m->accepting = true;
      apply(m,
            e == ESP_OK ? wifi_policy::Event::disconnected
                        : wifi_policy::Event::fatal,
            e == ESP_OK ? ESP_ERR_NO_MEM : e);
      publish(m);
    }
    Message msg{};
    while (xQueueReceive(m->queue, &msg, 0) == pdTRUE) {
      if (msg.generation != m->policy.generation())
        continue;
      if (m->pending_disconnect) {
        if (msg.event == wifi_policy::Event::disconnected) {
          esp_err_t e = fence(m);
          xQueueReset(m->queue);
          m->pending_disconnect = false;
          if (e != ESP_OK)
            apply(m, wifi_policy::Event::fatal, e);
        }
        continue;
      }
      if (msg.event == wifi_policy::Event::disconnected) {
        esp_err_t e = fence(m);
        xQueueReset(m->queue);
        if (e != ESP_OK) {
          apply(m, wifi_policy::Event::fatal, e);
          continue;
        }
      }
      bool ip_changed_ready = false;
      if (msg.event == wifi_policy::Event::got_ip) {
        ip_changed_ready =
            m->policy.snapshot().state == wifi_policy::State::ready &&
            (m->ip.ip.addr != msg.ip.ip.addr ||
             m->ip.netmask.addr != msg.ip.netmask.addr ||
             m->ip.gw.addr != msg.ip.gw.addr);
        m->ip = msg.ip;
      }
      if (msg.event == wifi_policy::Event::associated &&
          m->config.ipv6_linklocal) {
        esp_err_t e = esp_netif_create_ip6_linklocal(m->netif);
        if (e != ESP_OK)
          m->observable_error = e;
      }
      apply(m, msg.event, 0, msg.reason);
      if (ip_changed_ready)
        publish(m);
    }
    if (m->pending_disconnect && now_ms() >= m->disconnect_deadline) {
      m->pending_disconnect = false;
      m->observable_error = ESP_ERR_TIMEOUT;
      m->policy.apply({wifi_policy::Event::fatal, m->policy.generation(),
                       now_ms(), 0, ESP_ERR_TIMEOUT, 0});
      publish(m);
    }
    auto s = m->policy.snapshot();
    if (s.deadline && now_ms() >= s.deadline &&
        !(s.state == wifi_policy::State::retry_wait && m->pending_disconnect))
      apply(m, wifi_policy::Event::timer, ESP_ERR_TIMEOUT);
  }
}
esp_err_t request(wifi_manager *m, int command) {
  m->command = command;
  xTaskNotifyGive(m->worker);
  xSemaphoreTake(m->done, portMAX_DELAY);
  return m->result;
}
void cleanup(wifi_manager *m) {
  m->accepting = false;
  if (m->wifi_handler)
    esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                          m->wifi_handler);
  if (m->ip_handler)
    esp_event_handler_instance_unregister(IP_EVENT, ESP_EVENT_ANY_ID,
                                          m->ip_handler);
  if (m->barrier_handler) {
    fence(m);
    esp_event_handler_instance_unregister(WIFI_MANAGER_BARRIER, 0,
                                          m->barrier_handler);
  }
  if (m->worker) {
    m->command = 3;
    xTaskNotifyGive(m->worker);
    xSemaphoreTake(m->exited, portMAX_DELAY);
  }
  if (m->initialized)
    esp_wifi_deinit();
  if (m->attached)
    esp_wifi_clear_default_wifi_driver_and_handlers(m->netif);
  if (m->netif)
    esp_netif_destroy(m->netif);
  if (m->queue)
    vQueueDelete(m->queue);
  if (m->done)
    vSemaphoreDelete(m->done);
  if (m->barrier)
    vSemaphoreDelete(m->barrier);
  if (m->exited)
    vSemaphoreDelete(m->exited);
  delete m;
}
bool forbidden(wifi_manager *m) {
  return xTaskGetCurrentTaskHandle() == m->worker;
}
} // namespace
extern "C" {
void wifi_manager_config_default(wifi_manager_config_t *c) {
  if (c)
    *c = {15000, 15000, 1000, 30000, 60000, false};
}
esp_err_t wifi_manager_create(const wifi_manager_config_t *config,
                              wifi_manager_callback_t cb, void *ctx,
                              wifi_manager_handle_t *out) {
  if (!out)
    return ESP_ERR_INVALID_ARG;
  *out = nullptr;
  wifi_manager_config_t c{};
  wifi_manager_config_default(&c);
  if (config)
    c = *config;
  if (!wifi_policy::valid_config(policy_config(c)))
    return ESP_ERR_INVALID_ARG;
  std::lock_guard<std::mutex> lock(singleton_mutex);
  if (singleton)
    return ESP_ERR_INVALID_STATE;
  auto *m = new (std::nothrow) wifi_manager(c, cb, ctx);
  if (!m)
    return ESP_ERR_NO_MEM;
  esp_err_t e = ESP_ERR_NO_MEM;
  m->queue = xQueueCreate(24, sizeof(Message));
  m->done = xSemaphoreCreateBinary();
  m->barrier = xSemaphoreCreateBinary();
  m->exited = xSemaphoreCreateBinary();
  if (!m->queue || !m->done || !m->barrier || !m->exited) {
    cleanup(m);
    return e;
  }
  wifi_mode_t existing_mode;
  e = esp_wifi_get_mode(&existing_mode);
  if (e == ESP_OK) {
    cleanup(m);
    return ESP_ERR_INVALID_STATE;
  }
  if (e != ESP_ERR_WIFI_NOT_INIT) {
    cleanup(m);
    return e;
  }
  wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  e = esp_wifi_init(&init);
  if (e != ESP_OK) {
    cleanup(m);
    return e;
  }
  m->initialized = true;
  esp_netif_config_t nc = ESP_NETIF_DEFAULT_WIFI_STA();
  m->netif = esp_netif_new(&nc);
  if (!m->netif) {
    cleanup(m);
    return ESP_ERR_NO_MEM;
  }
  m->attached = true;
  e = esp_netif_attach_wifi_station(m->netif);
  if (e != ESP_OK) {
    cleanup(m);
    return e;
  }
  e = esp_wifi_set_default_wifi_sta_handlers();
  if (e != ESP_OK) {
    cleanup(m);
    return e;
  }
  e = esp_event_handler_instance_register(WIFI_MANAGER_BARRIER, 0, bridge, m,
                                          &m->barrier_handler);
  if (e == ESP_OK)
    e = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                            bridge, m, &m->wifi_handler);
  if (e == ESP_OK)
    e = esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, bridge,
                                            m, &m->ip_handler);
  if (e != ESP_OK) {
    cleanup(m);
    return e;
  }
  if (xTaskCreate(worker, "wifi_manager", 6144, m, 5, &m->worker) != pdPASS) {
    cleanup(m);
    return ESP_ERR_NO_MEM;
  }
  singleton = true;
  *out = m;
  return ESP_OK;
}
esp_err_t wifi_manager_start(wifi_manager_handle_t m,
                             const wifi_manager_credentials_t *credentials,
                             wifi_manager_storage_t storage) {
  if (!m)
    return ESP_ERR_INVALID_ARG;
  if (forbidden(m))
    return ESP_ERR_INVALID_STATE;
  std::lock_guard<std::mutex> lock(m->lifecycle);
  {
    std::lock_guard<std::mutex> status_lock(m->status_mutex);
    if (m->status.state != WIFI_MANAGER_STOPPED)
      return ESP_ERR_INVALID_STATE;
  }
  if (m->driver_started)
    return ESP_ERR_INVALID_STATE;
  if (!credentials && m->ram_credentials_used)
    return ESP_ERR_INVALID_STATE;
  if (storage != WIFI_MANAGER_STORAGE_RAM &&
      storage != WIFI_MANAGER_STORAGE_FLASH)
    return ESP_ERR_INVALID_ARG;
  if (credentials &&
      !wifi_policy::valid_credentials(
          credentials->ssid, credentials->ssid_len, credentials->password,
          credentials->password_len, credentials->allow_open))
    return ESP_ERR_INVALID_ARG;
  esp_err_t e = esp_wifi_set_storage(
      !credentials || storage == WIFI_MANAGER_STORAGE_FLASH ? WIFI_STORAGE_FLASH
                                                            : WIFI_STORAGE_RAM);
  if (e != ESP_OK)
    return e;
  wifi_config_t wc{};
  if (credentials) {
    std::memcpy(wc.sta.ssid, credentials->ssid, credentials->ssid_len);
    std::memcpy(wc.sta.password, credentials->password,
                credentials->password_len);
  } else {
    e = esp_wifi_get_config(WIFI_IF_STA, &wc);
    if (e != ESP_OK)
      return e;
    size_t n = strnlen(reinterpret_cast<char *>(wc.sta.ssid),
                       sizeof(wc.sta.ssid)),
           p = strnlen(reinterpret_cast<char *>(wc.sta.password),
                       sizeof(wc.sta.password));
    if (!n)
      return ESP_ERR_NOT_FOUND;
    if (!wifi_policy::valid_credentials(wc.sta.ssid, n, wc.sta.password, p,
                                        false))
      return ESP_ERR_INVALID_ARG;
  }
  e = esp_wifi_set_mode(WIFI_MODE_STA);
  if (e != ESP_OK)
    return e;
  if (credentials) {
    e = esp_wifi_set_config(WIFI_IF_STA, &wc);
    if (e != ESP_OK)
      return e;
    if (storage == WIFI_MANAGER_STORAGE_RAM)
      m->ram_credentials_used = true;
  }
  e = fence(m);
  if (e != ESP_OK)
    return e;
  xQueueReset(m->queue);
  e = esp_wifi_start();
  if (e != ESP_OK)
    return e;
  m->driver_started = true;
  return request(m, 1);
}
esp_err_t wifi_manager_get_status(wifi_manager_handle_t m,
                                  wifi_manager_status_t *out) {
  if (!m || !out)
    return ESP_ERR_INVALID_ARG;
  std::lock_guard<std::mutex> lock(m->status_mutex);
  *out = m->status;
  return ESP_OK;
}
esp_err_t wifi_manager_wait_ready(wifi_manager_handle_t m, uint32_t timeout) {
  if (!m)
    return ESP_ERR_INVALID_ARG;
  if (forbidden(m))
    return ESP_ERR_INVALID_STATE;
  uint64_t start = now_ms();
  for (;;) {
    wifi_manager_status_t s{};
    wifi_manager_get_status(m, &s);
    if (s.state == WIFI_MANAGER_READY)
      return ESP_OK;
    if (s.state == WIFI_MANAGER_STOPPED || s.state == WIFI_MANAGER_FAILED)
      return ESP_ERR_INVALID_STATE;
    if (now_ms() - start >= timeout)
      return ESP_ERR_TIMEOUT;
    const TickType_t delay_ticks = pdMS_TO_TICKS(1);
    vTaskDelay(delay_ticks ? delay_ticks : 1);
  }
}
esp_err_t wifi_manager_stop(wifi_manager_handle_t m) {
  if (!m)
    return ESP_ERR_INVALID_ARG;
  if (forbidden(m))
    return ESP_ERR_INVALID_STATE;
  std::lock_guard<std::mutex> lock(m->lifecycle);
  return request(m, 2);
}
esp_err_t wifi_manager_destroy(wifi_manager_handle_t m) {
  if (!m)
    return ESP_ERR_INVALID_ARG;
  if (forbidden(m))
    return ESP_ERR_INVALID_STATE;
  esp_err_t e = wifi_manager_stop(m);
  if (e != ESP_OK)
    return e;
  std::lock_guard<std::mutex> lock(singleton_mutex);
  cleanup(m);
  singleton = false;
  return ESP_OK;
}
}
