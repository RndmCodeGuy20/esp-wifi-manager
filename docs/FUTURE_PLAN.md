# Enhancement plan: toward v1.0.0

Status: proposed roadmap, not an implementation commitment.

Recorded: 2026-11-07. Starting point: v0.1.0.

## Purpose

Make ESP Wi-Fi Manager a dependable connection and recovery module that ESP-IDF
developers can adopt without inheriting a provisioning UI, cloud integration,
or application framework.

The intended position is:

> An observable, thoroughly tested Wi-Fi recovery engine for ESP-IDF applications
> that want to control their own provisioning and product behavior.

v1.0.0 should represent a stable interface and recovery behavior supported by
device evidence. It should not be a milestone reached by adding enough features
or increasing a coverage percentage.

## Progress since the initial plan

As of 2026-11-08, v0.1.1 adds ESP-IDF 5.5 support with CI builds against the
5.4 line and 5.5.6. The module is consumed by mini_dns and esp-swarm; the latter
pins v0.1.1, and its user reports the integration works as designed. These are
application integration milestones, not completion of the broader hardware
qualification or external-adoption goals below.

## Current foundation and gaps

The current component provides a C-compatible interface, station lifecycle
control, association/IP deadlines, exponential backoff with jitter, status
snapshots, explicit RAM/flash credential storage, and optional IPv6 link-local
setup. It has host tests, sanitizers, coverage tooling, a standalone example,
and firmware build CI.

The largest remaining gaps are integration ergonomics, a stable lifecycle and
storage contract, useful operational diagnostics, broader verified SDK/board
support, and real hardware qualification. Host fakes and successful compilation
do not establish radio reliability or long-running resource stability.

## Existing alternatives and positioning

Backoff, provisioning, multiple credentials, and host testing are already offered
by existing projects. These features alone are not a credible uniqueness claim.

