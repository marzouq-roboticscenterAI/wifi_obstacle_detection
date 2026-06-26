# CLAUDE.md — guidance & runbook for future sessions

WiFi CSI **device-free person localization/tracking**. A Raspberry Pi 5 captures
Channel State Information (Nexmon CSI) and streams it to a Jetson Orin Nano, which
runs the DSP + localization + Kalman tracker and a live 3D view. Inspired by MIT's
"See Through Walls with WiFi" (Wi-Vi); this is the commodity-hardware descendant,
not a literal reproduction. Full docs: **README.md**.

## Repo map
- `common/csi_protocol.h` — Pi→Jetson wire format
- `pi_collector/` — Pi: Nexmon CSI capture (libpcap) + forward (TCP or Bluetooth)
- `jetson_processor/` — Jetson: `net` → `dsp` → `localize` → `tracker` → `main`;
  `config.h` (room/anchors, has a generated block); `infer.h` (pose stub)
- `cuda/localize_cuda.cu` — GPU localizer + self-test
- `viz/csi_viz.c` — 3D OpenGL viewer (reads processor JSON over a pipe)
- `tools/sim_collector.c` — synthetic CSI generator (end-to-end test, no hardware)
- `scripts/` — install/configure/run/firmware helpers (see Make targets)

## Build & run (all from repo root; per-device targets)
`make` (processor+sim) · `make cuda` · `make viz` · `make test` (sim end-to-end)
Setup: `make deps-jetson` / `make deps-pi` · `make configure` (room+anchors) ·
`sudo make nexmon-part1` → reboot → `sudo make nexmon-part2` (Pi firmware) ·
`make backup-firmware` / `make restore-firmware` (offline stock-WiFi restore)
Run: `make run-jetson` (first) · `make run-pi` · `make share-net` (Pi internet via Jetson)
Bluetooth transport: `TRANSPORT=bt` (builds with `WITH_BT=1`).

## Hard constraints — do NOT relitigate these
- **Pi 5 BCM43455 is 1×1** (one antenna): no angle-of-arrival. Localization needs
  **multiple TX anchors** whose paths cross the room; one link = motion/Doppler only.
- **The Jetson's WiFi cannot do CSI** (it's Realtek/Intel/MediaTek, not Broadcom;
  Nexmon is Broadcom-only). The Jetson is compute/aggregator, not a sensor. More
  receive antennas ⇒ add more **Raspberry Pis** or ESP32 CSI nodes (multistatic;
  radios aren't phase-synced so no coherent array).
- **Full body pose** is NOT implemented and not feasible from 1×1 CSI — needs
  multi-antenna tensors + a trained net. `jetson_processor/infer.h` is the hook.
- **Nexmon is tightly coupled to Raspberry Pi OS.** Ubuntu (esp. brand-new 26.04)
  makes it much harder (firmware-version mismatch, no `Makefile.rpi` assumptions).
  Recommend **64-bit Raspberry Pi OS** on the Pi. The Pi need NOT match the Jetson's
  OS — they talk over OS-agnostic TCP.
- **Transport is a reliable byte stream** (transport-agnostic). Default = direct
  Ethernet (Jetson `192.168.100.1`, Pi `192.168.100.2`). Also USB CDC-Ether, WiFi,
  Bluetooth RFCOMM. Code never changes for IP transports — only the `-H` target.
- **Processor timing uses `CLOCK_MONOTONIC`** (link staleness, tracker dt), never the
  Pi's wire timestamp — so an offline/unsynced Pi clock can't stall tracking.
  `dsp.c` needs `_POSIX_C_SOURCE` for `clock_gettime`.

## Setup ORDER matters
Do **Nexmon last** — it patches the Pi's WiFi firmware and degrades the onboard
WiFi. Order: Jetson `deps`+`configure` → Pi `deps` → start anchors → Pi Nexmon.
Run the Pi's setup on **Ethernet** so the firmware patch can't strand it.

## Troubleshooting runbook (issues we've actually hit)
- **"Pi connects to internet but browsers won't load / WiFi is slow."** Two causes,
  often together: (1) **Nexmon's patched firmware** makes onboard WiFi slow/flaky
  BY DESIGN — use Ethernet on the Pi; restore stock WiFi offline with
  `sudo make restore-firmware` + reboot. (2) **DNS**: `ping -c3 1.1.1.1` (IP) vs
  `ping -c3 google.com` (name) — if IP works and name fails, fix DNS (set
  `nameserver 1.1.1.1`; on Ubuntu via netplan, on Pi OS via nmcli). Also check
  `ip route` for a `default via 192.168.100.1` left by `run-pi`/`share-net` while
  the Jetson NAT is down — restore DHCP if so.
- **`make nexmon-part2` fails with "Error 2".** Generic make failure — ask for the
  **last `[nexmon-2] …` banner** + ~20 lines above the error. Common: page size ≠
  4096 (`getconf PAGE_SIZE`; part 1 didn't reboot into the 4 KB kernel), broken
  armhf `libisl.so.10`/`libmpfr.so.4` symlinks (point them at the versions actually
  installed), or missing kernel headers for `reload-full`.
- **No CSI produced.** `sudo tcpdump -i wlan0 dst port 5500` should show packets.
  If empty: anchors aren't transmitting (ping-flood them), `nexutil` config not
  applied, or wrong `CHANSPEC`. The collector logs `forwarded=` when packets flow.
- **Tracker never gets a fix (`n_active < 2`).** Wrong/placeholder anchor MACs in
  `config.h` (run `make configure`), anchors not transmitting, or anchors clustered
  on one side (poor geometry). The localizer needs ≥2 perturbed links.
- **Track parks in a corner / one axis is wrong.** Weak observability perpendicular
  to the anchor layout — spread anchors across all walls. (Known sim behavior.)
- **3D viewer won't open.** Needs `freeglut3-dev` and a `DISPLAY`. `VIZ=0` runs
  headless (JSON still written to `track.jsonl`).

## Verification status (important for honesty)
VERIFIED here (x86 dev box, RTX 5060): all C builds clean (`-Wall -Wextra`), CUDA
localizer matches CPU + benchmarks, full pipeline tracks a **simulated** walker
end-to-end, all scripts `bash -n` clean.
NOT verifiable from the dev box (validate on real hardware first): real Nexmon CSI
capture, the Ethernet/NAT path, the OpenGL window, and the Nexmon build on a
specific Pi kernel. Be upfront about this with the user.

## Working agreements
- Keep the heavy code in **C** (CUDA/C++ only where it must be); transport-agnostic.
- Be explicit about what's tested vs. assumed; never claim hardware works when it's
  only been simulated.
- Persistent project knowledge also lives in the memory dir
  (`MEMORY.md` → `project-wifi-tracking.md`).
