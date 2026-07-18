# Implementation Plan: Fix P2 Audit Findings — W5500 ioLibrary_Driver

**Branch**: `003-fix-p2-audit-findings` | **Date**: 2026-07-18 | **Spec**: [spec.md](./spec.md)

## Summary

Apply 16 P2 audit fixes covering functional correctness, API consistency, and integration robustness. Each fix is on a separate branch off fork/master, independent of other P2 fixes and pending P0/P1 PRs.

## Technical Context

**Language/Version**: C (C99, embedded arm-none-eabi-gcc)

**Dependencies**: Wiznet ioLibrary_Driver W5500 SPI driver

**Target Platform**: ARM Cortex-M bare-metal with W5500 via SPI

**Project Type**: C library (embedded TCP/IP chip driver)

**Scale/Scope**: 16 changes across 6 files. Most fixes are 1-5 lines; AUD-028 (strict-C) is multi-issue with ~15 changes.

## Constitution Check

Template placeholder — no gates. PASS.

## Project Structure

Affected files: `Ethernet/socket.c`, `Ethernet/socket.h`, `Ethernet/wizchip_conf.c`, `Ethernet/wizchip_conf.h`, `Ethernet/W5500/w5500.c`, `Ethernet/W5500/w5500.h`, `Application/multicast/multicast.c`, `Application/loopback/loopback.c`

## Complexity Tracking

No violations. All fixes are localized, non-architectural changes.
