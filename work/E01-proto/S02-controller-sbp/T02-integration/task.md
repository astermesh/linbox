# T02: Controller + Shim Integration

**Story:** [S02: Controller + SBP Protocol](../story.md)
**Status:** Backlog

## Description

Build the controller process and connect the shim to it. Controller manages virtual time via unix socket + shared memory. Shim reads time from shared memory instead of hardcoded values.

## Deliverables

- `src/controller/main.c` — unix socket server, event loop, accepts SBP connections from shim instances
- `src/controller/time-manager.c` — virtual time management, writes to shared memory
- Update `src/shim/linbox.c` — constructor connects to controller via unix socket (path from env var `LINBOX_SOCK`), attaches shared memory
- Update `src/shim/time.c` — reads virtual time from shared memory instead of hardcoded value
- `CMakeLists.txt` — add `linbox-controller` build target

## Tests

- Controller starts, creates socket, waits for connections
- Shim connects on constructor, performs HELLO handshake
- Controller SET_TIME → shim's clock_gettime returns new value within 1ms
- Controller changes time 1000 times in a loop → shim always reads latest
- Multiple processes with .so connect to same controller, all see same time
- Controller not running → shim logs warning, falls back to real time (graceful degradation)
- Controller dies mid-session → shim detects, logs error, falls back to real time
- Stress: 100K clock_gettime calls/sec with controller updating time concurrently

---

[← Back](../story.md)
