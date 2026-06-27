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

> The Pi gets its internet **from the Jetson over Ethernet** — its onboard WiFi is
> never needed for setup (it's about to become a sensor). Bring up that gateway
> first, do everything else, and install **Nexmon last** (it degrades the Pi's
> WiFi by design — see the Nexmon caveat).

4. **Get the repo on both boards** (clone or copy this folder to each).
5. **Jetson — install dependencies** (uses the Jetson's own WiFi internet):
   ```bash
   make deps-jetson
   ```
6. **Jetson — describe your room and anchors** (writes `config.h` for you):
   ```bash
   make configure          # room size, Pi (RX) position, anchor MAC+xy
   ```
7. **Jetson — bring up the gateway** (shares its internet to the Pi over Ethernet):
   ```bash
   make net-jetson
   ```
8. **Pi — get online through the Jetson** over Ethernet (no WiFi needed):
   ```bash
   make net-pi             # then verify: ping -c2 1.1.1.1
   ```
9. **Pi — install dependencies** (now online via the Jetson):
   ```bash
   make deps-pi
   ```
10. **Start your anchors transmitting** so the Pi receives frames and CSI is
    produced — e.g. ping-flood the Pi from each anchor:
    ```bash
    sudo ping -f -i 0.001 <pi-ip>
    ```
11. **Pi — install Nexmon CSI (the LAST step).** It produces CSI but degrades the
    onboard WiFi, so do it once everything above is done:
    ```bash
    sudo make nexmon-part1     # system prep + 4 KB kernel switch, then reboots
    sudo make nexmon-part2     # auto-restores internet via the Jetson, then builds + flashes
    ```
    (`nexmon-part2` runs `net-pi` for you to restore Ethernet internet after the
    reboot; pass `NET_PI=0` if the Pi uses a non-Jetson uplink for the install.)
    Details / manual steps: [Raspberry Pi OS / Nexmon CSI setup](#raspberry-pi-os--nexmon-csi-setup-the-real-prerequisite).
    From here on the Pi runs over **Ethernet** (the onboard WiFi is the sensor).

### C. Run (every session)
12. **On the Jetson (start this first):**
    ```bash
    make run-jetson         # gateway + processor + 3D view (re-applies net-jetson)
    ```
13. **On the Pi:**
    ```bash
    make run-pi             # net + Nexmon CSI config + collector (re-applies net-pi)
    ```
14. **Watch** the OpenGL window on the Jetson track the person. **Ctrl-C** both to
    stop (the Pi's onboard WiFi is handed back to NetworkManager automatically).

Notes: `run-jetson` opens the viewer when a display is present (`VIZ=0` forces
headless; JSON still goes to `track.jsonl`). Everything is tunable via env vars
documented in each script header — `JETSON_IP`, `CHANSPEC` (e.g. `36/80`),
`MACFILTER`, `SHARE_NET=0`, `TRANSPORT=bt`, etc.

## Networking & internet

- **The Pi gets its internet through the Jetson over Ethernet.** Bring the gateway
  up explicitly and early with **`make net-jetson`** (Jetson: static IP + NAT
  sharing its WiFi uplink) and **`make net-pi`** (Pi: static IP + default route +
  DNS via the Jetson). `run-jetson`/`run-pi` re-apply these, so you can also rely
  on them at run time. So the Pi never needs its own WiFi for internet — including
  during `deps-pi` and the Nexmon install. Turn sharing off with `SHARE_NET=0`.
- The Jetson needs its **own** internet (its WiFi or another NIC) for this to work.
  If it has none, sharing is skipped harmlessly and the Pi just runs offline —
  which is fine for tracking (timing is clock-independent), but `deps-pi`/Nexmon
  would then need another internet source.
- The Pi's **onboard WiFi is the sensor**, not an uplink, once CSI capture is on;
  it drops off your network when `run-pi` starts and is restored on exit.
- `ip`-based addressing isn't persistent across reboots, but you don't have to
  think about it: **`nexmon-part2` automatically runs `net-pi`** to restore the
  Pi's internet via the Jetson after the part-1 reboot (skip with `NET_PI=0` if
  the Pi has its own uplink). `run-pi` re-applies it too.

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
OS**. What it depends on is **Nexmon CSI firmware**, which patches the BCM43455c0
firmware to emit CSI UDP frames. This is the one step that isn't a single
`make` — it's a firmware build, and the Pi 5 (aarch64) needs extra setup. The
steps below follow the official Pi 5 procedure (seemoo-lab/nexmon_csi discussion
#395); **commands drift with OS/kernel versions**, so if something fails, check
that thread for your kernel.

> Do this on the Pi *while it still has normal internet* (before capture mode),
> and do it **last** in your setup — see the caveat just below.

> ⚠️ **The Nexmon firmware makes the Pi's onboard WiFi slow/flaky — by design.**
> `install-firmware` replaces the BCM43455 firmware with a CSI/monitor-oriented
> patched build (and `unmanage` releases `wlan0` from NetworkManager). Normal
> WiFi on the Pi will be slower, higher-latency, and may "connect but not load
> pages." That's expected: in this project the Pi's onboard WiFi is the **sensor**,
> not your uplink — **run the Pi over Ethernet** for internet/`apt`/SSH. To get
> standard WiFi back, restore stock firmware — **offline** (no internet needed):
> `sudo make restore-firmware` then reboot. (`nexmon-part2` automatically snapshots
> the stock firmware to `firmware_backup/` before flashing; the restore script
> falls back to nexmon's `*.orig` backups or a cached firmware `.deb`. Online
> fallback: `sudo apt install --reinstall firmware-brcm80211`.)

**Scripted install (recommended)** — two parts, because part 1 reboots into the
new kernel:
```bash
sudo ./scripts/install_nexmon_part1.sh    # system prep + 4 KB kernel, then reboots
# ... Pi reboots ...
sudo ./scripts/install_nexmon_part2.sh    # clone, build, flash firmware, install tools
```
(equivalently `sudo make nexmon-part1` then, after reboot, `sudo make nexmon-part2`).
Tunables: `NEXMON_DIR` (default `/opt/nexmon`), `FWVER` (default `7_45_189`).

The manual equivalent of what those scripts do is below, for reference and
troubleshooting (`make deps-pi` already covers the common packages):

**1. Switch the Pi 5 to the 4 KB-page kernel** (nexmon doesn't support the
default 16 KB-page kernel), then reboot:
```bash
echo 'kernel=kernel8.img' | sudo tee -a /boot/firmware/config.txt
sudo reboot
```

**2. Build dependencies** (superset of `make deps-pi`):
```bash
sudo apt update
sudo apt install -y git libgmp3-dev gawk qpdf bison flex make autoconf libtool \
                    texinfo xxd libnl-3-dev libnl-genl-3-dev bc libssl-dev tcpdump
```

**3. (aarch64 only) add 32-bit libs the nexmon toolchain needs:**
```bash
sudo dpkg --add-architecture armhf
sudo apt update
sudo apt install -y libc6:armhf libisl23:armhf libmpfr6:armhf libmpc3:armhf libstdc++6:armhf
sudo ln -sf /usr/lib/arm-linux-gnueabihf/libisl.so.23  /usr/lib/arm-linux-gnueabihf/libisl.so.10
sudo ln -sf /usr/lib/arm-linux-gnueabihf/libmpfr.so.6  /usr/lib/arm-linux-gnueabihf/libmpfr.so.4
```

**4. Python 2.7** (nexmon build tools require it; pull from the Debian archive):
```bash
echo 'deb http://archive.debian.org/debian/ stretch contrib main non-free' | sudo tee -a /etc/apt/sources.list
sudo apt update
sudo apt install -y python2.7
sudo sed -i '/archive.debian.org/d' /etc/apt/sources.list && sudo apt update
```

**5. Build the nexmon framework + nexutil:**
```bash
cd ~
git clone --depth=1 https://github.com/seemoo-lab/nexmon.git
cd nexmon
source setup_env.sh
sed -i '1 s|$|2.7|' "$NEXMON_ROOT/buildtools/b43-v3/debug/b43-beautifier"   # use python2.7
make
cd "$NEXMON_ROOT/utilities/nexutil"
sudo -E make install USE_VENDOR_CMD=1
sudo setcap cap_net_admin+ep /usr/bin/nexutil
```

**6. Build + install the CSI firmware (Pi variant) and `makecsiparams`:**
```bash
cd "$NEXMON_ROOT/patches/bcm43455c0/7_45_189"
git clone --depth=1 https://github.com/seemoo-lab/nexmon_csi.git
cd nexmon_csi
make -f Makefile.rpi install-firmware     # build + flash the patched firmware
make -f Makefile.rpi unmanage             # stop NetworkManager managing wlan0
( cd utils/makecsiparams && make && sudo make install )   # provides makecsiparams/mcp
```

**7. Verify** the tools exist and the firmware is live:
```bash
which nexutil makecsiparams        # both should resolve
nexutil -I wlan0 -g                # talks to the patched firmware (no error)
```

After this, **`make run-pi` handles the per-session configuration** for you
(it runs `makecsiparams` + `nexutil` with your `CHANSPEC`/`MACFILTER` and starts
the collector). The collector captures the resulting UDP frames (dst port 5500)
with libpcap. Wire format parsed: 4-byte magic `0x11111111`, 6-byte src MAC,
2-byte seq, 2-byte core/nss, 2-byte chanspec, 2-byte chip, then `n_sub×4` bytes
of interleaved int16 real/imag (BCM43455c0 — no float unpacking).

Sanity check before launching the tracker:
```bash
sudo tcpdump -i wlan0 dst port 5500    # should show packets once an anchor transmits
```

### Alternative: prebuilt firmware (nexmonster)

If building from source fights with your kernel, the community **nexmonster**
fork ships **prebuilt** CSI firmware, skipping the toolchain / Python-2.7 build:

```bash
uname -r                                            # note your exact kernel
git clone https://github.com/nexmonster/nexmon_csi.git ~/nexmon_csi
# checkout the branch matching your kernel (e.g. pi-5.10.92, pi-5.4.51-plus),
# then follow that branch's README -- it installs prebuilt firmware + nexutil/mcp.
```

Honest caveat: these prebuilt builds are **pinned to specific kernel versions**
and historically target the Pi 3B+/4/Zero 2 W. There may be **no prebuilt for the
Pi 5 / your exact kernel**, in which case the from-source route above (discussion
#395) is the one to use. Check `uname -r` against the available branches first;
if there's no match, prefer `make nexmon-part1`/`part2`.

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
scripts/install_nexmon_*  Pi Nexmon CSI install, part 1/2 (make nexmon-part1/2)
scripts/backup_firmware.sh / restore_firmware.sh   offline stock-WiFi restore
scripts/net_jetson.sh / net_pi.sh   bring up Ethernet + Jetson->Pi internet (net-jetson/net-pi)
scripts/run_*.sh          per-device turn-key launch (run-jetson/run-pi)
scripts/share_net.sh      NAT helper used by net-jetson (share Jetson internet to the Pi)
CLAUDE.md                 architecture + constraints + troubleshooting runbook
```
