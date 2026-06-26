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
- **Transport** is TCP. Over the USB 3.0 link the Jetson appears as a USB
  CDC-Ethernet gadget (`192.168.55.1`); the same code also works over WiFi/LAN.
  CSI is tiny (~1 MB/s even at 1 kHz on 80 MHz), so USB 2.0 is already plenty.
- **Jetson** runs `jetson_processor/` — amplitude DSP, motion energy per link,
  least-squares multilateration over a room grid, and a Kalman tracker. Optional
  CUDA localizer in `cuda/` and a TensorRT pose hook in `jetson_processor/infer.h`.

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
sudo ./pi_collector/csi_collector -i wlan0 -H 192.168.55.1 -p 9999
```

Edit `jetson_processor/config.h` with **your** room size, the Pi (RX) position,
and each anchor's **MAC + (x,y)**. Recompile the processor after editing.

## Transports (USB / Ethernet / WiFi / Bluetooth)

The Pi→Jetson link is a reliable byte stream; the same magic-framed protocol runs
over any of these. Pick with `-t`:

| Transport | Jetson (server) | Pi (collector) | Notes |
|---|---|---|---|
| USB / Ethernet / WiFi (TCP) | `csi_processor -p 9999` | `csi_collector -H <ip> -p 9999` | default; fast, no RF cost |
| Bluetooth (RFCOMM) | `csi_processor -t bt -C 1` | `csi_collector -t bt -B <jetson_bdaddr> -C 1 -d N` | low bandwidth — see below |

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
tools/sim_collector.c     synthetic person generator for end-to-end testing
```
