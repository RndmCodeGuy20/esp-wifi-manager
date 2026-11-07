#pragma once
#include <cstddef>
#include <cstdint>
namespace wifi_policy {
enum class State {
  stopped,
  connecting,
  waiting_for_ip,
  ready,
  retry_wait,
  failed
};
struct Config {
  uint32_t association_timeout_ms = 15000, ip_timeout_ms = 15000,
           retry_initial_ms = 1000, retry_max_ms = 30000,
           stable_reset_ms = 60000;
};
bool valid_config(const Config &);
bool valid_credentials(const uint8_t *ssid, size_t ssid_len,
                       const uint8_t *password, size_t password_len,
                       bool allow_open);
/* For exact uniform jitter reject RNG values >= floor(2^32/width)*width,
 * width=ceiling-ceiling/2+1, using uint64 arithmetic, before retry_delay. */
uint32_t retry_ceiling(const Config &, uint32_t failures);
uint32_t retry_delay(const Config &, uint32_t failures, uint32_t random);
enum class Event {
  start,
  stop,
  associated,
  got_ip,
  lost_ip,
  disconnected,
  connect_failed,
  timer,
  fatal
};
struct Input {
  Event event;
  uint64_t generation;
  uint64_t now;
  /* For a failure-producing input, supply an accepted uniform RNG sample:
   * use retry_ceiling(config, effective_next_failures), where next failures is
   * the saturating increment of snapshot.failures, or 1 when this input ends
   * a ready interval lasting stable_reset_ms. Rejection-sample with that
   * ceiling's width as documented above. Non-failure inputs ignore random. */
  uint32_t random = 0;
  int error = 0;
  uint16_t reason = 0;
};
struct Actions {
  bool connect = false, disconnect = false, stop = false, changed = false;
};
struct Snapshot {
  State state = State::stopped;
  uint64_t generation = 0, deadline = 0;
  uint32_t attempts = 0, failures = 0;
  int error = 0;
  uint16_t reason = 0;
  bool has_ip = false;
};
/* Worker owns this object. Feed monotonic milliseconds; events/timers must
 * carry generation(). start/stop deliberately advance generation to fence
 * queued work. Actions execute once by worker; report immediate connect error
 * as connect_failed. Adapter owns actual IP payload and clears it whenever
 * has_ip becomes false. Run generation does not distinguish attempts within
 * one run: the adapter MUST normalize/drain prior-attempt driver events and
 * establish a disconnect-completion barrier before executing the next connect
 * action. Only events attributable to the current attempt may reach apply(). */
class Policy {
public:
  explicit Policy(Config config = {}) : config_(config) {}
  Actions apply(Input input);
  const Snapshot &snapshot() const { return s_; }
  uint64_t generation() const { return s_.generation; }

private:
  Config config_;
  Snapshot s_;
  uint64_t ready_since_ = 0;
  bool stable_ = false;
  void fail(const Input &, Actions &);
  void connect(uint64_t, Actions &);
};
} // namespace wifi_policy
