# W5500 DHCP RX Pointer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend the lean RP2040 probe to obtain a DHCP lease, receive one routed UDP datagram from the Pi, and verify that `Sn_RX_RD` advances by the expected nine bytes after `recvfrom` completes.

**Architecture:** Add the existing ioLibrary DHCP module to the probe build and keep all runtime logic in `src/main.c`. The probe derives a stable MAC from the RP2040 board ID, runs DHCP with a one-second repeating tick and a 60-second lease deadline, prints its leased address, then opens UDP port 49002. A one-shot Pi command sends `0xA5`; the probe validates the payload and the receive-pointer delta before closing the socket.

**Tech Stack:** C11, RP2040 Pico SDK, WIZnet ioLibrary DHCP/socket APIs, W5500 over the existing PIO SPI transport, USB CDC, Python 3 standard-library UDP sender on the Pi.

## Global Constraints

- Change only `tests/hardware/rp2040_w5500_probe/` for this feature.
- Keep the active probe lean: no persistent host controller, command shell, watchdog, packet echo, or driver changes.
- Use DHCP to prove that traffic passes through the router.
- Use socket 0 for DHCP, then re-use socket 0 for the UDP receive test on port 49002.
- Accept only a one-byte UDP payload of `0xA5`.
- Treat an RX pointer delta of exactly nine bytes as success: eight W5500 UDP receive-header bytes plus one payload byte.
- Bound DHCP acquisition to 60 seconds and packet receipt to 10 seconds.

---

### Task 1: Add DHCP To The Probe Build

**Files:**
- Modify: `tests/hardware/rp2040_w5500_probe/CMakeLists.txt:19-27`
- Modify: `tests/hardware/rp2040_w5500_probe/CMakeLists.txt:55-59`

**Interfaces:**
- Consumes: `IOLIBRARY_ROOT`, already required by the probe CMake configuration.
- Produces: `DHCP_init`, `DHCP_run`, `DHCP_time_handler`, and DHCP lease getters linked into `rp2040_w5500_probe`.

- [ ] **Step 1: Add the DHCP implementation to the existing ioLibrary target**

  Add the source file to `w5500_probe_iolibrary`:

  ```cmake
  add_library(w5500_probe_iolibrary STATIC
      "${IOLIBRARY_ROOT}/Ethernet/wizchip_conf.c"
      "${IOLIBRARY_ROOT}/Ethernet/socket.c"
      "${IOLIBRARY_ROOT}/Ethernet/W5500/w5500.c"
      "${IOLIBRARY_ROOT}/Internet/DHCP/dhcp.c"
  )
  ```

- [ ] **Step 2: Add the DHCP include directory**

  Add the DHCP include path to both target include lists:

  ```cmake
  "${IOLIBRARY_ROOT}/Internet/DHCP"
  ```

- [ ] **Step 3: Configure and build on the Pi**

  Run:

  ```bash
  cmake -S tests/hardware/rp2040_w5500_probe \
    -B /tmp/rp2040-w5500-probe-build \
    -DPICO_SDK_PATH=/usr/share/pico-sdk \
    -DWIZNET_PICO_C_PATH=/usr/share/wiznet-pico-c \
    -DIOLIBRARY_ROOT=/tmp/ioLibrary-probe
  cmake --build /tmp/rp2040-w5500-probe-build --target rp2040_w5500_probe
  ```

  Expected: `Built target rp2040_w5500_probe`.

### Task 2: Add Lean DHCP Lease Acquisition

**Files:**
- Modify: `tests/hardware/rp2040_w5500_probe/src/main.c:1-150`

**Interfaces:**
- Consumes: `DHCP_init`, `DHCP_run`, `DHCP_time_handler`, `getIPfromDHCP`, `getGWfromDHCP`, `getSNfromDHCP`, `getDNSfromDHCP`, `ctlnetwork`, and Pico repeating timers.
- Produces: `lease_dhcp(wiz_NetInfo *network)` returning `true` only after a complete DHCP lease is applied to the W5500, and a CDC record `PROBE dhcp ip=A.B.C.D`.

- [ ] **Step 1: Add the DHCP and board-ID headers**

  ```c
  #include "dhcp.h"
  #include "pico/unique_id.h"

  #include <string.h>
  ```

