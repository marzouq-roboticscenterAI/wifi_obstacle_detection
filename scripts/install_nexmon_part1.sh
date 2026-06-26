#!/usr/bin/env bash
# install_nexmon_part1.sh -- Nexmon CSI install on the Raspberry Pi 5, PART 1 of 2.
#
# System prep that must happen BEFORE a reboot: switch to the 4 KB-page kernel
# (nexmon does not support the Pi 5's default 16 KB kernel) and install all the
# build dependencies (incl. the aarch64 32-bit cross-libs and Python 2.7).
#
# Run with sudo, on the Pi, while it still has normal internet:
#     sudo ./scripts/install_nexmon_part1.sh
# It reboots at the end; after reboot run part 2.
#
# Follows seemoo-lab/nexmon_csi discussion #395. NOTE: these steps are
# OS/kernel-version sensitive and are NOT testable from the dev machine -- if an
# apt step fails, check that thread for your kernel.
set -eo pipefail

[ "$(id -u)" -eq 0 ] || { echo "run me with sudo (need root)."; exit 1; }

CONFIG_TXT=${CONFIG_TXT:-/boot/firmware/config.txt}
[ -f "$CONFIG_TXT" ] || CONFIG_TXT=/boot/config.txt   # older layouts

echo "[nexmon-1] selecting the 4 KB-page kernel in $CONFIG_TXT ..."
if grep -q '^kernel=kernel8.img' "$CONFIG_TXT"; then
    echo "          already set."
else
    echo 'kernel=kernel8.img' >> "$CONFIG_TXT"
fi

echo "[nexmon-1] installing build dependencies ..."
apt-get update
apt-get install -y git libgmp3-dev gawk qpdf bison flex make autoconf libtool \
                   texinfo xxd libnl-3-dev libnl-genl-3-dev bc libssl-dev tcpdump

# --- aarch64 (64-bit OS) needs 32-bit toolchain libraries -------------------
if [ "$(dpkg --print-architecture)" = arm64 ]; then
    echo "[nexmon-1] adding armhf 32-bit libraries (aarch64) ..."
    dpkg --add-architecture armhf
    apt-get update
    apt-get install -y libc6:armhf libisl23:armhf libmpfr6:armhf libmpc3:armhf libstdc++6:armhf
    ln -sf /usr/lib/arm-linux-gnueabihf/libisl.so.23 /usr/lib/arm-linux-gnueabihf/libisl.so.10
    ln -sf /usr/lib/arm-linux-gnueabihf/libmpfr.so.6 /usr/lib/arm-linux-gnueabihf/libmpfr.so.4
fi

# --- Python 2.7 from the Debian archive (nexmon build tools need it) ---------
if ! command -v python2.7 >/dev/null 2>&1; then
    echo "[nexmon-1] installing python2.7 from the Debian archive ..."
    LINE='deb http://archive.debian.org/debian/ stretch contrib main non-free'
    grep -qF "$LINE" /etc/apt/sources.list || echo "$LINE" >> /etc/apt/sources.list
    apt-get update || true
    apt-get install -y python2.7 || echo "[nexmon-1] WARN: python2.7 install failed; see #395"
    sed -i '\#archive.debian.org#d' /etc/apt/sources.list
    apt-get update || true
fi

echo
echo "[nexmon-1] PART 1 complete. A reboot is required to load the 4 KB kernel."
if [ -t 0 ]; then
    read -rp "Reboot now? [Y/n] " a || true
    case "${a:-Y}" in
        [Nn]*) echo "Reboot later, then: sudo ./scripts/install_nexmon_part2.sh" ;;
        *)     echo "rebooting ..."; reboot ;;
    esac
else
    echo "Not a TTY -- reboot manually, then run part 2:"
    echo "    sudo ./scripts/install_nexmon_part2.sh"
fi
