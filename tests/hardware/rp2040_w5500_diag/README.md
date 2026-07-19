# RP2040 W5500 Diagnostic Firmware

This standalone Pico firmware isolates W5500 transport, register, pointer, UDP,
and DHCP behavior from application firmware. It exposes a strict ASCII protocol
over USB CDC, records source provenance in the first event, and uses the RP2040
watchdog scratch journal to report a blocking driver's exact stage and phase
after reconnect.

This workflow does **not** use `/root/firmware`, the temperature-server source
tree, or the temperature-server UF2. The installed WIZnet package supplies only
the low-level PIO transport. `wizchip_conf.c`, `socket.c`, `w5500.c`, and
`dhcp.c` always come from the explicitly synchronized `IOLIBRARY_ROOT`.

## Safety And Result Rules

- Build the audited current revision with `DIAG_EXPECT_SPI_STATUS=1`.
- Do not run two serial clients against the CDC device at the same time.
- Do not interpret USB enumeration or a DHCP address alone as a pass.
- A stage passes only when its terminal protocol event is `event=PASS`.
- The controller returns `0` only when every requested result passes, `2` on
  `FAIL`, `3` on a matched watchdog-recovered `TIMEOUT`, and `4` on protocol,
  transport, framing, or incomplete-result errors.
- The current audited driver is expected to produce
  `callback-layout FAIL code=no-status-api` before any W5500 transfer. The
  controller therefore exits `2`. That nonzero result is valid structural
  evidence from the harness, not a passing driver and not a reason to rewrite
  the failure as success.

## Prerequisites

The development host needs Git, CMake 3.20 or newer, a C11 compiler, Python 3,
OpenSSH, rsync, and SHA-256 utilities. Host tooling uses only the Python 3
standard library.

The Raspberry Pi build and hardware host must provide:

| Item | Required value |
| --- | --- |
| Pico SDK | `/usr/share/pico-sdk` |
| WIZnet Pico transport | `/usr/share/wiznet-pico-c` |
| ioLibrary source | A temporary synchronized tree passed as `IOLIBRARY_ROOT` |
| Board connection | W55RP20-EVB-PICO USB connected to the Pi |
| Tools | CMake, ARM GNU toolchain, Python 3, `picotool`, `lsusb`, `udevadm` |

Examples below use `root@192.168.2.34`, `/tmp/rp2040-w5500-diag-src`,
`/tmp/ioLibrary-driver-diag`, and `/tmp/rp2040-w5500-diag-build`.

## Host Tests

Run from the repository root:

```bash
cmake -S tests/hardware/rp2040_w5500_diag \
  -B /tmp/opencode/w5500-diag-host \
  -DDIAG_BUILD_FIRMWARE=OFF
cmake --build /tmp/opencode/w5500-diag-host --parallel 4
ctest --test-dir /tmp/opencode/w5500-diag-host --output-on-failure
python3 -m unittest discover \
  -s tests/hardware/rp2040_w5500_diag/host \
  -p 'test_*.py' -v
python3 -m py_compile \
  tests/hardware/rp2040_w5500_diag/host/diag_host.py \
  tests/hardware/rp2040_w5500_diag/host/test_diag_host.py
python3 -m tabnanny tests/hardware/rp2040_w5500_diag/host
```

Host tests use deterministic generated build values. Firmware configuration is
different: all four provenance variables are mandatory and malformed values
are rejected.

## Calculate Provenance

Calculate these values in Bash in the exact worktree being audited, before
rsync. Keep the same shell open for the sync and configure commands.