- [ ] **Step 2: Add constants and DHCP state storage**

  ```c
  #define DHCP_BUFFER_SIZE 548u
  #define DHCP_LEASE_TIMEOUT_MS 60000u

  static uint8_t dhcp_buffer[DHCP_BUFFER_SIZE];

  static bool dhcp_tick(struct repeating_timer *timer)
  {
      (void)timer;
      DHCP_time_handler();
      return true;
  }
  ```

- [ ] **Step 3: Implement deterministic MAC derivation and lease acquisition**

  ```c
  static void derive_mac(const uint8_t board_id[8], uint8_t mac[6])
  {
      mac[0] = 0x02u;
      mac[1] = board_id[2];
      mac[2] = board_id[3];
      mac[3] = board_id[4];
      mac[4] = board_id[5];
      mac[5] = board_id[6];
  }

  static bool lease_dhcp(wiz_NetInfo *network)
  {
      pico_unique_board_id_t board_id;
      struct repeating_timer timer;
      absolute_time_t deadline;

      pico_get_unique_board_id(&board_id);
      memset(network, 0, sizeof(*network));
      derive_mac(board_id.id, network->mac);
      network->dhcp = NETINFO_DHCP;
      if (ctlnetwork(CN_SET_NETINFO, network) != 0) {
          return false;
      }

      reg_dhcp_cbfunc(NULL, NULL, NULL);
      if (!add_repeating_timer_ms(-1000, dhcp_tick, NULL, &timer)) {
          return false;
      }
      DHCP_init(PROBE_SOCKET, dhcp_buffer);
      deadline = make_timeout_time_ms(DHCP_LEASE_TIMEOUT_MS);
      while (!time_reached(deadline)) {
          uint8_t state = DHCP_run();
          if (state == DHCP_FAILED) {
              break;
          }
          if (state == DHCP_IP_ASSIGN || state == DHCP_IP_CHANGED ||
              state == DHCP_IP_LEASED) {
              getIPfromDHCP(network->ip);
              getGWfromDHCP(network->gw);
              getSNfromDHCP(network->sn);
              getDNSfromDHCP(network->dns);
              if (network->ip[0] != 0u && network->gw[0] != 0u &&
                  network->sn[0] != 0u) {
                  cancel_repeating_timer(&timer);
                  DHCP_stop();
                  return ctlnetwork(CN_SET_NETINFO, network) == 0;
              }
          }
          sleep_ms(10u);
      }
      cancel_repeating_timer(&timer);
      DHCP_stop();
      return false;
  }
  ```

- [ ] **Step 4: Replace the static-memory-only progress record with DHCP output**

  After `wizchip_init`, call `lease_dhcp(&network)`. On failure print:

  ```c
  printf("PROBE dhcp=FAIL\n");
  return 1;
  ```

  On success print:

  ```c
  printf("PROBE dhcp ip=%u.%u.%u.%u\n", network.ip[0], network.ip[1],
         network.ip[2], network.ip[3]);
  ```

- [ ] **Step 5: Build with the probe warning policy enabled**

  Run the Task 1 build command.

  Expected: `Built target rp2040_w5500_probe` with no `-Werror` diagnostics from `src/main.c`.

### Task 3: Receive And Validate The One-Shot UDP Packet

**Files:**
- Modify: `tests/hardware/rp2040_w5500_probe/src/main.c:100-190`

**Interfaces:**
- Consumes: a DHCP-configured `wiz_NetInfo`, UDP socket API `socket` and `recvfrom`, `getSn_RX_RD`.
- Produces: `PROBE recv_ready port=49002`, `PROBE rx_rd before=... after=... expected_delta=0009`, and an exit code of zero only for a valid receive.

- [ ] **Step 1: Add the receive-test constants**

  ```c
  #define PROBE_PAYLOAD 0xa5u
  #define UDP_RECEIVE_HEADER_SIZE 8u
  #define UDP_RECEIVE_TIMEOUT_MS 10000u
  ```