| Alternative | Documented capabilities relevant to this plan |
|---|---|
| [tonyp7/esp32-wifi-manager](https://github.com/tonyp7/esp32-wifi-manager) | C component combining connection management with a captive portal, scanning, HTTP, and DNS |
| [aluiziotomazelli/wifi_manager](https://components.espressif.com/components/aluiziotomazelli/wifi_manager/versions/1.1.0/readme) | Backoff, multiple stored credentials, sync/async operations, and host testing |
| [embedblocks/wifi-manager](https://components.espressif.com/components/embedblocks/wifi-manager/versions/0.4.1/readme) | Multi-network selection, BLE provisioning, deadlines, recovery, and events |

This is a comparison of published documentation as reviewed on the recorded
date, not an independent reliability benchmark or an exhaustive market survey.
Revisit it before making public comparative claims.

Our opportunity is to combine a small integration surface, clear failure
diagnostics, safe credential changes, and measured recovery/resource behavior.
Any claim of an exclusive capability needs further evidence.

## Priority 1: developer experience and interface cleanup

- Publish on the ESP Component Registry for a one-command installation path.
  Retain pinned Git dependencies and local development instructions.
- Provide validated credential helpers so applications do not manually populate
  byte arrays and lengths for ordinary textual credentials.
- Make saved-credential loading and storage selection explicit and consistent.
  Remove the current destroy/recreate requirement after RAM-only use without
  ever silently persisting a RAM-only candidate.
- Make control commands safe to enqueue from callbacks. Keep blocking waits
  restricted to appropriate application tasks, with clear completion/error
  reporting. Avoid multiplying the interface with unnecessary wrappers.
- Replace unbounded internal control waits with explicit timeout and recovery
  semantics. A timeout must not free resources still used by a live worker.
- Document driver/netif ownership, concurrency, partial-start failures, stop,
  restart, and destruction as a single coherent contract.
- Keep NVS recovery and shared event-loop initialization application-owned.
  Convenience must not mean erasing application settings or taking over globals.
- Supply complete examples for a headless application, an application-owned HTTP
  provisioning flow, and integration with Espressif provisioning.

Proposed acceptance criteria:

- A developer unfamiliar with the repository can run the example in roughly
  ten minutes after setting up the documented ESP-IDF toolchain.
- An existing provisioning flow integrates without modifying component source.
- Normal use does not require knowledge of internal event-loop barriers.
- Every supported lifecycle operation has documented completion, timeout, and
  failure behavior, exercised by regression tests.

## Priority 2: safe credential replacement

Support a candidate/confirmed credential workflow that provisioning transports
can share:

1. Try candidate credentials in RAM while preserving the last confirmed set.
2. Validate connection and IP acquisition; allow an optional application-defined
   validation step when a product needs it.
3. Persist only through an explicit successful commit.
4. Roll back to the previous confirmed configuration on failure or cancellation.

Define storage ownership and interrupted-update recovery before freezing this
interface. Do not promise transactional persistence until power-loss behavior
has been implemented and tested.

Acceptance requires failed candidates never overwriting confirmed credentials,
explicit commit/cancel results, and tests for interruptions at persistence
boundaries. Logs and diagnostics must not expose passphrases.

## Priority 3: operational diagnostics

- Report the failed stage, last driver error/disconnect reason, attempt duration,
  and next retry timing.
- Provide a bounded recent transition history suitable for support reports.
- Distinguish association, IPv4 acquisition, and optional application-probe
  failures. An application-probe failure must not automatically imply that the
  Wi-Fi link is broken or force a reconnect.
- Keep READY defined as local IPv4 readiness. Represent optional health checks
  separately instead of changing that meaning.
- Treat authentication diagnostics as evidence, not certainty: a disconnect
  reason alone does not always prove that a password is wrong.

Acceptance requires diagnosing a failed attempt from one status/support report,
bounded diagnostic memory, redacted sensitive data, and deterministic tests of
reported timings and transitions.

## Priority 4: resource behavior and hardware qualification

- Measure incremental component RAM/flash cost, stack high-water mark, idle
  wakeups, and connection/recovery timings separately from SDK overhead.
- Replace continuous polling with event/deadline-driven wakeups where practical.
- Define bounded queue behavior and investigate a guarantee of no manager-owned
  allocation after startup. Do not extend that claim to SDK or application code
  without evidence.
- Select and publish a small, explicit ESP-IDF/board support matrix. Expand it
  based on actual users; distinguish build-tested from hardware-tested targets.
- Execute the [hardware matrix](hardware-tests.md): absent AP, wrong credentials,
  DHCP failures, router reboot, flapping, IP changes, provisioning transitions,
  lifecycle repetition, and an overnight resource soak.
- Record board, SDK, router, revision, timing, heap/stack observations, and reset
  causes so others can reproduce each result.

Before v1.0, agree on the required soak duration, cycle counts, resource budgets,
and timing tolerances for the supported matrix. Passing means meeting those
published limits with no unexplained resets, stuck recovery, or accumulating
manager resource leaks. High host coverage is complementary evidence, not a
substitute for these tests.

## Priority 5: adoption and contributor experience

Users should be able to delete connection/recovery code they would otherwise
maintain. Contributors should be able to fix an issue affecting their product
and demonstrate that the fix belongs upstream.

- Add CONTRIBUTING guidance with one short local test path and an explanation
  of when hardware evidence is needed.
- Offer focused issues with reproduction steps and clear acceptance criteria.
- Add bug-report templates collecting SDK/board versions, redacted transition
  history, and network conditions.
- Welcome reproducible router quirks, board qualification reports, examples,
  documentation fixes, and regression tests as contributions.
- Keep changes localized and reviewable; avoid building a generic framework
  around hypothetical extension points.
- Establish responsive review, predictable releases, migration notes, and a
  compatibility/deprecation policy for the public interface.

## Suggested delivery sequence

| Milestone | Main outcome |
|---|---|
| Next 0.x milestone | Simplify integration and settle lifecycle/storage semantics |
| Following 0.x milestone | Add safe credential trials and useful diagnostics |
| Qualification milestone | Publish resource measurements and hardware results; validate independent consumers |
| v1.0.0 | Freeze the supported interface and publish the verified support/compatibility contract |

The sequence expresses dependencies, not release dates. Interface changes are
expected during 0.x and should be documented for existing consumers.

## v1.0.0 release checklist

- [ ] Registry installation and documented examples work from clean projects.
- [ ] Lifecycle, concurrency, storage, and timeout semantics are settled.
- [ ] Credential updates preserve the last confirmed configuration on failure.
- [ ] Failure diagnostics are useful, bounded, and redact sensitive data.
- [ ] CI, sanitizers, and coverage gates pass on the declared build matrix.
- [ ] Hardware acceptance and soak results meet the published release criteria.
- [ ] Resource measurements and tested SDK/board support are published.
- [ ] Several independent applications have exercised the integration contract.
- [ ] Contribution, compatibility, deprecation, and migration guidance exists.
- [ ] Release documentation clearly separates guarantees from known limitations.

## Deferred unless real users justify them

Enterprise authentication, mesh, automatic roaming, and a large provisioning UI
are not prerequisites for this v1.0 direction. Keep provisioning integrations
optional and avoid making cloud, BLE, or UI dependencies mandatory in the core.

This plan records intended work only. It does not claim these enhancements are
implemented, hardware-qualified, or unique among existing solutions.
