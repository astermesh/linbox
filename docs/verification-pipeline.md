# Verification Pipeline

A common verification mechanism for LinBox task implementation. The goal is simple: every task should be implemented together with the checks that prove it works, and those checks should be runnable as part of the normal workflow.

## Principles

- Every task must add or extend verification, not just code.
- Verification should be layered:
  - unit tests for pure logic
  - preload tests for shim behavior under `LD_PRELOAD`
  - pseudo-box tests for longer real-process lifecycles
  - end-to-end tests for container/service scenarios when available
- Cheap checks should run often.
- Expensive checks should still be easy to run with one command.
- A task is not done if its intended behavior is not covered by at least one verification mechanism.

## Verification Layers

### 1. Unit tests

Location: co-located `*.test.c`

Use for:
- protocol parsing
- shared memory logic
- PRNG logic
- controller helpers
- pure state machines

Run with:

```bash
make test
```

### 2. Preload tests

Location: co-located `*.preload.c`

Use for:
- libc interception behavior
- shim/controller interaction in a standalone binary
- wrapper path vs direct process behavior

Run with:

```bash
make test
```

### 3. Pseudo-box tests

Location: `tests/pseudo-box/`

Use for:
- controller lifecycle
- process lifecycle
- fallback behavior
- repeatability
- multi-process consistency
- stress scenarios that do not require Docker or privileged host features

Run with:

```bash
make pseudo-test
```

### 4. End-to-end tests

Location: `tests/e2e/`

Use for:
- real services
- containers
- PostgreSQL proof
- privileged networking and kernel features

Run with:

```bash
make e2e
```

## Task Completion Rule

Before marking a task complete:

1. Add or update the implementation.
2. Add or update the smallest useful unit/preload test.
3. Add or update a pseudo-box or e2e validation when the task changes lifecycle behavior.
4. Run the relevant verification commands.
5. Record verification coverage in the task file.

## Task Verification Checklist

Add this section to task files and keep it current:

```md
## Verification

- Unit tests:
- Preload tests:
- Pseudo-box tests:
- E2E tests:
- Manual checks:
```

Not every task needs all layers, but every task must explicitly state which layers apply.

## Recommended Workflow Per Task

```bash
# fast path
make test
make pseudo-test

# when relevant
make e2e
```

## Current Pseudo-Box Suite

- `tests/pseudo-box/time-lifecycle.sh`
- `tests/pseudo-box/failure-fallback.sh`
- `tests/pseudo-box/repeatability.sh`

These validate the substrate in a longer-lived real process cycle, even when full container validation is not available.

---

[← Back](README.md)
