# Changelog

## 0.1.0 — 2026-11-07

- Initial standalone ESP-IDF station connection manager extracted in design from mini_dns.
- Nonblocking recovery with bounded exponential backoff, equal jitter, and stable-connection reset.
- Explicit lifecycle and IPv4 readiness; optional IPv6 link-local setup.
- Deterministic host tests and independent component/example builds.
- Tick-safe readiness waits tested at 100 Hz and 1000 Hz FreeRTOS tick rates.

Hardware qualification remains pending; see `docs/hardware-tests.md`.
