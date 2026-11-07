#include "fake.hpp"
#include "wifi_manager.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
namespace {
wifi_manager_status_t status(wifi_manager_handle_t m) {
  wifi_manager_status_t s{};
  assert(wifi_manager_get_status(m, &s) == ESP_OK);
  return s;
}
template <class F> void eventually(F fn) {
  auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (!fn()) {
    assert(std::chrono::steady_clock::now() < end);
    std::this_thread::yield();
  }
}
wifi_manager_credentials_t credentials() {
  wifi_manager_credentials_t c{};
  std::memset(c.ssid, 's', 32);
  c.ssid_len = 32;
  std::memset(c.password, 'p', 8);
  c.password_len = 8;
  return c;
}
wifi_manager_handle_t create(bool ipv6 = false) {
  wifi_manager_config_t c;
  wifi_manager_config_default(&c);
  c.association_timeout_ms = 100;
  c.ip_timeout_ms = 100;
  c.retry_initial_ms = 20;
  c.retry_max_ms = 80;
  c.stable_reset_ms = 200;
  c.ipv6_linklocal = ipv6;
  wifi_manager_handle_t m = nullptr;
  assert(wifi_manager_create(&c, nullptr, nullptr, &m) == ESP_OK);
  return m;
}
void destroy(wifi_manager_handle_t m) {
  assert(wifi_manager_destroy(m) == ESP_OK);
  fake::reset();
  assert(fake::resources == 0);
}
void ready(wifi_manager_handle_t m) {
  fake::emit(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, nullptr);
  eventually([&] { return status(m).state == WIFI_MANAGER_WAITING_FOR_IP; });
  ip_event_got_ip_t ip{fake::netif, {{42}, {255}, {1}}};
  fake::emit(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip);
  eventually([&] { return status(m).state == WIFI_MANAGER_READY; });
  assert(status(m).ipv4 == 42);
}
void lifecycle() {
  fake::reset();
  auto m = create(true);
  auto c = credentials();
  assert(wifi_manager_start(m, &c, WIFI_MANAGER_STORAGE_RAM) == 0);
  assert(fake::storage == WIFI_STORAGE_RAM);
  assert(fake::active.sta.threshold.authmode == WIFI_AUTH_WPA2_PSK);
  assert(std::memcmp(fake::active.sta.ssid, c.ssid, 32) == 0);
  assert(wifi_manager_start(m, &c, WIFI_MANAGER_STORAGE_RAM) ==
         ESP_ERR_INVALID_STATE);
  esp_netif_t other{};
  ip_event_got_ip_t ip{&other, {{99}, {0}, {0}}};
  fake::emit(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip);
  assert(status(m).state == WIFI_MANAGER_CONNECTING);
  fake::ipv6_error = ESP_FAIL;
  ready(m);
  assert(status(m).last_error == ESP_FAIL);
  ip_event_got_ip_t changed{fake::netif, {{43}, {254}, {2}}};
  fake::emit(IP_EVENT, IP_EVENT_STA_GOT_IP, &changed);
  eventually([&] { return status(m).ipv4 == 43; });
  assert(status(m).netmask == 254);
  assert(status(m).gateway == 2);
  assert(wifi_manager_wait_ready(m, 0) == 0);
  ip.esp_netif = fake::netif;
  fake::emit(IP_EVENT, IP_EVENT_STA_LOST_IP, &ip);
  eventually([&] { return status(m).state == WIFI_MANAGER_WAITING_FOR_IP; });
  assert(status(m).ipv4 == 0);
  assert(wifi_manager_stop(m) == 0);
  int connects = fake::connects;
  fake::advance(10000);
  wifi_event_sta_disconnected_t d{3};
  fake::emit(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &d);
  assert(wifi_manager_stop(m) == 0);
  assert(fake::connects == connects);
  assert(status(m).state == WIFI_MANAGER_STOPPED);
  assert(wifi_manager_start(m, nullptr, WIFI_MANAGER_STORAGE_RAM) ==
         ESP_ERR_INVALID_STATE);
  assert(fake::saved.sta.ssid[0] == 0);
  assert(wifi_manager_start(m, &c, WIFI_MANAGER_STORAGE_FLASH) == 0);
  assert(fake::storage == WIFI_STORAGE_FLASH);
  assert(fake::connects == connects + 1);
  destroy(m);
}
void deadlines() {
  fake::reset();
  auto m = create();
  auto c = credentials();
  assert(wifi_manager_start(m, &c, WIFI_MANAGER_STORAGE_RAM) == 0);
  fake::advance(101);
  eventually([&] { return status(m).state == WIFI_MANAGER_RETRY_WAIT; });
  int n = fake::connects;
  fake::advance(25);
  // An old timeout disconnect arriving after the retry deadline must fence
  // first.
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  assert(fake::connects == n);
  wifi_event_sta_disconnected_t d{8};
  fake::emit(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &d);
  eventually([&] { return fake::connects == n + 1; });
  ready(m);
  destroy(m);
}
void immediate() {
  fake::reset();
  fake::connect_error = ESP_FAIL;
  fake::reject_once = true;
  auto m = create();
  auto c = credentials();
  assert(wifi_manager_start(m, &c, WIFI_MANAGER_STORAGE_RAM) == 0);
  assert(status(m).state == WIFI_MANAGER_RETRY_WAIT);
  assert(status(m).last_error == ESP_FAIL);
  assert(fake::random_calls >= 2);
  fake::advance(20);
  eventually([&] { return fake::connects >= 2; });
  assert(wifi_manager_stop(m) == 0);
  destroy(m);
}
void validation() {
  fake::reset();
  wifi_manager_handle_t m = nullptr;
  wifi_manager_config_t config{};
  assert(wifi_manager_create(&config, nullptr, nullptr, &m) ==
         ESP_ERR_INVALID_ARG);
  m = create();
  wifi_manager_handle_t other = nullptr;
  assert(wifi_manager_create(nullptr, nullptr, nullptr, &other) ==
         ESP_ERR_INVALID_STATE);
  assert(wifi_manager_start(m, nullptr, WIFI_MANAGER_STORAGE_FLASH) ==
         ESP_ERR_NOT_FOUND);
  auto c = credentials();
  c.ssid[3] = 0;
  assert(wifi_manager_start(m, &c, WIFI_MANAGER_STORAGE_RAM) ==
         ESP_ERR_INVALID_ARG);
  c = credentials();
  c.password_len = 0;
  assert(wifi_manager_start(m, &c, WIFI_MANAGER_STORAGE_RAM) ==
         ESP_ERR_INVALID_ARG);
  assert(fake::active.sta.ssid[0] == 0);
  c.allow_open = true;
  assert(wifi_manager_start(m, &c, WIFI_MANAGER_STORAGE_RAM) == 0);
  assert(fake::active.sta.threshold.authmode == WIFI_AUTH_OPEN);
  assert(wifi_manager_wait_ready(m, 0) == ESP_ERR_TIMEOUT);
  destroy(m);
}
void allocations() {
  for (int n = 1; n <= 12; ++n) {
    fake::reset();
    fake::fail(n);
    wifi_manager_handle_t m = nullptr;
    esp_err_t e = wifi_manager_create(nullptr, nullptr, nullptr, &m);
    if (e == 0)
      assert(wifi_manager_destroy(m) == 0);
    else
      assert(!m);
    fake::reset();
    assert(fake::resources == 0);
  }
}
struct Callback {
  wifi_manager_handle_t manager = nullptr;
  std::atomic<int> calls{0};
};
void callback(const wifi_manager_status_t *, void *p) {
  auto *c = static_cast<Callback *>(p);
  if (!c->manager)
    return;
  wifi_manager_status_t s{};
  assert(wifi_manager_get_status(c->manager, &s) == 0);
  assert(wifi_manager_stop(c->manager) == ESP_ERR_INVALID_STATE);
  assert(wifi_manager_start(c->manager, nullptr, WIFI_MANAGER_STORAGE_FLASH) ==
         ESP_ERR_INVALID_STATE);
  assert(wifi_manager_wait_ready(c->manager, 0) == ESP_ERR_INVALID_STATE);
  assert(wifi_manager_destroy(c->manager) == ESP_ERR_INVALID_STATE);
  ++c->calls;
}
void reentrancy() {
  fake::reset();
  Callback ctx;
  assert(wifi_manager_create(nullptr, callback, &ctx, &ctx.manager) == 0);
  auto c = credentials();
  assert(wifi_manager_start(ctx.manager, &c, WIFI_MANAGER_STORAGE_RAM) == 0);
  eventually([&] { return ctx.calls > 0; });
  ready(ctx.manager);
  int before = ctx.calls;
  ip_event_got_ip_t ip{fake::netif, {{44}, {255}, {1}}};
  fake::emit(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip);
  eventually(
      [&] { return status(ctx.manager).ipv4 == 44 && ctx.calls > before; });
  destroy(ctx.manager);
}
void failures() {
  fake::reset();
  auto m = create();
  auto c = credentials();
  for (int n = 1; n <= 4; ++n) {
    fake::fail(n);
    assert(wifi_manager_start(m, &c, WIFI_MANAGER_STORAGE_RAM) != ESP_OK);
    assert(status(m).state == WIFI_MANAGER_STOPPED);
  }
  fake::fail(0);
  assert(wifi_manager_start(m, &c, WIFI_MANAGER_STORAGE_RAM) == ESP_OK);
  fake::stop_error = ESP_FAIL;
  assert(wifi_manager_stop(m) == ESP_FAIL);
  assert(status(m).last_error == ESP_FAIL);
  assert(wifi_manager_start(m, &c, WIFI_MANAGER_STORAGE_RAM) ==
         ESP_ERR_INVALID_STATE);
  fake::stop_error = 0;
  destroy(m);
  m = create();
  assert(wifi_manager_start(m, &c, WIFI_MANAGER_STORAGE_RAM) == 0);
  fake::disconnect_error = ESP_FAIL;
  fake::advance(101);
  eventually([&] { return status(m).state == WIFI_MANAGER_FAILED; });
  assert(status(m).last_error == ESP_FAIL);
  destroy(m);
}
struct Blocking {
  std::atomic<bool> entered{false}, release{false};
};
void blocking_callback(const wifi_manager_status_t *s, void *p) {
  auto *b = static_cast<Blocking *>(p);
  if (s->state != WIFI_MANAGER_CONNECTING)
    return;
  b->entered = true;
  while (!b->release.load())
    std::this_thread::yield();
}
void overflow() {
  fake::reset();
  Blocking b;
  wifi_manager_handle_t m = nullptr;
  assert(wifi_manager_create(nullptr, blocking_callback, &b, &m) == 0);
  auto c = credentials();
  std::thread start([&] {
    assert(wifi_manager_start(m, &c, WIFI_MANAGER_STORAGE_RAM) == 0);
  });
  eventually([&] { return b.entered.load(); });
  for (int i = 0; i < 100; ++i)
    fake::emit(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, nullptr);
  b.release = true;
  start.join();
  eventually([&] { return status(m).last_error == ESP_ERR_NO_MEM; });
  assert(status(m).state == WIFI_MANAGER_RETRY_WAIT);
  destroy(m);
}
void stop_race() {
  fake::reset();
  Blocking b;
  wifi_manager_handle_t m = nullptr;
  assert(wifi_manager_create(nullptr, blocking_callback, &b, &m) == 0);
  auto c = credentials();
  std::thread start([&] {
    assert(wifi_manager_start(m, &c, WIFI_MANAGER_STORAGE_RAM) == 0);
  });
  eventually([&] { return b.entered.load(); });
  for (int i = 0; i < 100; ++i)
    fake::emit(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, nullptr);
  std::thread stop([&] { assert(wifi_manager_stop(m) == 0); });
  b.release = true;
  start.join();
  stop.join();
  assert(status(m).state == WIFI_MANAGER_STOPPED);
  int n = fake::connects;
  fake::advance(100000);
  assert(wifi_manager_stop(m) == 0);
  assert(fake::connects == n);
  destroy(m);
}

void missing_disconnect() {
  fake::reset();
  auto m = create();
  auto c = credentials();
  assert(wifi_manager_start(m, &c, WIFI_MANAGER_STORAGE_RAM) == 0);
  fake::advance(101);
  eventually([&] { return status(m).state == WIFI_MANAGER_RETRY_WAIT; });
  fake::advance(101);
  eventually([&] { return status(m).state == WIFI_MANAGER_FAILED; });
  assert(status(m).last_error == ESP_ERR_TIMEOUT);
  destroy(m);
}

void ownership() {
  fake::reset();
  wifi_init_config_t init{};
  assert(esp_wifi_init(&init) == 0);
  int owned = fake::resources;
  wifi_manager_handle_t m = nullptr;
  assert(wifi_manager_create(nullptr, nullptr, nullptr, &m) ==
         ESP_ERR_INVALID_STATE);
  assert(!m);
  assert(fake::resources == owned);
  wifi_mode_t mode;
  assert(esp_wifi_get_mode(&mode) == 0);
  assert(esp_wifi_deinit() == 0);
  fake::reset();
  assert(fake::resources == 0);
}

void saved_credentials() {
  fake::reset();
  auto m = create();
  auto c = credentials();
  assert(wifi_manager_start(m, &c, WIFI_MANAGER_STORAGE_FLASH) == 0);
  assert(fake::saved.sta.ssid[31] == 's');
  assert(wifi_manager_stop(m) == 0);
  assert(wifi_manager_start(m, nullptr, WIFI_MANAGER_STORAGE_RAM) == 0);
  assert(fake::storage == WIFI_STORAGE_FLASH);
  destroy(m);
}

void wait_ready_blocks_for_at_least_one_tick() {
  fake::reset();
  auto m = create();
  auto c = credentials();
  assert(wifi_manager_start(m, &c, WIFI_MANAGER_STORAGE_RAM) == ESP_OK);
  std::atomic<int> result{ESP_FAIL};
  std::thread waiter([&] { result = wifi_manager_wait_ready(m, 100); });
  eventually([&] { return fake::delay_calls > 0; });
  ready(m);
  waiter.join();
  assert(result == ESP_OK);
  assert(fake::zero_tick_delays == 0);
  destroy(m);
}

} // namespace
int main() {
  wait_ready_blocks_for_at_least_one_tick();
  saved_credentials();
  ownership();
  missing_disconnect();
  failures();
  overflow();
  stop_race();
  validation();
  allocations();
  lifecycle();
  deadlines();
  immediate();
  reentrancy();
  std::puts("13 adapter groups passed: tick-safe wait, missing disconnect, failures, overflow, "
            "stop race, validation, allocations, lifecycle, deadline barrier, "
            "immediate errors/RNG rejection, callback reentrancy");
}