- [ ] **Step 2: Replace the transmit pointer test with one receive loop**

  After the DHCP lease succeeds, open the UDP listener and print readiness:

  ```c
  uint8_t payload = 0u;
  uint8_t source_ip[4] = {0};
  uint16_t source_port = 0u;
  uint16_t rx_before;
  uint16_t rx_after;
  absolute_time_t deadline;
  int32_t received = SOCK_BUSY;

  if (socket(PROBE_SOCKET, Sn_MR_UDP, PROBE_PORT, 0u) != PROBE_SOCKET) {
      printf("PROBE udp_open=FAIL\n");
      return 1;
  }
  printf("PROBE recv_ready port=%u\n", PROBE_PORT);
  rx_before = getSn_RX_RD(PROBE_SOCKET);
  deadline = make_timeout_time_ms(UDP_RECEIVE_TIMEOUT_MS);
  while (!time_reached(deadline)) {
      received = recvfrom(PROBE_SOCKET, &payload, 1u, source_ip, &source_port);
      if (received != SOCK_BUSY) {
          break;
      }
      sleep_ms(10u);
  }
  rx_after = getSn_RX_RD(PROBE_SOCKET);
  ```

- [ ] **Step 3: Implement exact validation and failure records**

  ```c
  if (received != 1 || payload != PROBE_PAYLOAD) {
      printf("PROBE recv=FAIL result=%ld payload=%02x\n",
             (long)received, payload);
      close(PROBE_SOCKET);
      return 1;
  }
  if ((uint16_t)(rx_after - rx_before) !=
      UDP_RECEIVE_HEADER_SIZE + 1u) {
      printf("PROBE rx_rd=FAIL before=%04x after=%04x expected_delta=0009\n",
             rx_before, rx_after);
      close(PROBE_SOCKET);
      return 1;
  }
  printf("PROBE rx_rd before=%04x after=%04x expected_delta=0009\n",
         rx_before, rx_after);
  ```

- [ ] **Step 4: Run the existing host lock regression**

  Run:

  ```bash
  gcc -std=c99 -Wall -Wextra -Werror -Wno-unused-parameter \
    -D_WIZCHIP_=W5500 -I Ethernet -I Ethernet/W5500 \
    tests/test_w5500_atomic_pointer_write.c Ethernet/wizchip_conf.c \
    Ethernet/W5500/w5500.c -o /tmp/opencode/test_w5500_atomic_pointer_write
  /tmp/opencode/test_w5500_atomic_pointer_write
  ```

  Expected: exit code 0 and no output.

### Task 4: Verify On Routed Hardware

**Files:**
- Modify: `tests/hardware/rp2040_w5500_probe/README.md:3-29`

**Interfaces:**
- Consumes: the lease record printed over `/dev/ttyACM0` and the built UF2.
- Produces: repeatable Pi commands for flashing, observing the DHCP lease, and sending `0xA5` to port 49002.

- [ ] **Step 1: Document the one-shot Pi sender**

  Add this command, replacing `DEVICE_IP` with the CDC-reported lease:

  ```bash
  python3 -c 'import socket; socket.socket(socket.AF_INET, socket.SOCK_DGRAM).sendto(bytes([0xA5]), ("DEVICE_IP", 49002))'
  ```

- [ ] **Step 2: Build the final UF2 on the Pi**

  Run the Task 1 build command.

  Expected: `/tmp/rp2040-w5500-probe-build/rp2040_w5500_probe.uf2` is generated.

- [ ] **Step 3: Flash in BOOTSEL mode**

  Run:

  ```bash
  picotool load -v -x /tmp/rp2040-w5500-probe-build/rp2040_w5500_probe.uf2
  ```

  Expected: `Verifying Flash: ... OK` and a reboot into the application.

- [ ] **Step 4: Capture the lease and send the packet**

  Open `/dev/ttyACM0`, wait for `PROBE dhcp ip=...` and `PROBE recv_ready port=49002`, then run the documented one-shot sender with that IP.

  Expected transcript:

  ```text
  PROBE dhcp ip=A.B.C.D
  PROBE recv_ready port=49002
  PROBE rx_rd before=.... after=.... expected_delta=0009
  PROBE final_state=00
  ```

- [ ] **Step 5: Check the final diff and commit**

  Run:

  ```bash
  git diff --check
  git status --short
  ```

  Expected: only the probe CMake file, probe source, probe README, and this plan/spec documentation have intended changes.

  Commit only after the user explicitly requests it:

  ```bash
  git add tests/hardware/rp2040_w5500_probe docs/superpowers/specs/2026-07-20-w5500-dhcp-rx-pointer-design.md docs/superpowers/plans/2026-07-20-w5500-dhcp-rx-pointer.md
  git commit -m "test(w5500): validate DHCP UDP receive pointer"
  ```