```bash
set -euo pipefail
repo_root=$(git rev-parse --show-toplevel)
source_scopes=(
  tests/hardware/rp2040_w5500_diag
  Ethernet
  Internet/DHCP
)

check_source_guards() {
  local untracked unexpected_ignored

  untracked=$(git -C "$repo_root" ls-files --others --exclude-standard -- \
    "${source_scopes[@]}")
  if [ -n "$untracked" ]; then
    printf 'Refusing unhashed untracked source files:\n%s\n' "$untracked" >&2
    exit 1
  fi

  unexpected_ignored=$(
    git -C "$repo_root" ls-files --others --ignored --exclude-standard -- \
      "${source_scopes[@]}" \
      ':(glob,exclude)**/build/**' \
      ':(glob,exclude)**/*.log' \
      ':(glob,exclude)**/__pycache__/**'
  )
  if [ -n "$unexpected_ignored" ]; then
    printf 'Refusing ignored source files not excluded from rsync:\n%s\n' \
      "$unexpected_ignored" >&2
    exit 1
  fi
}

capture_source_state() {
  local diff_status

  check_source_guards
  source_git_sha=$(git -C "$repo_root" rev-parse HEAD)
  if git -C "$repo_root" diff --quiet HEAD -- "${source_scopes[@]}"; then
    source_dirty=0
  else
    diff_status=$?
    if [ "$diff_status" -ne 1 ]; then
      exit "$diff_status"
    fi
    source_dirty=1
  fi
  source_diff_sha=$(
    git -C "$repo_root" diff --no-ext-diff --binary HEAD -- \
      "${source_scopes[@]}" | sha256sum | cut -d' ' -f1
  )
}

capture_source_state
git_sha=$source_git_sha
dirty=$source_dirty
diff_sha=$source_diff_sha
build_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)
printf 'git=%s\ndirty=%s\ndiff=%s\nbuild=%s\n' \
  "$git_sha" "$dirty" "$diff_sha" "$build_utc"
```

The synchronized source set is exactly the diagnostic project, `Ethernet`, and
`Internet/DHCP`. `dirty` is `1` exactly when that tracked source set differs
from `HEAD`. `DIAG_DIFF_SHA256` is always a 64-hex SHA-256: a clean source set
hashes the empty binary diff and therefore uses
`e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`; a dirty
source set hashes the complete `git diff --binary HEAD` stream over those three
scopes, including staged and unstaged tracked changes.

The invariant is that every source file rsync can transfer is either the `HEAD`
version or represented by that diff stream. Any untracked file in a synchronized
scope aborts the workflow. Ignored `build/`, `*.log`, and `__pycache__/` outputs
are explicitly excluded below; any other ignored file in those scopes also
aborts instead of reaching rsync unhashed.

The firmware's first newline-terminated record is exactly:

<!-- markdownlint-disable MD013 -->

```text
DIAG protocol=1 seq=0 stage=boot event=PASS git=<40-hex> dirty=<0-or-1> diff=<64-hex> build=<UTC>
```

<!-- markdownlint-enable MD013 -->

The event is 194 bytes including its newline when populated with the fixed-width
values above. The accepted final protocol and CDC record limit is 256 bytes;
both implementations reject oversized or malformed framing rather than
truncating it.

## Synchronize Controlled Sources

Only the standalone project and the required ioLibrary directories are copied.
The exclusions must stay identical to the generated-file exceptions in the
provenance check above. The trailing slashes are intentional.

```bash
diag_source=/tmp/rp2040-w5500-diag-src
iolibrary_source=/tmp/ioLibrary-driver-diag
ssh root@192.168.2.34 \
  "mkdir -p '$diag_source' '$iolibrary_source/Ethernet' '$iolibrary_source/Internet/DHCP'"
rsync -a --delete \
  --exclude='build/' --exclude='*.log' --exclude='__pycache__/' \
  "$repo_root/tests/hardware/rp2040_w5500_diag/" \
  "root@192.168.2.34:$diag_source/"
rsync -a --delete \
  --exclude='build/' --exclude='*.log' --exclude='__pycache__/' \
  "$repo_root/Ethernet/" \
  "root@192.168.2.34:$iolibrary_source/Ethernet/"
rsync -a --delete \
  --exclude='build/' --exclude='*.log' --exclude='__pycache__/' \
  "$repo_root/Internet/DHCP/" \
  "root@192.168.2.34:$iolibrary_source/Internet/DHCP/"

capture_source_state
if [ "$source_git_sha" != "$git_sha" ]; then
  printf 'Refusing changed HEAD during source synchronization.\n' >&2
  exit 1
fi
if [ "$source_dirty" != "$dirty" ]; then
  printf 'Refusing changed dirty state during source synchronization.\n' >&2
  exit 1
fi
if [ "$source_diff_sha" != "$diff_sha" ]; then
  printf 'Refusing changed source diff during synchronization.\n' >&2
  exit 1
fi
```

