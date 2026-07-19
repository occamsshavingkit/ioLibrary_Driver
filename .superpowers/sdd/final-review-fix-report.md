<!-- markdownlint-disable MD013 -->

# Final Review Fix Report

Date: 2026-07-19

Starting HEAD: `c4bbb0c5382dbf83a7f563b85719f7c19eed0812`

Worktree:
`/home/quackdcs/W5500/.worktrees/rp2040-w5500-diagnostic`

## Scope

This consolidated wave changes only the host controller, its Python test, and
documentation. It does not change firmware production sources, CMake firmware
inputs, `Ethernet`, or `Internet/DHCP`.

The host change rejects network options with `status`. Documentation now makes
the synchronized-source provenance contract complete, records accepted final
line-size and DHCP-repeat decisions, and correctly labels the historical Task 5
evidence.

## CodeRabbit Findings

| ID | Disposition | Finding and rationale |
| --- | --- | --- |
| CR-01 | Fixed | The old README hash used plain `git diff`, so it omitted staged changes while rsync copied working-tree bytes. The workflow now hashes `git diff --no-ext-diff --binary HEAD` over `tests/hardware/rp2040_w5500_diag`, `Ethernet`, and `Internet/DHCP`. `git diff HEAD` represents the final working tree relative to `HEAD`, including staged and unstaged tracked changes. |
| CR-02 | Fixed | Untracked files in any synchronized source scope now abort before rsync. Known ignored/generated `build/`, `*.log`, and `__pycache__/` files are explicitly excluded from rsync and from the unexpected-ignored check; every other ignored file in those scopes aborts. Thus unhashed source cannot enter synchronization. |
| CR-03 | Fixed | Dirty detection now uses the same scoped `git diff --quiet HEAD` comparison as the hash instead of whole-tree porcelain output. `dirty=1` therefore means exactly that synchronized tracked sources differ from `HEAD`. |
| CR-04 | Fixed | README, design, and plan now state one clean-diff rule: `DIAG_DIFF_SHA256` is always 64 hex digits. Clean sources use the SHA-256 of an empty binary diff, `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`; dirty sources hash the complete scoped binary diff. The already-correct CMake and event representation were not changed. |
| CR-05 | Fixed | The plan's whole-repository rsync was replaced with three scoped rsync operations and the same source guards used by the README. The configure example passes all four explicit values: `DIAG_GIT_SHA`, `DIAG_GIT_DIRTY`, `DIAG_DIFF_SHA256`, and `DIAG_BUILD_UTC`. |
| CR-06 | Fixed | The plan's stale `DIAG_LINE_MAX 192u` example is now the user-approved `256u`. |
| CR-07 | Fixed | Plan and design prose now match implementation: DHCP clears only its own runner result, while every DHCP handler invocation performs chip reset, memory initialization, and bounded PHY preparation internally. Repeats are independent without replaying prior catalog stage records. |
| CR-08 | Fixed | The plan is labeled an implementation record. Its unchecked boxes are explicitly historical workflow steps, not current completion status, and the final README is named as the authoritative build guide. |
| CR-09 | Fixed | Task 5 now says the implementer did not flash during implementation and that the controller appended its HIL section later. The former contradictory statements now describe two distinct phases. |
| CR-10 | Fixed | The Task 5 artifact, transcript, rsync, and configure commands are labeled historical and provenance-unverified because they predate Task 9 generated metadata. Readers are directed to the final README; the historical commands were not rewritten to pretend they carried Task 9 values. |
| CR-11 | Fixed | The `nm -u` claim is corrected: no output proves no unresolved strong symbol blocked linking. It does not establish weak-symbol absence. Runtime `callback-layout FAIL code=no-status-api` is cited as evidence that the weak registrar resolved to null. |
| CR-12 | Fixed with TDD | `parse_args` now raises argparse's normal `SystemExit(2)` behavior if any network option accompanies `status`. A complete network set was first captured in a failing test. No-network `status` remains accepted and no status path mutates network configuration. |
| CR-13 | Fixed in docs | README and design now explicitly call the 256-byte record limit and self-contained DHCP preparation accepted final design decisions. `.superpowers/sdd/progress.md` was not edited. |
| CR-14 | Rejected | The claimed provenance-record overflow is false. `test_diag_build_info.c` and `test_diag_host.py` both prove the mandatory record is 194 bytes including newline; C and Python limits are 256 and oversized framing is tested. No production change was needed. |
| CR-15 | Rejected | The 60,000 ms DHCP value is a software loop deadline only. Every DHCP `diag_watchdog_begin` receives `DIAG_NETWORK_WATCHDOG_MS`, which is 5,000 ms. Other stage descriptors are at most 5,000 ms. No RP2040 watchdog call receives 60,000 ms. |
| CR-16 | Rejected | A speculative negative-stage API guard is unnecessary for the declared GCC toolchains. The all-nonnegative `diag_stage_id_t` has an unsigned compatible type, so `(diag_stage_id_t)-1` converts above `DIAG_STAGE_DHCP` and fails the existing `id < DIAG_STAGE_COUNT` check. Host and ARM GCC static-assert probes both compiled. |
| CR-17 | Rejected | The status-contract classifier intentionally keeps zero-return synchronous-PIO status callbacks installed when registration succeeds and transport callbacks remain intact. It restores transport callbacks only on alias failure, matching the implementation plan's status-contract requirement. |
| CR-18 | Rejected | UDP `sendto` retries and SENDOK polling intentionally share one 5-second `send_deadline`. This bounds the complete send/ARP operation to five seconds; starting a second deadline would permit roughly ten seconds and violate the bounded-send design. |
| CR-19 | Rejected | A per-board USB serial is not required. VID `6666`, PID `4021`, product `RP2040 W5500 Diagnostic`, and serial `RP2040-DIAG` identify this diagnostic firmware distinctly from the application. Existing HIL verified the diagnostic USB identity. |
| CR-20 | Rejected | Stage handlers return PASS or FAIL. `DIAG_STAGE_TIMEOUT` is generated only from the watchdog scratch journal during boot recovery, where `main.c` emits a `TIMEOUT` event directly. No handler can enter the PASS/FAIL result mapping with TIMEOUT. |
| CR-21 | Rejected, non-blocking | `test_diag_net` phase-name assertions directly cover the stable journal-to-text mapping and are appropriate host tests. No branch-correctness defect was demonstrated. |
| CR-22 | Rejected, non-blocking | Assert-based C host tests are intentional for small standalone test executables built without `NDEBUG`. Replacing the harness was unrelated to this fix wave. |
| CR-23 | Rejected, non-blocking | Duplicated fixture constants in C and Python intentionally detect cross-language protocol drift. Consolidating them would weaken independent contract checks or add generation machinery without correcting behavior. |
| CR-24 | Rejected, non-blocking | Long-token tokenizer behavior is already bounded by fixed-width C parsing and line framing. No accepted command or memory-safety failure was shown. |
| CR-25 | Rejected, non-blocking | UDP packet sizes `32` payload bytes and `40` received W5500 bytes are protocol/layout assertions, not unexplained production behavior. Their existing tests and packet type define the values. |
| CR-26 | Rejected | A normal-operation host deadline is intentionally absent. Firmware hardware/software deadlines govern active stages, while the host has an explicit 10-second reconnect deadline. The protocol defines no additional timeout exit code, so inventing a host timeout would conflict with the authoritative firmware result. |
| CR-27 | Rejected | The sysfs ancestor scan intentionally stops after the nearest ancestor that contains both `idVendor` and `idProduct`, even when they mismatch. Continuing upward could falsely identify a parent hub or controller. Existing hierarchy tests cover matching and mismatching devices. |

