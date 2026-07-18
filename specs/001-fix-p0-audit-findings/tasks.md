# Tasks: Fix P0 Audit Findings — W5500 ioLibrary_Driver

**Input**: Design documents from `specs/001-fix-p0-audit-findings/`

**Prerequisites**: plan.md, spec.md, research.md, data-model.md, quickstart.md

**Tests**: Not applicable (embedded C bug fixes; verification via ASan/UBSan and hardware-in-loop per quickstart.md)

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3, US4, US5)
- Include exact file paths in descriptions

## Path Conventions

Fixes touch existing source files in the ioLibrary_Driver tree:

```text
Application/loopback/loopback.c    # AUD-001 (US1)
Ethernet/socket.c                  # AUD-002 (US2), AUD-004 (US4)
Ethernet/wizchip_conf.c            # AUD-003 (US3), AUD-005 (US5)
```

---

## Phase 1: Setup

**Purpose**: Confirm fork exists and working directory is at the audit commit

- [X] T001 Verify the occamsshavingkit/ioLibrary_Driver fork exists on GitHub, is synced with upstream master at commit `39fae86465dbaa728107c3b2a90692c0a1639735`, and the local working directory is clean (`git status` shows no uncommitted changes)
- [X] T002 Add fork remote to local repository and verify `fork/master` points to the audit commit

**Checkpoint**: Fork confirmed; ready to create fix branches

---

## Phase 2: User Story 1 — AUD-001 (Priority: P1) 🎯 MVP

**Goal**: Fix out-of-bounds termination in the W5500 UDP client loopback (`Application/loopback/loopback.c:247-254`)

**Independent Test**: Mock recvfrom() returning DATA_BUF_SIZE, -13, and 0; verify no ASan/UBSan abort and function returns error for negative values

**Reference**: TODO.md AUD-001

- [X] T003 [US1] Create branch `fix/aud-001-udp-loopback-oob` from fork/master
- [X] T004 [US1] Move `ret <= 0` check before `buf[ret] = 0x00` and `printf()` in `Application/loopback/loopback.c` function `loopback_udpc()`, preserving both the null-termination and printf after the guard
- [X] T005 [US1] Push branch `fix/aud-001-udp-loopback-oob` to fork and verify commit contains only the AUD-001 fix

**Checkpoint**: AUD-001 fix ready for PR submission

---

## Phase 3: User Story 2 — AUD-002 (Priority: P2)

**Goal**: Fix `SO_KEEPALIVEAUTO` getter width in `Ethernet/socket.c:1380-1384`

**Independent Test**: Place canaries around a uint8_t variable, call getsockopt(SO_KEEPALIVEAUTO), verify canaries intact

**Reference**: TODO.md AUD-002

- [X] T006 [US2] Create branch `fix/aud-002-keepaliveauto-getter-width` from fork/master
- [X] T007 [US2] Change `*(uint16_t*) arg = getSn_KPALVTR(sn)` to `*(uint8_t*) arg = getSn_KPALVTR(sn)` in `Ethernet/socket.c` at the `SO_KEEPALIVEAUTO` case of `getsockopt()`
- [X] T008 [US2] Push branch `fix/aud-002-keepaliveauto-getter-width` to fork and verify commit contains only the AUD-002 fix

**Checkpoint**: AUD-002 fix ready for PR submission

---

## Phase 4: User Story 3 — AUD-003 (Priority: P3)

**Goal**: Initialize SPI callback members instead of incompatible BUS callbacks in `Ethernet/wizchip_conf.c:256-277`

**Independent Test**: In W5500 SPI build, call WIZCHIP_READ(MR) before registration; must return deterministic error without HardFault

**Reference**: TODO.md AUD-003

- [X] T009 [US3] Create branch `fix/aud-003-spi-callback-init` from fork/master
- [X] T010 [US3] Add `#if` conditional in the WIZCHIP static initializer at `Ethernet/wizchip_conf.c:267-275`: when `_WIZCHIP_IO_MODE_` is in SPI mode (`_WIZCHIP_IO_MODE_SPI_`), initialize the `IF.SPI` union member with `._read_byte = wizchip_spi_readbyte` and `._write_byte = wizchip_spi_writebyte` (stubs that return 0 and no-op respectively), plus `._read_burst = NULL` and `._write_burst = NULL` (W5500 byte-mode path at `w5500.c:74` checks these for burst capability fallback). Keep existing BUS initialization (`IF.BUS._read_data`, `IF.BUS._write_data`) under an `#else` branch for BUS mode
- [X] T011 [US3] Push branch `fix/aud-003-spi-callback-init` to fork and verify commit contains only the AUD-003 fix

**Checkpoint**: AUD-003 fix ready for PR submission

---

## Phase 5: User Story 4 — AUD-004 (Priority: P4)

**Goal**: Make nonblocking W5500 TCP recv() consume available data in `Ethernet/socket.c:663-693`

