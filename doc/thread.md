# Thread end device on the software radio

Goal: the ESP32-S3, with no 802.15.4 chip, joins an existing Thread network through the
software O-QPSK radio of [ieee802154.md](ieee802154.md) and passes IPv6 traffic. The first
target network is an Apple Home network whose border router is a HomePod mini.

Branch `thread-end-device`. This file is the plan and the log: each milestone lists what it
needs, how it is checked, and what was found.

## Where we start

From [ieee802154.md](ieee802154.md), "Status":

- PHY: 2 Mchip/s O-QPSK with DSSS and SFD, gapless receive through the 16 MS/s ring,
  200 of 200 frames each way against an ESP32-C6 over coax on channels 11, 15 and 20.
- ACKs: sent 192 us after a frame to this node ends, received for frames that ask for one.
- Not done: CCA (always clear), energy detection, RSSI, address filtering in the driver.
- The Zephyr 802.15.4 L2 build compiles; untested on the air. OpenThread was never built.

## What OpenThread's Zephyr glue needs from the driver

From `modules/zephyr/modules/openthread/platform/radio.c` (Zephyr revision `west.yml` pins):

| Need | Driver today | Plan |
| --- | --- | --- |
| A devicetree node, `zephyr,ieee802154` chosen (`DEVICE_DT_GET`) | `DEVICE_DEFINE` without a node | Binding `esp-sdr,ieee802154`; the driver defines its device from the node when OpenThread is the L2 |
| `IEEE802154_HW_TX_RX_ACK`, else `k_panic()` | Has it | Nothing |
| A net device on `OPENTHREAD_L2` | Only `IEEE802154_L2` or no L2 | Third case in the driver |
| `tx()`: 0, `-ENOMSG` for no ACK, `-EBUSY` for a busy channel | 0, `-ENOMSG`, `-EIO` (maps to abort) | Nothing for M1 |
| `cca()` before CSMA frames (no `HW_CSMA`) | Always 0 (clear) | M3: done, from the AGC's input level |
| RSSI and LQI on received frames | LQI only; RSSI undefined | M2: done, from the AGC's input level at the PHR. Undefined (`INT16_MIN`) reached OpenThread truncated to 0 dBm. Its noise floor is the fixed receive sensitivity (-100 dBm), not `otPlatRadioGetRssi()` |
| Frames with the FCS (`CONFIG_IEEE802154_L2_PKT_INCL_FCS`, on with OpenThread: its frame length counts the FCS) | FCS always stripped | M2: done, kept when the option is on |
| The ACK frame, through `ieee802154_handle_ack()` | ACKs consumed in the driver | Later, for sleepy children: only data polls read it, and an ACK from the burst capture after a transmission has no usable RSSI (the AGC is idle again by then) |
| Filter: PAN ID, short and extended address | Stored, used by auto-ACK | Nothing: OpenThread's MAC filters again in software |
| MAC security | Not offered (no `HW_TX_SEC`) | Nothing: OpenThread encrypts in software |
| Settings in flash | n/a | M1: `CONFIG_OPENTHREAD_SETTINGS_RAM`. A flash write parks the ring's CPU and usually ends the run late (`esp_sdr_ring.h`), costing a receive gap; flash settings come after the join works |

## Milestones

### M0: workspace

- Base: `main` after the upstream sync (Zephyr `03b033e`, every module pin unchanged).
- OpenThread module at `modules/lib/openthread`, revision `4a8d3345` (the one Zephyr's
  `west.yml` pins): `west.yml` adds `openthread` to the allowlist and `west2nix.toml` has its
  entry. Until this branch is in `main`, the workspace's manifest does not list it: fetch it
  by hand and name it in `EXTRA_ZEPHYR_MODULES`.
- The worktree needs the gitignored blobs as symlinks to the main checkout's, one per file in
  real `zephyr/blobs/lib/<chip>/` directories: `zephyr_blobs_verify()` accepts only paths
  `west blobs list` reports, which are the main checkout's, after resolving symlinks.
- Building from the worktree: west resolves the `esp-sdr-zephyr` module to the main checkout.
  An extra module with the same name replaces it (`zephyr_module.py` keys modules by name, extra
  modules last):

```
export PATH=~/dev/esp-sdr-ws/.venv/bin:$PATH
WT=$PWD/esp-sdr-zephyr/.claude/worktrees/thread-end-device
west build --pristine --sysbuild -b xiao_esp32s3/esp32s3/procpu -d build-thread \
    $WT/apps/thread -- \
    -DEXTRA_ZEPHYR_MODULES="$WT;$PWD/modules/lib/openthread"
```

### M1: it builds and the stack comes up

- Driver: OpenThread L2 case (net device from the devicetree node, MTU 1280); the ACK
  shortcut for stray ACKs applies to both L2s.
- `apps/thread`: OpenThread MTD (minimal Thread device), shell (`ot ...`), manual start,
  settings in RAM, USB shell and DFU from `apps/common` like the other S3 apps.
- Fits the internal RAM next to the three ring banks (the DRAM image ends below 0x3fcb0000).
- Check: `ot state` says `disabled`; `ot ifconfig up` starts the ring without errors;
  OpenThread's MAC counts frames from the air. (No C6 on this bench for an `ot scan`.)

### M2: the radio tells the truth

- RSSI per frame: the sample power is no measure (the AGC holds every frame near -10 dBFS),
  but the AGC's own input level is (AGC_R6C_REG): read at the PHR, plus
  `CONFIG_ESP_SDR_IEEE802154_RSSI_OFFSET`.
