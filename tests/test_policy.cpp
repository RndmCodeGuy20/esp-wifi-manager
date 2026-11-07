#include "policy.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace wifi_policy;
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      std::cerr << __LINE__ << ": " #x "\n";                                   \
      std::exit(1);                                                            \
    }                                                                          \
  } while (0)
void test_validation() {
  Config c;
  CHECK(valid_config(c));
  for (int i = 0; i < 5; i++) {
    Config bad = c;
    uint32_t *fields[] = {&bad.association_timeout_ms, &bad.ip_timeout_ms,
                          &bad.retry_initial_ms, &bad.retry_max_ms,
                          &bad.stable_reset_ms};
    *fields[i] = 0;
    CHECK(!valid_config(bad));
  }
  Config bad = c;
  bad.retry_max_ms = 999;
  CHECK(!valid_config(bad));
  uint8_t ssid[33], pass[64];
  for (auto &byte : ssid)
    byte = 0x61;
  for (auto &byte : pass)
    byte = 0x62;
  CHECK(!valid_credentials(nullptr, 1, pass, 8, false));
  CHECK(!valid_credentials(ssid, 0, pass, 8, false));
  CHECK(!valid_credentials(ssid, 33, pass, 8, false));
  for (size_t n : {size_t(1), size_t(3), size_t(31), size_t(32)})
    CHECK(valid_credentials(ssid, n, pass, 8, false));
  CHECK(!valid_credentials(ssid, 1, nullptr, 8, false));
  CHECK(!valid_credentials(ssid, 1, pass, 7, false));
  CHECK(!valid_credentials(ssid, 1, pass, 64, false));
  CHECK(valid_credentials(ssid, 32, pass, 63, false));
  CHECK(!valid_credentials(ssid, 1, nullptr, 0, false));
  CHECK(valid_credentials(ssid, 1, nullptr, 0, true));
  ssid[0] = 0;
  CHECK(!valid_credentials(ssid, 1, pass, 8, false));
  ssid[0] = 0xe2;
  pass[0] = 0;
  CHECK(!valid_credentials(ssid, 1, pass, 8, false));
}
void test_retry() {
  Config c;
  for (uint32_t n = 1; n < 40; n++) {
    uint32_t ceiling = n >= 6 ? 30000 : 1000u << (n - 1);
    CHECK(retry_delay(c, n, 0) == ceiling / 2);
    CHECK(retry_delay(c, n, ceiling / 2) == ceiling);
    CHECK(retry_delay(c, n, UINT32_MAX) >= ceiling / 2);
    CHECK(retry_delay(c, n, UINT32_MAX) <= ceiling);
  }
  CHECK(retry_delay(c, UINT32_MAX, 0) == 15000);
  CHECK(retry_delay(c, 0, 0) == 500);
}
void test_lifecycle() {
  Policy p;
  uint64_t now = 10;
  auto send = [&](Event e, uint32_t rnd = 0, int err = 0, uint16_t reason = 0) {
    return p.apply({e, p.generation(), now, rnd, err, reason});
  };
  CHECK(!send(Event::timer).changed);
  CHECK(send(Event::start).connect);
  CHECK(p.snapshot().attempts == 1);
  CHECK(p.snapshot().deadline == 15010);
  CHECK(!send(Event::start).changed);
  CHECK(!p.apply({Event::associated, 0, now}).changed);
  CHECK(send(Event::associated).changed);
  CHECK(p.snapshot().state == State::waiting_for_ip);
  CHECK(!send(Event::associated).changed);
  CHECK(send(Event::got_ip).changed);
  CHECK(p.snapshot().has_ip);
  CHECK(!send(Event::got_ip).changed);
  now += 59999;
  CHECK(!send(Event::timer).changed);
  now++;
  send(Event::timer);
  CHECK(p.snapshot().failures == 0);
  CHECK(send(Event::lost_ip).changed);
  CHECK(!p.snapshot().has_ip);
  CHECK(!send(Event::lost_ip).changed);
  CHECK(send(Event::got_ip).changed);
  CHECK(send(Event::disconnected, 0, 5, 42).changed);
  CHECK(p.snapshot().reason == 42);
  CHECK(p.snapshot().error == 5);
  CHECK(p.snapshot().state == State::retry_wait);
  CHECK(!send(Event::disconnected).changed);
  CHECK(!send(Event::got_ip).changed);
  now = p.snapshot().deadline - 1;
  CHECK(!send(Event::timer).connect);
  now++;
  CHECK(send(Event::timer).connect);
  CHECK(send(Event::connect_failed, 0, 7).changed);
  CHECK(p.snapshot().failures == 2);
  now = p.snapshot().deadline;
  CHECK(send(Event::timer).connect);
  now = p.snapshot().deadline;
  CHECK(send(Event::timer).disconnect);
  CHECK(p.snapshot().failures == 3);
  now = p.snapshot().deadline;
  send(Event::timer);
  send(Event::associated);
  now = p.snapshot().deadline;
  CHECK(send(Event::timer).disconnect);
  now = p.snapshot().deadline;
  send(Event::timer);
  send(Event::associated);
  send(Event::got_ip);
  CHECK(p.snapshot().failures == 4);
  now += 60000;
  send(Event::timer);
  CHECK(p.snapshot().failures == 0);
  CHECK(send(Event::disconnected).changed);
  CHECK(p.snapshot().failures == 1);
  uint64_t old = p.generation();
  CHECK(send(Event::stop).stop);
  CHECK(p.snapshot().state == State::stopped);
  CHECK(!p.snapshot().has_ip);
  CHECK(!send(Event::stop).changed);
  CHECK(!p.apply({Event::timer, old, UINT64_MAX}).changed);
  CHECK(send(Event::start).connect);
  CHECK(p.snapshot().attempts == 1);
  CHECK(send(Event::fatal, 0, 99).disconnect);
  CHECK(p.snapshot().state == State::failed);
  CHECK(!send(Event::disconnected).changed);
  CHECK(send(Event::stop).stop);
}
void test_saturation_and_stale_events() {
  Policy q;
  auto a = q.apply({Event::start, 0, UINT64_MAX - 2});
  CHECK(a.connect);
  CHECK(q.snapshot().deadline == UINT64_MAX);
  CHECK(q.apply({Event::timer, q.generation(), UINT64_MAX}).disconnect);
  Policy r;
  r.apply({Event::start, 0, 0});
  CHECK(!r.apply({Event::lost_ip, r.generation(), 0}).changed);
  CHECK(!r.apply({Event::connect_failed, r.generation() + 1, 0}).changed);
  CHECK(r.apply({Event::stop, r.generation(), 0}).stop);
}
void test_stability_without_timer() {
  Policy p;
  p.apply({Event::start, 0, 0});
  p.apply({Event::disconnected, p.generation(), 1});
  p.apply({Event::timer, p.generation(), 501});
  p.apply({Event::got_ip, p.generation(), 502});
  p.apply({Event::disconnected, p.generation(), 60502});
  CHECK(p.snapshot().failures == 1);
}
void test_stop_states() {
  for (bool ready : {false, true}) {
    Policy p;
    p.apply({Event::start, 0, 0});
    p.apply({Event::associated, p.generation(), 1});
    if (ready) {
      p.apply({Event::got_ip, p.generation(), 2});
      CHECK(!p.apply({Event::connect_failed, p.generation(), 3}).changed);
    }
    CHECK(p.apply({Event::stop, p.generation(), 4}).stop);
    CHECK(p.snapshot().state == State::stopped);
    CHECK(!p.snapshot().has_ip);
  }
}
void test_retry_extremes() {
  Config c;
  c.retry_initial_ms = 3;
  c.retry_max_ms = 9;
  CHECK(retry_delay(c, 1, 0) == 1);
  CHECK(retry_delay(c, 1, 2) == 3);
  CHECK(retry_delay(c, 2, 0) == 3);
  CHECK(retry_delay(c, 3, 0) == 4);
  c.retry_initial_ms = UINT32_MAX;
  c.retry_max_ms = UINT32_MAX;
  CHECK(valid_config(c));
  CHECK(retry_delay(c, UINT32_MAX, 0) == UINT32_MAX / 2);
  CHECK(retry_delay(c, 1, UINT32_MAX / 2 + 1) == UINT32_MAX);
}
int main() {
  test_validation();
  test_retry();
  test_lifecycle();
  test_saturation_and_stale_events();
  test_retry_extremes();
  test_stability_without_timer();
  test_stop_states();
  std::cout << "7 policy test groups passed\n";
}
