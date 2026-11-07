#include "policy.hpp"
#include <cstring>
#include <limits>
namespace wifi_policy {
namespace {
uint64_t deadline(uint64_t now, uint32_t duration) {
  return now > UINT64_MAX - duration ? UINT64_MAX : now + duration;
}
uint32_t increment(uint32_t n) { return n == UINT32_MAX ? n : n + 1; }
} // namespace
bool valid_config(const Config &c) {
  return c.association_timeout_ms && c.ip_timeout_ms && c.retry_initial_ms &&
         c.retry_max_ms >= c.retry_initial_ms && c.stable_reset_ms;
}
bool valid_credentials(const uint8_t *ssid, size_t n, const uint8_t *pass,
                       size_t p, bool open) {
  return ssid && n >= 1 && n <= 32 && !std::memchr(ssid, 0, n) &&
         ((p == 0 && open) ||
          (pass && p >= 8 && p <= 63 && !std::memchr(pass, 0, p)));
}
uint32_t retry_ceiling(const Config &c, uint32_t failures) {
  uint32_t ceiling = c.retry_initial_ms;
  for (uint32_t i = 1; i < failures && ceiling < c.retry_max_ms; i++)
    ceiling = ceiling > c.retry_max_ms / 2 ? c.retry_max_ms : ceiling * 2;
  return ceiling;
}
uint32_t retry_delay(const Config &c, uint32_t failures, uint32_t random) {
  const uint32_t ceiling = retry_ceiling(c, failures);
  const uint32_t half = ceiling / 2;
  return half + random % (ceiling - half + 1);
}
void Policy::connect(uint64_t now, Actions &a) {
  s_.state = State::connecting;
  s_.attempts = increment(s_.attempts);
  s_.deadline = deadline(now, config_.association_timeout_ms);
  a.connect = true;
  a.changed = true;
}
void Policy::fail(const Input &in, Actions &a) {
  if (s_.state == State::ready && in.now >= ready_since_ &&
      in.now - ready_since_ >= config_.stable_reset_ms)
    s_.failures = 0;
  s_.has_ip = false;
  stable_ = false;
  s_.failures = increment(s_.failures);
  s_.error = in.error;
  s_.reason = in.reason;
  s_.state = State::retry_wait;
  s_.deadline = deadline(in.now, retry_delay(config_, s_.failures, in.random));
  a.changed = true;
}
Actions Policy::apply(Input in) {
  Actions a;
  if (in.generation != s_.generation)
    return a;
  if (in.event == Event::start) {
    if (s_.state != State::stopped)
      return a;
    const uint64_t generation = s_.generation + 1;
    s_ = {};
    s_.generation = generation;
    connect(in.now, a);
    return a;
  }
  if (in.event == Event::stop) {
    if (s_.state == State::stopped)
      return a;
    const uint64_t generation = s_.generation + 1;
    s_ = {};
    s_.generation = generation;
    stable_ = false;
    a.stop = true;
    a.changed = true;
    return a;
  }
  if (s_.state == State::stopped || s_.state == State::failed)
    return a;
  if (in.event == Event::fatal) {
    s_.state = State::failed;
    s_.error = in.error;
    s_.has_ip = false;
    s_.deadline = 0;
    a.disconnect = true;
    a.changed = true;
    return a;
  }
  switch (in.event) {
  case Event::associated:
    if (s_.state == State::connecting) {
      s_.state = State::waiting_for_ip;
      s_.deadline = deadline(in.now, config_.ip_timeout_ms);
      a.changed = true;
    }
    break;
  case Event::got_ip:
    if (s_.state == State::connecting || s_.state == State::waiting_for_ip) {
      s_.state = State::ready;
      s_.has_ip = true;
      ready_since_ = in.now;
      stable_ = false;
      s_.deadline = deadline(in.now, config_.stable_reset_ms);
      s_.error = 0;
      a.changed = true;
    }
    break;
  case Event::lost_ip:
    if (s_.state == State::ready) {
      if (in.now >= ready_since_ &&
          in.now - ready_since_ >= config_.stable_reset_ms)
        s_.failures = 0;
      s_.state = State::waiting_for_ip;
      s_.has_ip = false;
      stable_ = false;
      s_.deadline = deadline(in.now, config_.ip_timeout_ms);
      a.changed = true;
    }
    break;
  case Event::connect_failed:
    if (s_.state == State::connecting)
      fail(in, a);
    break;
  case Event::disconnected:
    if (s_.state != State::retry_wait)
      fail(in, a);
    break;
  case Event::timer:
    if (in.now < s_.deadline)
      break;
    if (s_.state == State::retry_wait)
      connect(in.now, a);
    else if (s_.state == State::connecting ||
             s_.state == State::waiting_for_ip) {
      fail(in, a);
      a.disconnect = true;
    } else if (s_.state == State::ready && !stable_ && in.now >= ready_since_) {
      s_.failures = 0;
      stable_ = true;
      s_.deadline = 0;
      a.changed = true;
    }
    break;
  default:
    break;
  }
  return a;
}
} // namespace wifi_policy
