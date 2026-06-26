#!/usr/bin/env bash
# install_pi.sh -- install all dependencies on the Raspberry Pi 5 (collector).
#
# Covers: the collector's build deps (libpcap, optional Bluetooth) AND the
# prerequisites needed to BUILD Nexmon CSI (the actual firmware that produces
# CSI). It does NOT build/flash Nexmon itself -- that step is kernel-specific
# and needs the 4 KB-page kernel; see the README. Optionally clones the repo.
#
#   WITH_BT=0 ./scripts/install_pi.sh          # skip Bluetooth packages
#   CLONE_NEXMON=1 ./scripts/install_pi.sh     # also git-clone nexmon_csi to ~
set -euo pipefail

WITH_BT=${WITH_BT:-1}
CLONE_NEXMON=${CLONE_NEXMON:-0}

command -v apt-get >/dev/null || { echo "this script expects Raspberry Pi OS (apt)"; exit 1; }

echo "[install-pi] apt-get update ..."
sudo apt-get update

PKGS=(build-essential git libpcap-dev)
# Nexmon CSI build prerequisites:
PKGS+=(automake bc bison flex gawk libgmp3-dev libtool-bin make qpdf texinfo libssl-dev)
if [ "$WITH_BT" = 1 ]; then PKGS+=(bluez libbluetooth-dev); fi

echo "[install-pi] installing: ${PKGS[*]}"
sudo apt-get install -y "${PKGS[@]}"

echo "[install-pi] installing kernel headers (for the Nexmon driver build) ..."
sudo apt-get install -y raspberrypi-kernel-headers \
  || sudo apt-get install -y "linux-headers-$(uname -r)" \
  || echo "[install-pi] WARN: could not auto-install kernel headers; install the headers matching 'uname -r' manually."

if [ "$CLONE_NEXMON" = 1 ] && [ ! -d "$HOME/nexmon_csi" ]; then
    echo "[install-pi] cloning nexmon_csi to ~/nexmon_csi ..."
    git clone https://github.com/seemoo-lab/nexmon_csi.git "$HOME/nexmon_csi"
fi

echo
echo "[install-pi] dependency install complete."
echo "  Next steps (manual, see README):"
echo "   1. Switch the Pi 5 to the 4 KB-page kernel (config.txt: kernel=kernel8.img)."
echo "   2. Build + install Nexmon CSI (Makefile.rpi variant) so 'nexutil' and"
echo "      'makecsiparams' are available."
echo "   3. Then:  make run-pi"