The second `capture_source_state` is mandatory and runs immediately after all
three successful rsync operations. Remote CMake configuration proceeds only if
the guarded local source state has the same HEAD, dirty flag, and binary diff
hash captured before transfer. The explicit provenance values therefore
describe the completed remote snapshot: rsync succeeded and no synchronized
source state changed across the transfer. Later local edits are irrelevant to
that snapshot because this workflow performs no later synchronization.

## Configure And Build On The Pi

Pass every provenance value explicitly. Do not rely on a remote `.git`
directory.

```bash
ssh root@192.168.2.34 \
  "cmake -S '$diag_source' -B /tmp/rp2040-w5500-diag-build \
    -DDIAG_BUILD_FIRMWARE=ON \
    -DDIAG_EXPECT_SPI_STATUS=1 \
    -DPICO_SDK_PATH=/usr/share/pico-sdk \
    -DWIZNET_PICO_C_PATH=/usr/share/wiznet-pico-c \
    -DIOLIBRARY_ROOT='$iolibrary_source' \
    -DDIAG_GIT_SHA='$git_sha' \
    -DDIAG_GIT_DIRTY='$dirty' \
    -DDIAG_DIFF_SHA256='$diff_sha' \
    -DDIAG_BUILD_UTC='$build_utc' \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON"
ssh root@192.168.2.34 \
  "cmake --build /tmp/rp2040-w5500-diag-build \
    --target rp2040_w5500_diag --parallel 4 --verbose"
```

Audit the build inputs before flashing:

```bash
ssh root@192.168.2.34 \
  "grep -F '$iolibrary_source/Ethernet/wizchip_conf.c' \
     /tmp/rp2040-w5500-diag-build/compile_commands.json && \
   grep -F '$iolibrary_source/Ethernet/socket.c' \
     /tmp/rp2040-w5500-diag-build/compile_commands.json && \
   grep -F '$iolibrary_source/Ethernet/W5500/w5500.c' \
     /tmp/rp2040-w5500-diag-build/compile_commands.json && \
   grep -F '$iolibrary_source/Internet/DHCP/dhcp.c' \
     /tmp/rp2040-w5500-diag-build/compile_commands.json && \
   ! grep -E '/root/firmware|temperature.server' \
     /tmp/rp2040-w5500-diag-build/compile_commands.json"
scp root@192.168.2.34:/tmp/rp2040-w5500-diag-build/rp2040_w5500_diag.uf2 \
  /tmp/opencode/rp2040_w5500_diag.uf2
sha256sum /tmp/opencode/rp2040_w5500_diag.uf2
```

## Flash With BOOTSEL

1. Disconnect USB power from the board.
2. Hold **BOOTSEL**, reconnect USB to the Pi, then release **BOOTSEL**.
3. Confirm `picotool info` sees the ROM boot device.
4. Load, verify, and reboot the exact UF2:

```bash
ssh root@192.168.2.34 \
  "picotool info && \
   picotool load -v -x /tmp/rp2040-w5500-diag-build/rp2040_w5500_diag.uf2"
```

If `picotool` cannot find a device, repeat the physical BOOTSEL procedure. Do
not substitute a previously built UF2.

## Locate The Diagnostic CDC Device

The firmware identifies as VID:PID `6666:4021`, product
`RP2040 W5500 Diagnostic`, and serial `RP2040-DIAG`.

```bash
ssh root@192.168.2.34 "lsusb -d 6666:4021"
ssh root@192.168.2.34 '
  for device in /dev/ttyACM*; do
    properties=$(udevadm info --query=property --name="$device") || continue
    printf "%s\n" "$properties" | grep -q "^ID_VENDOR_ID=6666$" || continue
    printf "%s\n" "$properties" | grep -q "^ID_MODEL_ID=4021$" || continue
    printf "%s\n" "$device"
  done
'
```

