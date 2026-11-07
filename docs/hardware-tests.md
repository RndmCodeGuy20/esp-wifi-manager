# Hardware acceptance

Status: **not run**. Host tests and successful compilation cannot establish radio,
router, DHCP, socket, or FreeRTOS timing behavior on hardware.

For every run record component revision, application revision, board, ESP-IDF
revision, router model/firmware, configuration, timestamps and state callback log.
Never record Wi-Fi passwords. Confirm the callback does not block.

| Scenario | Procedure | Acceptance | Result |
|---|---|---|---|
| Normal boot | Start with valid saved credentials | IPv4 readiness, correct address | Pending |
| Missing credentials | Start with empty station configuration | Distinct missing-credentials error; no attempt | Pending |
| Wrong password | Start with valid-length incorrect password | Bounded attempts, jittered retries, no event-loop stalls | Pending |
| Absent AP | Disable AP before boot | Retry ceiling grows to configured cap | Pending |
| No DHCP | Allow association, disable DHCP | IP deadline causes reconnection | Pending |
| Slow DHCP | Delay lease within configured deadline | Ready without unnecessary reconnect | Pending |
| Router reboot | Reboot after readiness | Readiness cleared; recover automatically | Pending |
| Flapping | Repeated outages shorter than stability interval | Backoff does not reset on every brief connection | Pending |
| Stable recovery | Stay ready beyond stability interval, then disconnect | Backoff restarts at initial range | Pending |
| Changed IP | Change lease after reconnect | Status shows current address; app reconnects affected sockets | Pending |
| Provisioning | Stop manager, start APSTA portal, scan, save 32-byte SSID | No manager reconnect; scan succeeds; saved bytes intact | Pending |
| Stop races | Stop during association, DHCP wait and retry wait | No reconnect after stop returns | Pending |
| Lifecycle soak | Repeat create/start/stop/destroy | No leaked tasks, netifs, handlers or heap growth | Pending |
| IPv6 optional | Run with IPv6 absent/present | IPv4 readiness independent of link-local creation | Pending |

Compare retry delays against the configured half-ceiling through ceiling window;
driver association time is additional. Confirm default event-loop responsiveness
with another periodic event handler while the AP is absent. Review minimum free
heap, stack high-water marks and watchdog/reset reports during an overnight soak.
