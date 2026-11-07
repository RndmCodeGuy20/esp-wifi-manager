#include "fake.hpp"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>
esp_event_base_t WIFI_EVENT = "wifi", IP_EVENT = "ip";
struct Task {
  std::mutex mutex;
  std::condition_variable cv;
  uint32_t notifications = 0;
  std::thread thread;
};
struct Semaphore {
  std::mutex mutex;
  std::condition_variable cv;
  bool set = false;
};
struct Queue {
  std::mutex mutex;
  size_t cap, size;
  std::deque<std::vector<char>> data;
};
thread_local Task *current = nullptr;
namespace {
struct Handler {
  esp_event_base_t base;
  int32_t id;
  esp_event_handler_t fn;
  void *arg;
};
std::recursive_mutex events_mutex;
std::vector<Handler *> handlers;
std::atomic<int> failure{0}, calls{0};
bool ok() { return ++calls != failure; }
bool initialized = false;
std::vector<Task *> tasks;
} // namespace
namespace fake {
std::atomic<int> connects{0}, stops{0}, resources{0};
std::atomic<int> delay_calls{0}, zero_tick_delays{0};
std::atomic<int64_t> time{0};
std::atomic<int> connect_error{0}, ipv6_error{0}, stop_error{0},
    disconnect_error{0}, random_calls{0};
std::atomic<bool> reject_once{false};
int storage = 0;
wifi_config_t saved{}, active{};
esp_netif_t *netif = nullptr;
void fail(int n) {
  calls = 0;
  failure = n;
}
void reset() {
  for (auto *t : tasks) {
    t->thread.join();
    delete t;
    fake::resources--;
  }
  tasks.clear();
  failure = 0;
  calls = 0;
  connects = 0;
  stops = 0;
  time = 0;
  delay_calls = 0;
  zero_tick_delays = 0;
  connect_error = 0;
  ipv6_error = 0;
  stop_error = 0;
  disconnect_error = 0;
  random_calls = 0;
  reject_once = false;
  saved = {};
  active = {};
}
void emit(esp_event_base_t b, int32_t id, void *d) {
  std::lock_guard<std::recursive_mutex> lock(events_mutex);
  for (auto *h : handlers)
    if (h->base == b && (h->id == id || h->id == ESP_EVENT_ANY_ID))
      h->fn(h->arg, b, id, d);
}
void advance(uint32_t ms) { time += int64_t(ms) * 1000; }
} // namespace fake
BaseType_t xTaskCreate(void (*fn)(void *), const char *, uint32_t, void *arg,
                       int, TaskHandle_t *out) {
  if (!ok())
    return 0;
  auto *t = new Task;
  tasks.push_back(t);
  *out = t;
  fake::resources++;
  t->thread = std::thread([=] {
    current = t;
    fn(arg);
  });
  return 1;
}
void vTaskDelete(void *) {}
TaskHandle_t xTaskGetCurrentTaskHandle() { return current; }
void xTaskNotifyGive(Task *t) {
  std::lock_guard<std::mutex> lock(t->mutex);
  ++t->notifications;
  t->cv.notify_one();
}
uint32_t ulTaskNotifyTake(int, uint32_t wait) {
  auto *t = current;
  std::unique_lock<std::mutex> lock(t->mutex);
  t->cv.wait_for(lock, std::chrono::milliseconds(wait),
                 [&] { return t->notifications != 0; });
  auto n = t->notifications;
  t->notifications = 0;
  return n;
}
void vTaskDelay(uint32_t ticks) {
  ++fake::delay_calls;
  if (ticks == 0)
    ++fake::zero_tick_delays;
  std::this_thread::yield();
}
QueueHandle_t xQueueCreate(size_t cap, size_t size) {
  if (!ok())
    return nullptr;
  auto *q = new Queue;
  q->cap = cap;
  q->size = size;
  fake::resources++;
  return q;
}
BaseType_t xQueueSend(Queue *q, const void *data, uint32_t) {
  std::lock_guard<std::mutex> lock(q->mutex);
  if (q->data.size() == q->cap)
    return 0;
  q->data.emplace_back(static_cast<const char *>(data),
                       static_cast<const char *>(data) + q->size);
  return 1;
}
BaseType_t xQueueReceive(Queue *q, void *out, uint32_t) {
  std::lock_guard<std::mutex> lock(q->mutex);
  if (q->data.empty())
    return 0;
  std::memcpy(out, q->data.front().data(), q->size);
  q->data.pop_front();
  return 1;
}
void xQueueReset(Queue *q) {
  std::lock_guard<std::mutex> lock(q->mutex);
  q->data.clear();
}
void vQueueDelete(Queue *q) {
  delete q;
  fake::resources--;
}
SemaphoreHandle_t xSemaphoreCreateBinary() {
  if (!ok())
    return nullptr;
  fake::resources++;
  return new Semaphore;
}
BaseType_t xSemaphoreTake(Semaphore *s, uint32_t) {
  std::unique_lock<std::mutex> lock(s->mutex);
  s->cv.wait(lock, [&] { return s->set; });
  s->set = false;
  return 1;
}
void xSemaphoreGive(Semaphore *s) {
  std::lock_guard<std::mutex> lock(s->mutex);
  s->set = true;
  s->cv.notify_one();
}
void vSemaphoreDelete(Semaphore *s) {
  delete s;
  fake::resources--;
}
esp_err_t esp_event_handler_instance_register(esp_event_base_t b, int32_t id,
                                              esp_event_handler_t fn, void *arg,
                                              void **out) {
  if (!ok())
    return ESP_FAIL;
  std::lock_guard<std::recursive_mutex> lock(events_mutex);
  auto *h = new Handler{b, id, fn, arg};
  handlers.push_back(h);
  *out = h;
  fake::resources++;
  return 0;
}
esp_err_t esp_event_handler_instance_unregister(esp_event_base_t, int32_t,
                                                void *h) {
  std::lock_guard<std::recursive_mutex> lock(events_mutex);
  handlers.erase(std::remove(handlers.begin(), handlers.end(), h),
                 handlers.end());
  delete static_cast<Handler *>(h);
  fake::resources--;
  return 0;
}
esp_err_t esp_event_post(esp_event_base_t b, int32_t id, const void *d, size_t,
                         uint32_t) {
  fake::emit(b, id, const_cast<void *>(d));
  return 0;
}
esp_err_t esp_wifi_init(const wifi_init_config_t *) {
  if (initialized)
    return ESP_OK;
  if (!ok())
    return ESP_FAIL;
  initialized = true;
  fake::active = fake::saved;
  fake::resources++;
  return 0;
}
esp_err_t esp_wifi_get_mode(wifi_mode_t *mode) {
  if (!initialized)
    return ESP_ERR_WIFI_NOT_INIT;
  *mode = WIFI_MODE_STA;
  return ESP_OK;
}
esp_err_t esp_wifi_deinit() {
  initialized = false;
  fake::resources--;
  return 0;
}
esp_netif_t *esp_netif_new(const esp_netif_config_t *) {
  if (!ok())
    return nullptr;
  fake::resources++;
  return fake::netif = new esp_netif_t;
}
void esp_netif_destroy(esp_netif_t *p) {
  delete p;
  fake::netif = nullptr;
  fake::resources--;
}
esp_err_t esp_netif_attach_wifi_station(esp_netif_t *) {
  return ok() ? 0 : ESP_FAIL;
}
esp_err_t esp_wifi_set_default_wifi_sta_handlers() {
  return ok() ? 0 : ESP_FAIL;
}
esp_err_t esp_wifi_clear_default_wifi_driver_and_handlers(void *) { return 0; }
esp_err_t esp_netif_create_ip6_linklocal(esp_netif_t *) {
  return fake::ipv6_error;
}
esp_err_t esp_wifi_set_storage(int s) {
  if (!ok())
    return ESP_FAIL;
  fake::storage = s;
  return 0;
}
esp_err_t esp_wifi_get_config(int, wifi_config_t *c) {
  if (!ok())
    return ESP_FAIL;
  *c = fake::active;
  return 0;
}
esp_err_t esp_wifi_set_config(int, wifi_config_t *c) {
  if (!ok())
    return ESP_FAIL;
  fake::active = *c;
  if (fake::storage == WIFI_STORAGE_FLASH)
    fake::saved = *c;
  return 0;
}
esp_err_t esp_wifi_set_mode(int) { return ok() ? 0 : ESP_FAIL; }
esp_err_t esp_wifi_start() { return ok() ? 0 : ESP_FAIL; }
esp_err_t esp_wifi_stop() {
  fake::stops++;
  return fake::stop_error;
}
esp_err_t esp_wifi_connect() {
  fake::connects++;
  return fake::connect_error;
}
esp_err_t esp_wifi_disconnect() { return fake::disconnect_error; }
uint32_t esp_random() {
  fake::random_calls++;
  return fake::reject_once.exchange(false) ? UINT32_MAX : 0;
}
int64_t esp_timer_get_time() { return fake::time; }
// The fake joins worker tasks after their exit semaphore is consumed.
struct Reaper {
  ~Reaper() {}
};
