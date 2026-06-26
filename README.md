# WiFi person tracking (Raspberry Pi 5 + Jetson Orin Nano)

A device-free **coarse person localization & tracking** system from WiFi Channel
State Information (CSI), in the spirit of MIT's *"See Through Walls with Wi-Fi!"*
(Adib & Katabi, SIGCOMM 2013). It does **not** reproduce that paper literally —
Wi-Vi used USRP software radios with a 3-antenna MIMO array and interference
nulling. This is the commodity-hardware descendant: a person's body perturbs the
WiFi channel, and we triangulate which transmitter→receiver paths are perturbed.

## Architecture

```
 TX anchors (several known-position WiFi          Pi 5 (BCM43455, 1x1)        Jetson Orin Nano
 devices flooding frames at the Pi)   ---RF--->   Nexmon CSI firmware   --TCP--> DSP + radio-tomographic
                                                  csi_collector (C)      USB    localization + Kalman track
                                                  parse + forward      (CDC-Eth)  [+ optional TensorRT pose]
```

- **Pi 5** runs Nexmon CSI and `pi_collector/` — pure capture/forward, no DSP.
- **Transport** is TCP over a **direct Gigabit Ethernet cable** between the Pi and
  Jetson (default; set static IPs, no router needed). The same code also runs over
  USB CDC-Ethernet, WiFi, or Bluetooth RFCOMM — see Transports below. Ethernet is
  the recommended link: gigabit, low latency, no RF cost, and it leaves the Pi's
  onboard WiFi entirely free for sensing. CSI is tiny (~1 MB/s even at 1 kHz on
  80 MHz), so bandwidth is never the constraint on Ethernet.
- **Jetson** runs `jetson_processor/` — amplitude DSP, motion energy per link,
  least-squares multilateration over a room grid, and a Kalman tracker. Optional
  CUDA localizer in `cuda/` and a TensorRT pose hook in `jetson_processor/infer.h`.

## Step-by-step setup & run

### A. Hardware (one-time)
1. **Connect the Pi and Jetson** with an Ethernet cable (both ports are
   auto-MDIX, so a normal cable is fine).
2. **Give the Jetson internet** (connect its WiFi to your network) and **attach a
   display** to it for the live 3D view.
3. **Place 3-4 TX anchors** (any WiFi devices: APs, ESP32s, spare phones) around
   the room, ideally spread across all walls. Write down each anchor's **MAC
   address** and its **(x, y) position in metres**, plus the **Pi's position**.

### B. Software setup (one-time)
4. **Get the repo on both boards** (clone or copy this folder to each).
5. **Pi — install dependencies** (do this while the Pi still has its normal
   WiFi/internet, i.e. before capture mode):
   ```bash
   make deps-pi
   ```
