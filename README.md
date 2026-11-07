# ESP Wi-Fi Manager

[![CI](https://github.com/rndmcodeguy20/esp-wifi-manager/actions/workflows/ci.yml/badge.svg)](https://github.com/rndmcodeguy20/esp-wifi-manager/actions/workflows/ci.yml)
[![MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-5.4–5.5-orange.svg)](https://docs.espressif.com/projects/esp-idf/en/release-v5.4/esp32s3/)

Connect, recover, and hand control back to your application. A reusable ESP-IDF
station connection manager with a C API, nonblocking retries, bounded connection
waits, and explicit lifecycle control.

Supports **ESP-IDF >=5.4,<5.6**; firmware CI targets **ESP32-S3** on the
5.4 line and pinned 5.5.6. The standalone example is locally build-tested on
5.5.6. Other SDK versions and targets have not been qualified. This is an early release; hardware soak
qualification is still pending.

## What it handles

- Capped exponential backoff with uniformly sampled equal jitter.
- Separate association and IPv4 acquisition deadlines.
- Automatic recovery after disconnect or IPv4 loss, with accurate IP snapshots.
- Saved credentials, explicit RAM/flash storage, and full 32-byte textual SSIDs.
- Stop/restart, resource cleanup, and handoff to an application-owned portal.
- Optional IPv6 link-local setup without gating IPv4 readiness.

Your application owns provisioning UI, reset buttons, NVS recovery, and Internet
or cloud health checks. **READY means an IPv4 address is available**, not that an
Internet service is reachable.

## Add it to a project

Create or extend your application's `main/idf_component.yml`:

```yaml
dependencies:
  idf: ">=5.4,<5.6"
  esp-wifi-manager:
    git: https://github.com/rndmcodeguy20/esp-wifi-manager.git
    version: v0.1.1
```

Add the component to your `main/CMakeLists.txt` requirements:

```cmake
idf_component_register(SRCS "main.c" INCLUDE_DIRS "."
    REQUIRES esp-wifi-manager nvs_flash esp_netif esp_event)
```

Run `idf.py reconfigure` and build normally. Pin a release tag or full commit SHA
and commit your application's `dependencies.lock`. No ESP Component Registry
publication is required. See the official [Git dependency reference](https://docs.espressif.com/projects/idf-component-manager/en/latest/reference/manifest_file.html#git-dependencies).

Prefer a local checkout? Clone this repository as `components/esp-wifi-manager`
inside your project instead of adding the Git dependency. For an external local
checkout, see [local development](docs/usage.md#local-development).

## Minimal usage

Initialize shared infrastructure once, then create and start the manager:

```c
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "wifi_manager.h"

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_manager_config_t config;
    wifi_manager_config_default(&config);

    wifi_manager_handle_t manager = NULL;
    ESP_ERROR_CHECK(wifi_manager_create(&config, NULL, NULL, &manager));

    /* Uses previously saved protected-network credentials. */
    esp_err_t err = wifi_manager_start(manager, NULL, WIFI_MANAGER_STORAGE_FLASH);
    if (err == ESP_ERR_NOT_FOUND) {
        /* Start your provisioning flow from this application task.
           The manager still owns an initialized, stopped Wi-Fi driver. */
        return;
    }
    ESP_ERROR_CHECK(err);

    err = wifi_manager_wait_ready(manager, 30000);
    if (err == ESP_ERR_TIMEOUT) {
        /* Background recovery continues. Stop explicitly before provisioning. */
        return;
    }
    ESP_ERROR_CHECK(err);
    /* Start services; keep manager alive to recover future disconnects. */
}
```

For first-boot credentials, callbacks, and error handling, read the
[project integration guide](docs/usage.md). The [station example](examples/station)
is a complete buildable application:

```sh
git clone https://github.com/rndmcodeguy20/esp-wifi-manager.git
cd esp-wifi-manager
# Activate an supported ESP-IDF 5.4 or 5.5 environment first.
idf.py -C examples/station set-target esp32s3
idf.py -C examples/station build
```

The example uses placeholder credentials when none are saved. Replace them
locally or add provisioning before flashing your device.

## Defaults

| Setting | Default |
|---|---:|
| Association deadline | 15 s |
| IPv4 acquisition deadline | 15 s |
| Initial retry ceiling | 1 s |
| Maximum retry ceiling | 30 s |
| Continuous readiness before backoff reset | 60 s |
| IPv6 link-local request | Disabled |

For each failure, wait uniformly between half the current ceiling and the
ceiling. The ceiling doubles up to the cap. Brief successful connections retain
backoff; a stable connection resets it. Ordinary network failures retry while
running. An unrecoverable driver/control error enters FAILED and requires an
application stop/restart.

## Lifecycle rules

1. The application initializes NVS, esp-netif, and the default event loop.
2. The manager exclusively owns the Wi-Fi driver and station netif. An already
   initialized driver or second manager is rejected.
3. Call lifecycle and wait methods from an application task. They must not run
   on the default event-loop task or inside the manager callback.
4. Callbacks run on the worker. Return promptly; reading status is allowed.
5. `wait_ready()` timing out does not stop recovery. `stop()` cancels it and
   leaves the initialized driver available for an APSTA provisioning portal.
6. Stop the portal before restarting. Destroy only when no task or portal uses
   the manager or its driver resources.

SSID/passphrase embedded NUL bytes, raw hexadecimal PSKs, and enterprise
authentication are unsupported. Explicit protected credentials set the driver minimum authentication mode to
WPA2-PSK. Open networks require an empty password and explicit opt-in, and set
the threshold to OPEN. After
RAM-only use, supply explicit credentials to restart or recreate the manager
before loading saved credentials; RAM credentials are never silently persisted.
See [credential storage and provisioning](docs/usage.md#credentials-and-storage).

## Tests and CI

```sh
cmake -S tests -B build/host -DENABLE_SANITIZERS=ON
cmake --build build/host --parallel
ctest --test-dir build/host --output-on-failure
```

CI runs the adapter suite at 100 Hz and 1000 Hz tick rates, ASAN/UBSAN, a separate
TSan job, GCC coverage, a C public-header check,
and ESP32-S3 firmware builds on ESP-IDF 5.4 and 5.5.6. Core coverage must reach **95% lines / 90%
branches**; adapter coverage is reported separately. Reports are uploaded as
workflow artifacts. Checkout and artifact actions use current release versions;
Dependabot checks GitHub Actions updates weekly.

For GCC/gcovr 8.3 use `bash scripts/coverage.sh`. With Clang and LLVM tools use
`CXX=clang++ bash scripts/coverage-llvm.sh`. Latest local measurements and coverage
scope are in [validation](docs/validation.md). Radio behavior and long-running
recovery need the [hardware acceptance matrix](docs/hardware-tests.md).

## License

[MIT](LICENSE). Contributions should include a regression test for behavior
changes and pass the host suite and firmware build.