Use the single matching path below as `<device>`. Zero or multiple matches are a
transport/setup error; do not guess which device to open.

## Controller Commands

The controller records every raw CDC event with a UTC host timestamp. Run it on
the Pi so it can reopen the USB device after a watchdog reset.

```bash
python3 /tmp/rp2040-w5500-diag-src/host/diag_host.py \
  --device <device> status
python3 /tmp/rp2040-w5500-diag-src/host/diag_host.py \
  --device <device> run pointer-api
python3 /tmp/rp2040-w5500-diag-src/host/diag_host.py \
  --device <device> repeat pointer-api 100
python3 /tmp/rp2040-w5500-diag-src/host/diag_host.py \
  --device <device> \
  --device-ip 192.168.2.247 --subnet 255.255.255.0 \
  --gateway 192.168.2.1 --listen-ip 192.168.2.34 --listen-port 49000 \
  run-all
```

`status` is read-only and accepts no network options. The host parser rejects
that combination instead of silently ignoring configuration values after the
barrier `STATUS` event.

The firmware shell itself accepts the following newline-terminated commands:

<!-- markdownlint-disable MD013 -->

| Command | Meaning | Pass/fail interpretation |
| --- | --- | --- |
| `help` | Print the command names | `shell HELP` means the request was parsed |
| `status` | Report recovery mode and passed-stage mask | `shell STATUS` is a barrier, not a hardware pass |
| `list` | Describe every stage and timeout | Complete only after `complete=true count=14` |
| `run <stage>` | Run required prerequisites through one stage | Requested run passes only if all required terminal results pass |
| `run all` | Run the complete ordered catalog | Passes only if all 14 expected stages pass |
| `repeat <stage> <count>` | Re-run a stage and its prerequisites | Every requested repetition must pass |
| `net <device-ip> <mask> <gateway> <host-ip> <host-port>` | Install validated static/echo configuration | `shell NET` acknowledges configuration; it does not prove network I/O |
| `reboot` | Emit `shell REBOOT` and request a watchdog reboot | A reboot event is not a stage pass |

<!-- markdownlint-enable MD013 -->

Malformed or unavailable commands produce `shell ERROR` and controller exit
`4`. The controller deliberately does not turn failures or timeouts into passes.

## Stage Interpretation

<!-- markdownlint-disable MD013 -->

| Stage | A pass proves | Representative failure meaning |
| --- | --- | --- |
| `callback-layout` | Independent, safely registrable SPI-status callbacks, or an explicitly unclaimed status contract | `no-status-api` means the audited revision claims status support but exposes no independent registration API; `status-alias` means status registration overwrote SPI transport union storage |
| `transport-init` | PIO transport and non-recursive critical section initialized, callbacks registered | `pio-open` means low-level PIO transport setup failed |
| `chip-reset` | The bounded hardware reset sequence returned | Watchdog timeout identifies a blocked reset driver call |
| `version` | One byte transfer read W5500 `VERSIONR == 0x04` | Wrong value or transfer status identifies basic SPI failure |
| `memory-init` | ioLibrary configured socket TX/RX memory | Failure identifies `wizchip_init` or transfer behavior |
| `phy-link` | Link reached the required state within its deadline | Link-down and transfer/watchdog failures remain distinct |
| `single-register` | Byte save/write/read/restore matched | Readback details identify the mismatching value |
| `burst-register` | Burst save/write/read/restore matched | Details identify the first mismatching byte |
| `pointer-sequential` | Explicit high/low byte pointer operations matched and restored | Phase identifies save, write, read, or restore failure |
| `pointer-burst` | One two-byte buffer operation per pointer matched and restored | Failure isolates burst auto-increment behavior |
| `pointer-api` | Public `setSn_TX_WR` and `setSn_RX_RD` returned, matched, and restored | `TIMEOUT phase=write-tx` on the known revision means the setter acquired an outer non-recursive critical section and then `WIZCHIP_WRITE` recursively acquired it |
| `socket-open` | Static network settings and UDP socket state were accepted | Socket/register snapshots identify setup failure |
| `udp` | The Pi echoed an exact sequence payload and W5500 pointers/state remained valid | Timeout, source, payload, or pointer details identify the failed phase |
| `dhcp` | A bounded run obtained IP, subnet, gateway, and DNS from a complete lease | State transitions and socket snapshots identify incomplete DHCP |

