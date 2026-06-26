#!/usr/bin/env bash
# install_nexmon_part2.sh -- Nexmon CSI install on the Raspberry Pi 5, PART 2 of 2.
#
# Run AFTER part 1 + reboot, with sudo:
#     sudo ./scripts/install_nexmon_part2.sh
#
# Clones + builds the nexmon framework, installs nexutil, builds + flashes the
# BCM43455c0 CSI firmware (Makefile.rpi), and installs makecsiparams. After this,
# `make run-pi` configures capture and starts the collector.
#
# Tunables (env): NEXMON_DIR (default /opt/nexmon), FWVER (default 7_45_189),
#                 FORCE=1 to proceed even if the page size isn't 4 KB.
# Follows seemoo-lab/nexmon_csi discussion #395. NOT testable from the dev box.
set -eo pipefail

[ "$(id -u)" -eq 0 ] || { echo "run me with sudo (need root)."; exit 1; }

NEXMON_DIR=${NEXMON_DIR:-/opt/nexmon}
FWVER=${FWVER:-7_45_189}
FORCE=${FORCE:-0}

# --- confirm we booted the 4 KB-page kernel (part 1 + reboot) ----------------
PS=$(getconf PAGE_SIZE 2>/dev/null || echo 0)
if [ "$PS" != 4096 ] && [ "$FORCE" != 1 ]; then
    echo "ERROR: page size is $PS, expected 4096."
    echo "       Did you run part 1 and reboot? (Override with FORCE=1.)"
    exit 1
fi

echo "[nexmon-2] cloning nexmon framework into $NEXMON_DIR ..."
mkdir -p "$(dirname "$NEXMON_DIR")"
[ -d "$NEXMON_DIR/.git" ] || git clone --depth=1 https://github.com/seemoo-lab/nexmon.git "$NEXMON_DIR"
cd "$NEXMON_DIR"

echo "[nexmon-2] sourcing build environment ..."
# shellcheck disable=SC1091
source ./setup_env.sh
: "${NEXMON_ROOT:?setup_env.sh did not set NEXMON_ROOT}"

# nexmon's b43 tools need a python2.7 shebang
sed -i '1 s|$|2.7|' "$NEXMON_ROOT/buildtools/b43-v3/debug/b43-beautifier" 2>/dev/null || true

echo "[nexmon-2] building nexmon framework (this takes a while) ..."
make

echo "[nexmon-2] installing nexutil ..."
cd "$NEXMON_ROOT/utilities/nexutil"
make
make install USE_VENDOR_CMD=1
setcap cap_net_admin+ep "$(command -v nexutil)" || true

echo "[nexmon-2] building + flashing the CSI firmware ($FWVER) ..."
PATCHDIR="$NEXMON_ROOT/patches/bcm43455c0/$FWVER"
[ -d "$PATCHDIR" ] || { echo "patch dir $PATCHDIR not found (wrong FWVER for your chip?)"; exit 1; }
cd "$PATCHDIR"
[ -d nexmon_csi/.git ] || git clone --depth=1 https://github.com/seemoo-lab/nexmon_csi.git
cd nexmon_csi
make -f Makefile.rpi install-firmware
make -f Makefile.rpi unmanage
make -f Makefile.rpi reload-full || echo "[nexmon-2] reload-full failed (a reboot also loads the new firmware)"

echo "[nexmon-2] installing makecsiparams ..."
( cd utils/makecsiparams && make && make install )

echo
echo "[nexmon-2] verifying ..."
which nexutil makecsiparams || { echo "tools missing -- build did not complete"; exit 1; }
echo "[nexmon-2] PART 2 complete. Nexmon CSI is installed."
echo "  Next:  make configure   (if not done)   then   make run-pi"
