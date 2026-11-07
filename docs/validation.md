# Validation

## Local baseline — 2026-11-07

| Check | Measured result |
|---|---|
| Policy tests | Seven groups pass |
| Production adapter with threaded ESP/FreeRTOS fakes | Thirteen groups pass at both 100 Hz and 1000 Hz |
| AddressSanitizer + UndefinedBehaviorSanitizer | All three host test targets pass |
| ThreadSanitizer | Adapter concurrency suite passes |
| LLVM core coverage | 98.56% lines / 93.40% branch outcomes |
| LLVM adapter coverage | 92.70% lines / 82.09% branch outcomes |
| ESP32-S3 standalone example | Builds with the installed ESP-IDF 5.4-derived toolchain |
| First consumer (mini_dns) | Firmware builds; 60 host tests pass |

These baseline measurements used Apple Clang 21 and an ESP-IDF 5.4.4-derived
checkout with Xtensa GCC 14.2. They are not a claim of hardware qualification.
Hosted results are available in the repository's
[CI runs](https://github.com/rndmcodeguy20/esp-wifi-manager/actions/workflows/ci.yml).
Download a run's coverage artifact for its GCC/gcovr measurements.

The [initial hosted run](https://github.com/RndmCodeGuy20/esp-wifi-manager/actions/runs/42393440559)
passed all jobs, including the ESP-IDF container firmware build. Its GCC/gcovr
report measured core coverage at 98.4% lines / 92.9% branches and adapter coverage
at 91.5% / 66.7%. That run predates the added coarse-tick readiness regression;
use newer run artifacts for measurements of later revisions.

The readiness regression first failed at a simulated 100 Hz tick rate because
`pdMS_TO_TICKS(1)` became zero. Clamping the wait to at least one tick fixed it;
the 100 Hz and 1000 Hz suites now both pass with ASAN/UBSAN and TSan.

## Reproduce

```sh
cmake -S tests -B build/sanitize -DENABLE_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build/sanitize --parallel
ctest --test-dir build/sanitize --output-on-failure

cmake -S tests -B build/threads -DENABLE_TSAN=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build/threads --parallel
ctest --test-dir build/threads --output-on-failure

# GCC + matching gcov + gcovr 8.3:
bash scripts/coverage.sh
# Or Clang + LLVM tools:
CXX=clang++ bash scripts/coverage-llvm.sh

# Activate ESP-IDF 5.4 first:
idf.py -C examples/station set-target esp32s3
idf.py -C examples/station build
```

Host builds also compile the public header as C. The standalone firmware example
compiles against real ESP-IDF headers, independently of any consumer project.

## Coverage scope

The 95% line / 90% branch gate covers production `src/policy.cpp`: state policy,
retry calculation and credential validation. Production `src/wifi_manager.cpp`
is measured and reported separately. Vendor code, fakes, tests and header-only
accessors are outside those denominators. There are no source exclusions. LLVM
and GCC can count branches differently; use the report from the compiler used by
your run rather than comparing their percentages as identical measurements.

The suite covers credential byte limits, retry caps/jitter, stable-reset timing,
association/IP deadlines, stale work, stop/restart, driver ownership, partial
initialization cleanup, queue overflow, callback restrictions, IP updates, and
RAM/flash isolation. Time and randomness are injected for deterministic policy
cases; adapter tests use threaded fakes for task synchronization.

## Limits

The fake event bridge serializes dispatch under a mutex; it does not reproduce
the full SDK event-loop scheduler or radio behavior. Allocation-failure tests
cover explicit SDK/FreeRTOS acquisition points, not failures inside C++ library
internals. Counter saturation is checked through arithmetic/extreme inputs,
not billions of real connection attempts.

Hardware acceptance, teardown timing under real event load, DHCP/router behavior
and an overnight resource soak remain pending. Run the
[hardware matrix](hardware-tests.md) for the actual product board and network.