6. **Pi — install Nexmon CSI** (the one manual, hardware-specific step): switch
   the Pi 5 to the **4 KB-page kernel** and build + install the patched firmware
   so `nexutil` and `makecsiparams` exist. See
   [Raspberry Pi OS / Nexmon CSI setup](#raspberry-pi-os--nexmon-csi-setup-the-real-prerequisite).
7. **Jetson — install dependencies:**
   ```bash
   make deps-jetson
   ```
8. **Jetson — describe your room and anchors** (writes `config.h` for you):
   ```bash
   make configure          # prompts for room size, Pi (RX) position, anchor MAC+xy
   ```
9. **Start your anchors transmitting** so the Pi receives frames and CSI is
   produced — e.g. ping-flood the Pi (or a broadcast) from each anchor:
   ```bash
   sudo ping -f -i 0.001 <pi-ip>      # run on/near each anchor
   ```

### C. Run (every session)
10. **On the Jetson (start this first):**
    ```bash
    make run-jetson         # static IP + internet-share to Pi + processor + 3D view
    ```
11. **On the Pi:**
    ```bash
    make run-pi             # static IP + route via Jetson + Nexmon CSI + collector
    ```
12. **Watch** the OpenGL window on the Jetson track the person. **Ctrl-C** both to
    stop (the Pi's onboard WiFi is handed back to NetworkManager automatically).

Notes: `run-jetson` opens the viewer when a display is present (`VIZ=0` forces
headless; JSON still goes to `track.jsonl`). Everything is tunable via env vars
documented in each script header — `JETSON_IP`, `CHANSPEC` (e.g. `36/80`),
`MACFILTER`, `SHARE_NET=0`, `TRANSPORT=bt`, etc.

## Networking & internet

- **By default the Pi gets its internet through the Jetson.** `run-jetson` shares
  the Jetson's uplink (its WiFi/another NIC) to the Pi over the Ethernet link via
  NAT, and `run-pi` routes the Pi's default through the Jetson (`192.168.100.1`).
  So at runtime the Pi is online *via the Jetson*, even though its onboard WiFi is
  busy sensing. Turn it off with `SHARE_NET=0` on either script.
- The Jetson needs its **own** internet (its WiFi or another NIC) for this to do
  anything. If it has none, sharing is skipped harmlessly and the Pi just runs
  offline — which is fine (timing is clock-independent).
- The Pi's **onboard WiFi is the sensor**, not an uplink, once CSI capture is on;
  it drops off your network when `run-pi` starts and is restored on exit.
- **First-time setup** (apt installs) still needs Pi internet *before* capture:
  use the Pi's normal WiFi (it's a plain station until `run-pi`), a temporary
  Ethernet-to-router, or bring up the Jetson + `run-jetson` share first, then
  `make deps-pi`.

## Honest capabilities & limits

- **The Pi's onboard radio is 1×1** (single antenna/stream). No antenna array ⇒
  **no angle-of-arrival**. Position comes *only* from using **multiple TX anchors**
  whose line-of-sight paths cross the room. One link alone = motion/presence +
  radial Doppler, not (x,y). Use **≥3–4 anchors spread around all walls**; anchors
  clustered on one side make the perpendicular axis weakly observable.
- Accuracy is **room-scale (sub-metre to ~1 m)**, single moving person. Multiple
  people, static-person detection, and through-wall robustness are harder and not
  promised here.
- **Full body pose/skeleton ("body tracking") is NOT provided** and is not
  feasible from a single 1×1 link. That requires multi-antenna CSI tensors + a
  trained neural net (RF-Pose / DensePose-from-WiFi). The Jetson GPU can *run*
  such a model — see `jetson_processor/infer.h` for where it plugs in.

## Build & test (no hardware needed)

```bash
make                 # builds processor + synthetic collector
make test            # runs them together, writes /tmp/track.jsonl
make cuda && ./cuda/localize_cuda   # verify GPU localizer == CPU, benchmark
```

The synthetic collector (`tools/sim_collector`) simulates a person walking
across the room and streams fake CSI, so you can validate the full
DSP→localize→track chain before touching radios.

## Run for real

On the **Jetson**:
```bash
make processor
./jetson_processor/csi_processor -p 9999     # prints one JSON line per tick
```

On the **Pi** (after Nexmon CSI is installed — see below):
```bash
sudo apt install libpcap-dev
make collector
sudo ./pi_collector/csi_collector -i wlan0 -H 192.168.100.1 -p 9999
```

Set **your** room size, Pi (RX) position, and each anchor's **MAC + (x,y)** with
the configurator instead of hand-editing C:

```bash
make configure            # interactive prompts
# or non-interactive:
ROOM_W=6 ROOM_H=5 RX_X=3 RX_Y=0 ./scripts/configure.sh \
    -a TX-NW,aa:bb:cc:dd:ee:01,0,5  -a TX-NE,aa:bb:cc:dd:ee:02,6,5 \
    -a TX-W,aa:bb:cc:dd:ee:03,0,2.5 -a TX-E,aa:bb:cc:dd:ee:04,6,2.5
```

It rewrites only the generated block in `jetson_processor/config.h` (backup at
`config.h.bak`); `run-jetson` rebuilds the processor for you.

## Transports (USB / Ethernet / WiFi / Bluetooth)

The Pi→Jetson link is a reliable byte stream; the same magic-framed protocol runs
over any of these. Pick with `-t`:

| Transport | Jetson (server) | Pi (collector) | Notes |
|---|---|---|---|
| **Direct Ethernet (TCP)** | `csi_processor -p 9999` | `csi_collector -H 192.168.100.1 -p 9999` | **default/recommended**; gigabit, no RF cost |
| USB CDC-Ether / WiFi (TCP) | `csi_processor -p 9999` | `csi_collector -H <ip> -p 9999` | same code, different IP |
| Bluetooth (RFCOMM) | `csi_processor -t bt -C 1` | `csi_collector -t bt -B <jetson_bdaddr> -C 1 -d N` | low bandwidth — see below |

### Direct Ethernet setup (recommended)

Connect a single Ethernet cable Pi↔Jetson (both ports are auto-MDIX, so no
crossover cable needed). With no router/DHCP on that link, give each end a static
address on a private subnet:

```bash
# On the Jetson (server):
sudo ip addr add 192.168.100.1/24 dev eth0
./jetson_processor/csi_processor -p 9999

# On the Pi (collector):
sudo ip addr add 192.168.100.2/24 dev eth0
ping 192.168.100.1                       # sanity check
sudo ./pi_collector/csi_collector -i wlan0 -H 192.168.100.1 -p 9999
```

Make the IPs persistent via NetworkManager/netplan (Jetson) and
`dhcpcd.conf`/NetworkManager (Pi) once you've confirmed it works. The Pi's
internet is provided over this link by the Jetson by default (NAT) — see
**Networking & internet** below.

**Bluetooth needs an opt-in build** (it pulls in `libbluetooth`):
```bash
sudo apt install libbluetooth-dev        # both boards
make WITH_BT=1 -C jetson_processor       # on the Jetson
make WITH_BT=1 -C pi_collector           # on the Pi
```

**Pair the two adapters once** (BlueZ), then connect on a fixed RFCOMM channel:
```bash
# On the Jetson (it is the RFCOMM server):
bluetoothctl
  power on
  agent on
  discoverable on
  pairable on
# On the Pi (initiates pairing to the Jetson's MAC):
bluetoothctl
  power on
  agent on
  pair  AA:BB:CC:DD:EE:FF      # Jetson's bdaddr
  trust AA:BB:CC:DD:EE:FF
# then:
./jetson_processor/csi_processor -t bt -C 1
sudo ./pi_collector/csi_collector -i wlan0 -t bt -B AA:BB:CC:DD:EE:FF -C 1 -d 4
```

**Bandwidth budget (important).** RFCOMM realistically carries ~1.5–2 Mbps. One
CSI record is `26 + n_sub*4` bytes (≈282 B at 20 MHz, ≈1050 B at 80 MHz). To stay
inside the link:
- Prefer a **20 MHz capture** and keep the aggregate frame rate ≲ 600 records/s
  (≈150 Hz across 4 anchors) — that fits without decimation.
- At 40/80 MHz, or higher rates, use **`-d N`** on the collector to forward only
  1 of every N frames per link. Start with `-d 4` and increase if you see the
  collector's `link down` reconnects or growing latency.
- Bluetooth is 2.4 GHz: if you sense on 2.4 GHz WiFi it will add interference —
  prefer a 5 GHz sensing channel when using the BT transport.

## Output format

One JSON object per `PROC_PERIOD_MS` tick on the processor's stdout:
```json
{"t_ns":..,"track":{"x":4.2,"y":2.5,"vx":..,"vy":..,"speed":..,"valid":1},
 "fix":{"x":..,"y":..,"conf":..,"n_active":2,"have":1},
 "links":[{"mac":"..","name":"TX-NE","energy":0.01,"doppler":3.0}, ...]}
```

## Raspberry Pi OS / Nexmon CSI setup (the real prerequisite)

The collector is plain userspace C and runs on **standard 64-bit Raspberry Pi
OS**. What it depends on is **Nexmon CSI firmware**, which patches the BCM43455
firmware to emit CSI UDP frames. On the Pi 5:

1. Use 64-bit Raspberry Pi OS (Lite is fine).
2. Switch to the **4 KB-page kernel** (Pi 5 defaults to 16 KB pages, which nexmon
   does not support): set `kernel=kernel8.img` in `/boot/firmware/config.txt`.
3. Build Nexmon CSI with the **`Makefile.rpi`** variant for recent kernels
   (no patched `brcmfmac` needed). See seemoo-lab/nexmon_csi discussion #395.
4. Configure a capture with `makecsiparams`/`mcp` + `nexutil`, then bring up
   `wlan0`. Have your anchors send frames (e.g. ping flood) so the Pi receives
   and emits CSI.

The collector then captures those UDP frames (dst port 5500) with libpcap and
forwards them. Format parsed: 4-byte magic `0x11111111`, 6-byte src MAC, 2-byte
seq, 2-byte core/nss, 2-byte chanspec, 2-byte chip, then `n_sub×4` bytes of
interleaved int16 real/imag (BCM43455c0 — no float unpacking).

## Layout

```
common/csi_protocol.h     wire format Pi<->Jetson
pi_collector/             Pi 5: Nexmon CSI capture + forward (libpcap)
jetson_processor/         Jetson: net, dsp, localize, tracker, main, infer hook
cuda/localize_cuda.cu     GPU localizer + correctness/bench self-test
viz/csi_viz.c             3D OpenGL live viewer (reads the processor's JSON)
tools/sim_collector.c     synthetic person generator for end-to-end testing
scripts/configure.sh      generate room/RX/anchors block in config.h (make configure)
scripts/install_*.sh      per-device dependency installers (deps-jetson/deps-pi)
scripts/run_*.sh          per-device turn-key launch (run-jetson/run-pi)
scripts/share_net.sh      optional: share Jetson internet to the Pi over the link
```
