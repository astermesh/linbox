# Pseudo-Box Validation

Lightweight validation scenarios for LinBox that exercise a longer lifecycle than unit tests, without requiring Docker-in-Docker or privileged host features.

## Contents

- [Time lifecycle](time-lifecycle.sh) — controller + shim + real process + dynamic time update
- [Failure fallback](failure-fallback.sh) — process behavior after controller shutdown
- [Repeatability](repeatability.sh) — repeated runs should produce identical virtual time output

## Purpose

These are not full end-to-end service tests. They are pseudo-tests that validate the substrate behavior in a real process lifecycle:

- controller starts
- shim attaches
- process reads virtual state
- controller changes or disappears
- process continues or falls back safely

They are intended to complement unit tests and preload tests until full container/service validation is available.

---

[← Back](../../rnd/testing/README.md)