## TDD Evidence

### RED

Command:

```bash
python3 -m unittest discover \
  -s tests/hardware/rp2040_w5500_diag/host \
  -p 'test_diag_host.py' \
  -k rejects_network_configuration_for_status -v
```

Exact output before the parser change:

```text
test_rejects_network_configuration_for_status (test_diag_host.CommandTests.test_rejects_network_configuration_for_status) ... FAIL

======================================================================
FAIL: test_rejects_network_configuration_for_status (test_diag_host.CommandTests.test_rejects_network_configuration_for_status)
----------------------------------------------------------------------
Traceback (most recent call last):
  File "/home/quackdcs/W5500/.worktrees/rp2040-w5500-diagnostic/tests/hardware/rp2040_w5500_diag/host/test_diag_host.py", line 330, in test_rejects_network_configuration_for_status
    with redirect_stderr(StringIO()), self.assertRaises(SystemExit):
AssertionError: SystemExit not raised

----------------------------------------------------------------------
Ran 1 test in 0.001s

FAILED (failures=1)
```

### GREEN

The same command after the minimal parser guard produced:

```text
test_rejects_network_configuration_for_status (test_diag_host.CommandTests.test_rejects_network_configuration_for_status) ... ok

----------------------------------------------------------------------
Ran 1 test in 0.001s

OK
```