<!-- markdownlint-enable MD013 -->

The accepted DHCP repeat boundary is self-contained: every `dhcp` invocation
performs its own chip reset, memory initialization, and bounded PHY preparation
before starting DHCP. Therefore `repeat dhcp 3` performs three independent DHCP
attempts without replaying the catalog's earlier `chip-reset`, `version`,
`memory-init`, or `phy-link` stage records.

`callback-layout FAIL code=no-status-api` is expected for the audited current
revision with `DIAG_EXPECT_SPI_STATUS=1`. The weak independent registrar is
absent, so the preflight refuses to write callbacks into the aliased
`WIZCHIP.IF` union. Because `callback-layout` performs no W5500 transfer, this
result proves the defect was detected before chip-select or SPI activity.

## Capture Current-Driver Structural Evidence

After flashing, request `callback-layout` directly. The automatic preflight has
already failed on the current driver, leaving later-stage prerequisites
unsatisfied. A later-stage `run` request does not rerun a failed prerequisite,
so the direct request is required to repeat the structural preflight. Preserve
the controller's real exit status and transcript:

```bash
set +e
ssh root@192.168.2.34 \
  "python3 /tmp/rp2040-w5500-diag-src/host/diag_host.py \
    --device <device> run callback-layout" \
  > /tmp/opencode/w5500-diag-current.log 2>&1
controller_status=$?
set -e
cat /tmp/opencode/w5500-diag-current.log
test "$controller_status" -eq 2
boot_record="stage=boot event=PASS git=$git_sha"
boot_record="$boot_record dirty=$dirty diff=$diff_sha"
boot_record="$boot_record build=$build_utc"
grep -F "$boot_record" \
  /tmp/opencode/w5500-diag-current.log
grep -F "stage=callback-layout event=FAIL code=no-status-api" \
  /tmp/opencode/w5500-diag-current.log
```

Exit `2` plus those two exact records is the expected current-driver structural
diagnosis. It does not satisfy corrected-driver acceptance.

## Separate Driver-Correction Acceptance

Build and flash each driver correction separately with provenance from that
candidate's clean worktree. Do not combine driver corrections with this harness
commit.

1. A status-storage/API correction must pass from `callback-layout` through
   `burst-register` (`run burst-register`).
2. A revision with corrected status storage but the isolated nested-lock defect
   must reconnect once and exit `3` on the persisted
   `pointer-api TIMEOUT reset=watchdog phase=write-tx`. The provenance boot event
   must arrive before that timeout, and there must be no reset loop.
3. A candidate pointer correction must complete all commands below with exit
   `0`; every transcript must carry that candidate's provenance:

```bash
ssh root@192.168.2.34 \
  "python3 /tmp/rp2040-w5500-diag-src/host/diag_host.py \
    --device <device> repeat pointer-api 100"
ssh root@192.168.2.34 \
  "python3 /tmp/rp2040-w5500-diag-src/host/diag_host.py \
    --device <device> \
    --device-ip 192.168.2.247 --subnet 255.255.255.0 \
    --gateway 192.168.2.1 --listen-ip 192.168.2.34 --listen-port 49000 \
    repeat udp 20"
ssh root@192.168.2.34 \
  "python3 /tmp/rp2040-w5500-diag-src/host/diag_host.py \
    --device <device> repeat dhcp 3"
```

Physical flashing, USB enumeration, and corrected-candidate runs are mandatory
acceptance evidence. If BOOTSEL access or corrected candidate revisions are not
available, record those as unresolved blockers rather than fabricating results.