- Received frames keep their FCS when `CONFIG_IEEE802154_L2_PKT_INCL_FCS` is on.
- Check: plausible RSSI on frames from the air, weaker frames with lower LQI.

### M3: carrier sense

- `cca()`: CCA mode 1 on the AGC's input level, eight reads over 128 us (8 symbols, the
  802.15.4 CCA window), against `CONFIG_ESP_SDR_IEEE802154_CCA_THRESHOLD`. `-EBUSY` when busy.
- Check (not done, no second radio on this bench): with another radio sending a frame every
  millisecond, CCA reports busy; without, clear.

### M4: join the HomePod network

- Credentials: Apple keeps the active dataset in iCloud Keychain. Route to try: the Home
  Assistant companion app imports Apple's Thread credentials, then Home Assistant shows the
  dataset (TLV hex). Unverified.
- On the S3: `ot dataset set active <hex>`, `ot ifconfig up`, `ot thread start`.
- Check: `ot state` is `child`; `ot ping` to the border router's mesh-local address answers;
  from the Mac, `ping6` to the S3's off-mesh-routable address answers.
- Transmit is on the air here, not on coax: keep sessions short and TX power low until M3.

### M5: stays joined

- Settings in flash, written with the ring stopped around the write.
- An hour joined without detaching; child supervision answered.
- Stretch: Matter over IP commissioning, if RAM allows.

## Risks

- **RAM.** OpenThread adds tens of KiB of `.bss` and stacks to an image already bounded by the
  ring banks. PSRAM is the overflow, but no flash access from a thread whose stack is in PSRAM
  ([hardware-quirks.md](hardware-quirks.md)), and OpenThread's tasklet thread writes settings.
- **CPU.** The PHY uses about 52 of its 60 cycles per sample on CPU 0. Joining runs elliptic
  curve and AES in software on the same CPU; dropped samples mean dropped frames, which the
  parent retries.
- **Enhanced ACKs.** A frame of version 2015 with the AR bit needs an Enh-Ack; the driver sends
  only Imm-Acks. OpenThread uses version 2015 only with header IEs (CSL, link metrics), which a
  plain always-on child does not ask for. Watch for it in M4.
- **Retune gaps.** A transmission stops the ring and restarts it; frames arriving then are lost.

## Log

- M0: worktree created, OpenThread `4a8d3345` fetched to `modules/lib/openthread`.
- M0: merged `upstream/main` (43 commits: Zigbee coordinator stack for the C6 in `lib/zigbee`,
  Ubertooth and chancap BLE receive, MicroPython, the multiapp shell command renamed to
  `multiapp`). The 802.15.4 driver is unchanged upstream. Workspace Zephyr checked out at
  `03b033e` by hand from cyber-murmel.
- M1 build: links without warnings. The first link ran 36520 bytes into the ring banks. Moved
  back: OpenThread's instance `.bss` to PSRAM (`CONFIG_ESP_SPIRAM_BSS_RELOC_LIBS_AND_OBJS`,
  18872 bytes), AES tables to flash (`CONFIG_MBEDTLS_AES_ROM_TABLES`, 8 KiB), stack and heap
  sizes from `apps/wpan` and `apps/multiapp`. The DRAM image now ends at 0x3fcaf990, 1648 bytes
  below the banks. PSA crypto needs an ITS store: the settings subsystem (ZMS) in the storage
  partition.
- M1 on hardware (ESP32-S3 N16R8 board, console moved to UART0 for debugging): the first image
  hung at boot. Device and system init (settings, PSA crypto, OpenThread's instance) run on the
  main stack, which overflowed 1536 bytes into the idle stacks below it; the second CPU then
  faulted restoring its idle thread. Measured peaks: main 2576, interrupt stacks 512, system
  work queue 688 (1552 with immediate logging, as the debug build has it). With those sizes it boots: `ot state` is `disabled`,
  `ot ifconfig up` starts the ring (`receiver ready`), and OpenThread's MAC counts frames from
  the air with no FCS errors: on channels 23 and 25 (6 s each: 4 and 3 frames, all addressed
  to other nodes), and once a secured frame it has no key for. `ot promiscuous` needs the
  interface down; its hex dump printed raw bytes in this build.
- M1 production image (shell on USB): boots, confirms, 10 frames in 15 s on channel 25 with
  no FCS errors.
- M2: OpenThread's glue counts the FCS in a received frame's length; the driver stripped it,
  so every frame reached OpenThread two bytes short. Fixed through
  `CONFIG_IEEE802154_L2_PKT_INCL_FCS`.
- M2 RSSI: sample power after the AGC is -11 dBFS on every frame on channel 25 and -7 on 23,
  whatever the sender. AGC_R6C_REG read at each PHR (35 frames): gain index 33 to 66, level
  25 to 53, their sum 86 to 90; idle on noise gain 75 to 78, level 10 to 13. Read after the FCS
  instead it is mostly idle again. With the -108 offset frames on channel 25 read -86 to
  -56 dBm, the weaker ones with lower LQI (230 and 180 at -86 and -80).
- Debug variant: console on UART0 (scratch overlay, not in the branch). With deferred logging
  a second log backend (`CONFIG_LOG_BACKEND_UART` next to the shell's) overflowed the logging
  thread and reset the board at boot; `apps/common`'s fatal error handler reboots without
  printing.