The pre-existing `test_forms_status_command` also remains green, proving that
no-network status behavior was preserved.

## Provenance Guard Evidence

At the required clean starting HEAD, the scoped command produced the canonical
empty-diff hash:

```text
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855  -
```

With the final-review tracked changes present, the scoped hash was computed,
`diag_host.py` was staged, and the same hash was computed again. Both outputs
were identical, proving staging does not remove source content from the hash:

```text
01d4c6bdc0205a910fb202376cc8ce88f39e9e16a76656d450afaefc0be0233a  -
01d4c6bdc0205a910fb202376cc8ce88f39e9e16a76656d450afaefc0be0233a  -
```

A temporary ordinary untracked source file was detected as:

```text
tests/hardware/rp2040_w5500_diag/provenance-untracked-probe.txt
```

A temporary ignored file not covered by the rsync exclusions was detected as:

```text
tests/hardware/rp2040_w5500_diag/.DS_Store
```

Both probes were deleted immediately. The unexpected-ignored query emits no
output for the generated `host/__pycache__/` files, matching the explicit rsync
exclusion.

## Final Validation

### Fresh C Configure, Build, And Tests

Command:

```bash
cmake -S tests/hardware/rp2040_w5500_diag \
  -B /tmp/opencode/w5500-diag-final-review \
  -DDIAG_BUILD_FIRMWARE=OFF
cmake --build /tmp/opencode/w5500-diag-final-review --parallel 4
ctest --test-dir /tmp/opencode/w5500-diag-final-review --output-on-failure
```

Exact CTest output:

```text
Internal ctest changing into directory: /tmp/opencode/w5500-diag-final-review
Test project /tmp/opencode/w5500-diag-final-review
    Start 1: diag_protocol
1/8 Test #1: diag_protocol ....................   Passed    0.00 sec
    Start 2: diag_build_info
2/8 Test #2: diag_build_info ..................   Passed    0.00 sec
    Start 3: diag_runner
3/8 Test #3: diag_runner ......................   Passed    0.00 sec
    Start 4: diag_stage_phases
4/8 Test #4: diag_stage_phases ................   Passed    0.00 sec
    Start 5: diag_journal
5/8 Test #5: diag_journal .....................   Passed    0.00 sec
    Start 6: diag_usb_identity
6/8 Test #6: diag_usb_identity ................   Passed    0.00 sec
    Start 7: w5500_diag_status_contract
7/8 Test #7: w5500_diag_status_contract .......   Passed    0.00 sec
    Start 8: diag_net
8/8 Test #8: diag_net .........................   Passed    0.00 sec

100% tests passed, 0 tests failed out of 8

Total Test time (real) =   0.01 sec
```

### Fresh Python Tests

Command:

```bash
python3 -m unittest discover \
  -s tests/hardware/rp2040_w5500_diag/host \
  -p 'test_*.py' -v
```

Exact output:

```text
test_main_returns_controller_exit_status (test_diag_host.CliTests.test_main_returns_controller_exit_status) ... ok
test_forms_network_and_run_all_commands (test_diag_host.CommandTests.test_forms_network_and_run_all_commands) ... ok
test_forms_repeat_command (test_diag_host.CommandTests.test_forms_repeat_command) ... ok
test_forms_run_command (test_diag_host.CommandTests.test_forms_run_command) ... ok
test_forms_status_command (test_diag_host.CommandTests.test_forms_status_command) ... ok
test_rejects_network_configuration_for_status (test_diag_host.CommandTests.test_rejects_network_configuration_for_status) ... ok
test_rejects_partial_network_configuration (test_diag_host.CommandTests.test_rejects_partial_network_configuration) ... ok
test_rejects_repeat_count_outside_firmware_range (test_diag_host.CommandTests.test_rejects_repeat_count_outside_firmware_range) ... ok
test_binds_nonblocking_udp_to_explicit_listener (test_diag_host.ControllerHelperTests.test_binds_nonblocking_udp_to_explicit_listener) ... ok
test_closes_udp_socket_when_bind_fails (test_diag_host.ControllerHelperTests.test_closes_udp_socket_when_bind_fails) ... ok
test_serial_buffer_accepts_exact_provenance_event_without_truncation (test_diag_host.ControllerHelperTests.test_serial_buffer_accepts_exact_provenance_event_without_truncation) ... ok
test_serial_buffer_frames_partial_crlf_lines (test_diag_host.ControllerHelperTests.test_serial_buffer_frames_partial_crlf_lines) ... ok
test_serial_buffer_rejects_non_ascii_line (test_diag_host.ControllerHelperTests.test_serial_buffer_rejects_non_ascii_line) ... ok
test_serial_buffer_rejects_oversized_line (test_diag_host.ControllerHelperTests.test_serial_buffer_rejects_oversized_line) ... ok
test_writes_utc_timestamped_transcript (test_diag_host.ControllerHelperTests.test_writes_utc_timestamped_transcript) ... ok
test_autonomous_barrier_fail_is_transcripted_without_poisoning_command (test_diag_host.ControllerIntegrationTests.test_autonomous_barrier_fail_is_transcripted_without_poisoning_command) ... ok
test_multiplexes_udp_echo_while_run_all_is_active (test_diag_host.ControllerIntegrationTests.test_multiplexes_udp_echo_while_run_all_is_active) ... ok
test_reconnects_to_matching_device_and_requires_recovered_timeout (test_diag_host.ControllerIntegrationTests.test_reconnects_to_matching_device_and_requires_recovered_timeout) ... ok
test_rejects_recovered_timeout_before_provenance_boot_event (test_diag_host.ControllerIntegrationTests.test_rejects_recovered_timeout_before_provenance_boot_event) ... ok
test_rejects_recovered_timeout_without_observed_interruption (test_diag_host.ControllerIntegrationTests.test_rejects_recovered_timeout_without_observed_interruption) ... ok
test_runs_command_to_completion_over_cdc_selector (test_diag_host.ControllerIntegrationTests.test_runs_command_to_completion_over_cdc_selector) ... ok
test_reconnect_registration_failure_closes_descriptor_and_retries (test_diag_host.ControllerRegistrationTests.test_reconnect_registration_failure_closes_descriptor_and_retries) ... ok
test_parses_timeout (test_diag_host.EventTests.test_parses_timeout) ... ok
test_rejects_duplicate_key (test_diag_host.EventTests.test_rejects_duplicate_key) ... ok
test_rejects_invalid_sequence_value (test_diag_host.EventTests.test_rejects_invalid_sequence_value) ... ok
test_rejects_missing_required_key (test_diag_host.EventTests.test_rejects_missing_required_key) ... ok
test_rejects_non_diag_line (test_diag_host.EventTests.test_rejects_non_diag_line) ... ok
test_rejects_non_key_value_token (test_diag_host.EventTests.test_rejects_non_key_value_token) ... ok
test_rejects_unsupported_protocol (test_diag_host.EventTests.test_rejects_unsupported_protocol) ... ok
test_requires_literal_diag_prefix (test_diag_host.EventTests.test_requires_literal_diag_prefix) ... ok
test_matches_watchdog_timeout_to_interrupted_operation (test_diag_host.ReconnectTests.test_matches_watchdog_timeout_to_interrupted_operation) ... ok
test_recovery_boot_does_not_regress_interrupted_sequence (test_diag_host.ReconnectTests.test_recovery_boot_does_not_regress_interrupted_sequence) ... ok
test_rejects_timeout_for_different_interrupted_operation (test_diag_host.ReconnectTests.test_rejects_timeout_for_different_interrupted_operation) ... ok
test_rescan_returns_none_without_matching_usb_identity (test_diag_host.ReconnectTests.test_rescan_returns_none_without_matching_usb_identity) ... ok
test_rescan_selects_only_matching_usb_identity (test_diag_host.ReconnectTests.test_rescan_selects_only_matching_usb_identity) ... ok
test_terminal_event_clears_completed_active_operation (test_diag_host.ReconnectTests.test_terminal_event_clears_completed_active_operation) ... ok
test_repeat_requires_every_requested_pass (test_diag_host.ResultTests.test_repeat_requires_every_requested_pass) ... ok
test_returns_four_on_shell_error (test_diag_host.ResultTests.test_returns_four_on_shell_error) ... ok
test_returns_three_on_recovered_timeout (test_diag_host.ResultTests.test_returns_three_on_recovered_timeout) ... ok
test_returns_two_on_fail_without_reinterpreting_failure_code (test_diag_host.ResultTests.test_returns_two_on_fail_without_reinterpreting_failure_code) ... ok
test_returns_zero_when_requested_stage_passes (test_diag_host.ResultTests.test_returns_zero_when_requested_stage_passes) ... ok
test_run_all_rejects_missing_network_acknowledgement (test_diag_host.ResultTests.test_run_all_rejects_missing_network_acknowledgement) ... ok
test_run_all_rejects_missing_stage_result (test_diag_host.ResultTests.test_run_all_rejects_missing_stage_result) ... ok
test_run_all_requires_every_stage_and_network_acknowledgement (test_diag_host.ResultTests.test_run_all_requires_every_stage_and_network_acknowledgement) ... ok
test_accepts_initial_sequence (test_diag_host.SequenceTests.test_accepts_initial_sequence) ... ok
test_accepts_multiple_events_for_one_operation (test_diag_host.SequenceTests.test_accepts_multiple_events_for_one_operation) ... ok
test_accepts_next_operation (test_diag_host.SequenceTests.test_accepts_next_operation) ... ok
test_detects_sequence_gap (test_diag_host.SequenceTests.test_detects_sequence_gap) ... ok
test_detects_sequence_regression (test_diag_host.SequenceTests.test_detects_sequence_regression) ... ok
test_closes_descriptor_when_raw_setup_fails (test_diag_host.SerialTests.test_closes_descriptor_when_raw_setup_fails) ... ok
test_opens_nonblocking_raw_cdc (test_diag_host.SerialTests.test_opens_nonblocking_raw_cdc) ... ok
test_does_not_require_broader_packet_validation_for_echo (test_diag_host.UdpTests.test_does_not_require_broader_packet_validation_for_echo) ... ok
test_echoes_packet_with_big_endian_magic (test_diag_host.UdpTests.test_echoes_packet_with_big_endian_magic) ... ok
test_rejects_packet_too_short_to_contain_magic (test_diag_host.UdpTests.test_rejects_packet_too_short_to_contain_magic) ... ok
test_rejects_packet_without_magic (test_diag_host.UdpTests.test_rejects_packet_without_magic) ... ok

----------------------------------------------------------------------
Ran 55 tests in 0.429s

OK
```