**Independent Test**: Open TCP socket with SF_IO_NONBLOCK, inject 1 byte, verify recv() returns 1; empty socket must return SOCK_BUSY

**Reference**: TODO.md AUD-004

- [X] T012 [US4] Create branch `fix/aud-004-nonblocking-tcp-recv-order` from fork/master
- [X] T013 [US4] Swap the `sock_io_mode` and `recvsize != 0` checks in the non-IPv6 (`#else`) branch of the TCP recv() polling loop at `Ethernet/socket.c:687-692`, so `recvsize != 0` is checked before the nonblocking-mode return, matching the IPv6 branch ordering at lines 679-685
- [X] T014 [US4] Push branch `fix/aud-004-nonblocking-tcp-recv-order` to fork and verify commit contains only the AUD-004 fix

**Checkpoint**: AUD-004 fix ready for PR submission

---

## Phase 6: User Story 5 — AUD-005 (Priority: P5)

**Goal**: Stop ctlwizchip() from dereferencing absent arguments in `Ethernet/wizchip_conf.c:433-437`

**Independent Test**: Call ctlwizchip(CW_RESET_WIZCHIP, NULL) and ctlwizchip(CW_INIT_WIZCHIP, NULL) at -O0 and -O2; neither may fault

**Reference**: TODO.md AUD-005

- [X] T015 [US5] Create branch `fix/aud-005-ctlwizchip-null-deref` from fork/master
- [X] T016 [US5] Remove the unconditional `uint8_t tmp = *(uint8_t*) arg` at `Ethernet/wizchip_conf.c:436` and add guarded dereferences inside only the `ctlwizchip_type` cases that require `arg` (CW_SYS_LOCK, CW_SYS_UNLOCK, CW_GET_SYSLOCK for W6100/W6300; in W5500 builds these cases are dead code so the dereference is simply removed). Commands that require a valid argument must return `-1` when `arg == NULL`; commands that accept `NULL` (CW_RESET_WIZCHIP, CW_INIT_WIZCHIP) must proceed without dereferencing
- [X] T017 [US5] Push branch `fix/aud-005-ctlwizchip-null-deref` to fork and verify commit contains only the AUD-005 fix

**Checkpoint**: AUD-005 fix ready for PR submission

---

## Phase 7: Verification & PR Submission

**Purpose**: Verify all 5 fixes pass build and sanitizer checks, then submit as separate PRs

### Verification (run BEFORE PR submission)

- [X] T018 [P] For each fix branch, compile with `arm-none-eabi-gcc -D_WIZCHIP_=XXXX` for all 7 chip values (W5100, W5100S, W5200, W5300, W5500, W6100, W6300) using the repo's existing Makefile or equivalent cross-compile invocation, and confirm no build errors (plan.md multi-chip constraint)
- [X] T019 [P] Execute quickstart.md ASan/UBSan validation per fix: compile only the affected source files with a host compiler (`gcc -fsanitize=address,undefined -D_WIZCHIP_=5500`), link with mock SPI stubs returning fixed values, mock edge-case return values (DATA_BUF_SIZE, negative, NULL) at -O0 and -O2, and confirm no sanitizer abort (SC-004)
- [X] T020 For each fix, verify the commit message and diff address the root cause described in the TODO.md AUD entry, not just the symptom (SC-003)

### PR Submission (run AFTER verification passes)

- [X] T021 [P] Create PR from `occamsshavingkit:fix/aud-001-udp-loopback-oob` to `Wiznet:master` with description referencing AUD-001
- [X] T022 [P] Create PR from `occamsshavingkit:fix/aud-002-keepaliveauto-getter-width` to `Wiznet:master` with description referencing AUD-002
- [X] T023 [P] Create PR from `occamsshavingkit:fix/aud-003-spi-callback-init` to `Wiznet:master` with description referencing AUD-003
- [X] T024 [P] Create PR from `occamsshavingkit:fix/aud-004-nonblocking-tcp-recv-order` to `Wiznet:master` with description referencing AUD-004
- [X] T025 [P] Create PR from `occamsshavingkit:fix/aud-005-ctlwizchip-null-deref` to `Wiznet:master` with description referencing AUD-005
- [X] T026 Verify all 5 PRs are open against Wiznet/ioLibrary_Driver with correct base branch (master) and each contains exactly one commit (SC-001)

**Checkpoint**: All 5 P0 fixes verified and submitted as independent PRs

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies — start immediately
- **User Stories (Phases 2-6)**: All depend on Phase 1 (fork/branch setup). All 5 are independent of each other and can run in parallel.
- **Verification & PRs (Phase 7)**: Depends on all fix phases (2-6) being complete. Verification tasks (T018-T020) run first and block PR creation (T021-T026). T018 and T019 can run in parallel.

### User Story Dependencies

