# Changelog

## 0.1.1 — 2026-11-07

- Support ESP-IDF `>=5.4,<5.6`, with CI builds on 5.4 and pinned 5.5.6.
- Set a WPA2-PSK minimum for explicit protected credentials; explicitly allowed
  open credentials use the OPEN threshold. The public C API is unchanged.
- Add driver-threshold regression checks at both host tick rates and local
  ESP-IDF 5.5.6 build verification; hardware qualification remains pending.

## 0.1.0 — 2026-11-07

- Initial standalone ESP-IDF station connection manager extracted in design from mini_dns.
- Nonblocking recovery with bounded exponential backoff, equal jitter, and stable-connection reset.
- Explicit lifecycle and IPv4 readiness; optional IPv6 link-local setup.
- Deterministic host tests and independent component/example builds.
- Tick-safe readiness waits tested at 100 Hz and 1000 Hz FreeRTOS tick rates.

Hardware qualification remains pending; see `docs/hardware-tests.md`.