### Python Syntax, Whitespace, And Security

Commands:

```bash
python3 -m py_compile \
  tests/hardware/rp2040_w5500_diag/host/diag_host.py \
  tests/hardware/rp2040_w5500_diag/host/test_diag_host.py
python3 -m tabnanny tests/hardware/rp2040_w5500_diag/host
bandit -r tests/hardware/rp2040_w5500_diag/host -ll
```

`py_compile` and `tabnanny` produced no output and exited zero. Exact Bandit
result:

```text
Test results:
    No issues identified.

Code scanned:
    Total lines of code: 1526
    Total lines skipped (#nosec): 0
    Total potential issues skipped due to specifically being disabled (e.g., #nosec BXXX): 0

Run metrics:
    Total issues (by severity):
        Undefined: 0
        Low: 0
        Medium: 0
        High: 0
    Total issues (by confidence):
        Undefined: 0
        Low: 0
        Medium: 0
        High: 0
Files skipped (0):
```

### Documentation And Diff Validation

Commands:

```bash
markdownlint tests/hardware/rp2040_w5500_diag/README.md
markdownlint --disable MD013 MD032 -- \
  docs/superpowers/specs/2026-07-19-rp2040-w5500-diagnostic-firmware-design.md \
  docs/superpowers/plans/2026-07-19-rp2040-w5500-diagnostic-firmware.md \
  .superpowers/sdd/task-5-report.md
git diff --check
git diff --cached --check
```