All 5 user stories are independent — no fix depends on another fix:

- **US1 (AUD-001)**: `Application/loopback/loopback.c` only — independent
- **US2 (AUD-002)**: `Ethernet/socket.c` only — independent
- **US3 (AUD-003)**: `Ethernet/wizchip_conf.c` only — independent
- **US4 (AUD-004)**: `Ethernet/socket.c` only — depends on US2 only for merge conflict avoidance (same file)
- **US5 (AUD-005)**: `Ethernet/wizchip_conf.c` only — depends on US3 only for merge conflict avoidance (same file)

**Note**: US2+US4 touch `socket.c` and US3+US5 touch `wizchip_conf.c`. Each fix is a distinct, non-overlapping edit. If done sequentially within same-file pairs, apply the lower-numbered AUD first.

### Parallel Opportunities

- All 5 fix tasks (T004, T007, T010, T013, T016) touch different files or non-overlapping code regions — **can run in parallel for code editing, but must be committed sequentially per file to avoid merge conflicts**
- Same-file pairs (US2+US4 in `socket.c`, US3+US5 in `wizchip_conf.c`): edits are non-overlapping code regions, but git operations must be serialized per file. Apply lower-numbered AUD first within each file pair.
- All 5 PR creation tasks (T021-T025) are independent — **can all run in parallel** (after verification passes)
- Verification tasks (T018, T019) can run in parallel with each other

---

## Parallel Example: All Fixes

```bash
# Code edits in different files can be prepared in parallel:
Task: "Apply AUD-001 fix in Application/loopback/loopback.c"
Task: "Apply AUD-002 fix in Ethernet/socket.c"       # Non-overlapping code region with AUD-004
Task: "Apply AUD-003 fix in Ethernet/wizchip_conf.c"  # Non-overlapping code region with AUD-005
Task: "Apply AUD-004 fix in Ethernet/socket.c"        # Different region; commit sequentially after AUD-002
Task: "Apply AUD-005 fix in Ethernet/wizchip_conf.c"  # Different region; commit sequentially after AUD-003
```

```bash
# Verification tasks can run in parallel (after all fixes pushed):
Task: "Verify multi-chip compilation for all 5 fix branches (T018)"
Task: "Execute ASan/UBSan validation for all 5 fixes (T019)"
```

```bash
# All 5 PR creation tasks are independent — can run in parallel (after verification):
Task: "Create PR for AUD-001 (T021)"
Task: "Create PR for AUD-002 (T022)"
Task: "Create PR for AUD-003 (T023)"
Task: "Create PR for AUD-004 (T024)"
Task: "Create PR for AUD-005 (T025)"
```

---

## Implementation Strategy

### MVP First (AUD-001 Only)

1. Complete Phase 1: Setup (fork verification)
2. Complete Phase 2: AUD-001 (UDP loopback OOB)
3. **STOP and SUBMIT PR**: First P0 fix submitted
4. Continue with remaining fixes

### Incremental Delivery

1. Setup → Fork ready
2. AUD-001 → Push → Verify (multi-chip + ASan) → Submit PR → deploy blocker #1 resolved
3. AUD-002 → Push → Verify (multi-chip + ASan) → Submit PR → deploy blocker #2 resolved
4. AUD-003 → Push → Verify (multi-chip + ASan) → Submit PR → deploy blocker #3 resolved
5. AUD-004 → Push → Verify (multi-chip + ASan) → Submit PR → deploy blocker #4 resolved
6. AUD-005 → Push → Verify (multi-chip + ASan) → Submit PR → deploy blocker #5 resolved
7. Each PR is independently reviewable and mergeable

### Parallel Strategy

With multiple developers/subagents:

1. One agent completes Phase 1 (Setup)
2. Once Setup is done, 5 agents each handle one AUD fix:
   - Agent A: AUD-001 (US1) in `Application/loopback/loopback.c`
   - Agent B: AUD-002 (US2) in `Ethernet/socket.c`
   - Agent C: AUD-003 (US3) in `Ethernet/wizchip_conf.c`
   - Agent D: AUD-004 (US4) in `Ethernet/socket.c` (after Agent B or coordinate region)
   - Agent E: AUD-005 (US5) in `Ethernet/wizchip_conf.c` (after Agent C or coordinate region)
3. After all fixes pushed: run verification (T018, T019 in parallel)
4. After verification passes: create all 5 PRs (T021-T025 in parallel)

---

## Notes

- [P] tasks = different files, no dependencies
- [Story] label maps task to specific audit finding for traceability
- Each user story (AUD fix) is independently completable and testable
- No tests to write (embedded C bug fixes; verification per quickstart.md)
- Commit after each fix; each fix is one atomic commit on its own branch
- Stop at any checkpoint to submit PR independently
- PRs require user authorization per AGENTS.md rule #9 (non-occamsshavingkit target repo)
