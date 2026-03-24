# T01: Docker Integration

**Story:** [S06: Container + E2E](../story.md)
**Status:** Done

## Description

Package everything into Docker containers. Controller and sandbox communicate via unix socket mounted as a volume. Seccomp profile blocks io_uring and traps targeted syscalls.

## Deliverables

- `docker/Dockerfile.sandbox` — based on Ubuntu/Debian, copies liblinbox.so, entrypoint sets `LD_PRELOAD`
- `docker/Dockerfile.controller` — builds and runs linbox-controller
- `docker/seccomp-profile.json` — Docker seccomp profile (block io_uring_setup/enter/register, trap time/random syscalls)
- `docker-compose.yml` — controller + sandbox services, shared volume for unix socket and shared memory file
- `scripts/run-sandbox.sh` — convenience script for manual testing

## Tests

- `docker compose build` succeeds
- `docker compose up` → both containers start without errors
- Controller log: "listening on /run/linbox/linbox.sock"
- Sandbox log: "linbox: shim loaded (pid=1)" (or similar)
- Shim connects to controller: "linbox: connected to controller"
- `docker compose down` → clean shutdown, no orphan processes
- Rebuild after code change → new .so is picked up

## Verification

- Unit tests: not applicable
- Preload tests: covered indirectly by `make verify` because the shared library is rebuilt and preload suites still pass
- Pseudo-box tests: `make verify`
- E2E tests: `tests/e2e/docker-smoke.sh` (also via `make verify`)
- Manual checks: `docker compose -f docker-compose.yml config` when Docker is available

---

[← Back](../story.md)