All commands produced no output and exited zero. MD013 and MD032 were disabled
only for the three historical documents because the starting versions already
contain extensive long-line and list-spacing violations. The current README
passes the complete default ruleset. Focused assertions for the canonical clean
hash, scoped binary-diff command, plan's 256-byte limit, Task 5
`provenance-unverified` label, and removal of the plan's whole-repository rsync
also produced no output and exited zero.

### Enum Compatibility Probe

The following assertion was compiled with host GCC 13.3.0 and
`arm-none-eabi-gcc` 13.2.1 under `-std=c11 -Wall -Wextra -Werror -pedantic`:

```c
_Static_assert((diag_stage_id_t)-1 > DIAG_STAGE_DHCP,
               "negative stage must convert above the valid range");
```

Both compiler commands produced no output and exited zero.

## HIL Artifact Continuity

The existing Task 9 artifact still hashes to:

```text
1ba616a278b054384576c502f522d7b8bc95c271d4af1be855d7beb0f2ca175a  /tmp/opencode/rp2040_w5500_diag-c95f0a1.uf2
```

This matches the tracked Task 9 ledger. The command below produced no output:

```bash
git diff --name-only c95f0a1 -- \
  tests/hardware/rp2040_w5500_diag/src \
  tests/hardware/rp2040_w5500_diag/CMakeLists.txt \
  Ethernet Internet/DHCP
```

Therefore no firmware source, firmware build definition, or ioLibrary source
has changed since `c95f0a1`. Its HIL result remains behaviorally representative
of this host/docs/tests-only fix wave. This wave does not claim new flashing or
new HIL execution.

## Independent Review

A read-only Antigravity final reviewer examined `git diff HEAD`, the source
snapshot shell, parser/test change, all changed historical documents, firmware
source boundaries, and this report. It returned no compliance, regression, or
consistency findings. Its residual notes were limited to host clock accuracy,
the intentional argparse exit-code change for formerly accepted invalid input,
and the absence of a new HIL run in this host/docs/tests-only wave.

## Files Changed

- `.superpowers/sdd/final-review-fix-report.md`
- `.superpowers/sdd/task-5-report.md`
- `docs/superpowers/plans/2026-07-19-rp2040-w5500-diagnostic-firmware.md`
- `docs/superpowers/specs/2026-07-19-rp2040-w5500-diagnostic-firmware-design.md`
- `tests/hardware/rp2040_w5500_diag/README.md`
- `tests/hardware/rp2040_w5500_diag/host/diag_host.py`
- `tests/hardware/rp2040_w5500_diag/host/test_diag_host.py`

`.superpowers/sdd/progress.md` was not edited.

## Concerns

No unresolved concern remains in this fix wave. Corrected-driver acceptance
remains separate follow-up work exactly as recorded before this wave.

## Post-Rsync State-Stability Follow-Up

Starting HEAD: `b740ce066c1f49102b37f9cf70e8d67f1d71f8ad`

This follow-up changes only the README, implementation-record plan, design, and
this appended report. It does not change firmware, host, test, CMake, Ethernet,
or DHCP source. Existing report history above remains unchanged.

### Follow-Up Review Dispositions

| Finding | Disposition | Rationale |
| --- | --- | --- |
| Source mutation during rsync could make explicit provenance describe a different local state | Fixed in docs | README, plan, and design now require the same source guards and state capture before and immediately after all three rsync operations. Remote CMake starts only when HEAD, dirty state, and the complete scoped binary-diff hash still equal their pre-transfer values. A transfer or check failure stops the workflow. |
| `DIAG_BUILD_UTC` validation does not reject calendar-impossible timestamps | Rejected | The authoritative workflow does not accept a human-authored timestamp: it assigns `build_utc` with `date -u +%Y-%m-%dT%H:%M:%SZ`. CMake enforces the documented 20-character UTC representation. Calendar validation of a manually forged cache value is outside the provenance workflow and the value drives no firmware control flow. |
| UDP send polling should receive a fresh deadline | Rejected, duplicate of CR-18 | `sendto` retries and SENDOK polling intentionally share one five-second `send_deadline`, bounding the complete send/ARP operation. Resetting it between phases would allow roughly ten seconds and weaken the stated bound. |
| The host controller needs a general idle deadline | Rejected, duplicate of CR-26 | Active firmware stages are bounded by their software deadlines and watchdog journal; reconnect has its own ten-second host deadline. The protocol defines no normal-operation host timeout result, so an invented idle timeout could contradict the authoritative firmware result. |
| Successful SPI-status callbacks should be restored after contract classification | Rejected, duplicate of CR-17 | Successful registration is meant to leave independent, zero-return status callbacks installed. Only alias failure mutates transport callback storage, and that path restores the saved transport callbacks before reporting failure. |
| `%23s` should be derived automatically from `DIAG_STAGE_NAME_MAX` | Rejected, non-blocking | `DIAG_STAGE_NAME_MAX` is 24 bytes including the terminator and `%23s` is the corresponding bounded conversion. Longer input is split into extra tokens and rejected rather than accepted as a truncated command. No mismatch or behavioral defect exists in the current fixed protocol. |
| Phase IDs should be globally unique or strongly typed | Rejected, non-blocking | Journal phases are interpreted together with their stage ID. Phase `1` intentionally means `driver-call` for early driver stages and `save` for register stages; `diag_stage_phase_name(stage, phase)` performs that stage-qualified mapping, with direct tests for valid and invalid combinations. |
| `diag_stage_set_details` needs a GNU printf-format attribute | Rejected, non-blocking | A compiler-specific annotation could add diagnostics for future edits but is not runtime correctness. Current format strings are internal literals whose argument types match on inspection. The optional diagnostic annotation is unrelated to this documentation-only provenance fix. |
| Passing `sizeof(packet)` to `sendto` risks an unsafe size conversion | Rejected | `packet` is `diag_udp_packet_t`; `_Static_assert(sizeof(diag_udp_packet_t) == 32u)` fixes its size at compile time. Thirty-two fits the ioLibrary `uint16_t` length parameter, and the send result and pointer-delta checks independently enforce the same protocol size. |

### State-Stability Validation

A temporary isolated Git harness copied the documented `source_scopes`,
`check_source_guards`, `capture_source_state`, and three post-transfer
comparisons. Each mutation was injected between the two captures. A mutation
scenario counted as passing only when the documented guard exited nonzero; the
unchanged scenario counted as passing only when all three values matched.

Command:

```bash
bash -n /tmp/opencode/w5500-state-stability/validate.sh && \
  bash /tmp/opencode/w5500-state-stability/validate.sh
```

Exact output from the successful run (stdout and stderr as captured):

```text
PASS unchanged source state
Refusing changed source diff during synchronization.
PASS unstaged tracked mutation aborted on hash change
Refusing changed dirty state during source synchronization.
PASS staged tracked mutation aborted on dirty-state change
Refusing changed HEAD during source synchronization.
PASS HEAD change aborted
PASS new untracked source aborted
PASS unexpected ignored source aborted
Refusing unhashed untracked source files:
scope/new.txt
Refusing ignored source files not excluded from rsync:
scope/new.ignored
```

The command exited zero. The temporary harness was deleted after the run. The
five required case categories therefore establish that unchanged state proceeds, tracked
unstaged and staged mutations abort, a HEAD change aborts, and new untracked or
unexpected ignored source aborts.

### Follow-Up Documentation Validation

Commands:

```bash
markdownlint tests/hardware/rp2040_w5500_diag/README.md
markdownlint --disable MD013 MD032 -- \
  docs/superpowers/specs/2026-07-19-rp2040-w5500-diagnostic-firmware-design.md \
  docs/superpowers/plans/2026-07-19-rp2040-w5500-diagnostic-firmware.md \
  .superpowers/sdd/final-review-fix-report.md
git diff --check
test -z "$(git diff --name-only -- \
  tests/hardware/rp2040_w5500_diag/src \
  tests/hardware/rp2040_w5500_diag/host \
  tests/hardware/rp2040_w5500_diag/tests \
  tests/hardware/rp2040_w5500_diag/CMakeLists.txt \
  Ethernet Internet/DHCP)"
```

All four commands produced no output and exited zero. MD013 and MD032 remain
disabled only for the historical plan, design, and report, which inherited
those violations. The authoritative README passes the complete default
markdownlint ruleset, the diff has no whitespace errors, and no firmware, host,
test, CMake, Ethernet, or DHCP source differs from the follow-up starting HEAD.
