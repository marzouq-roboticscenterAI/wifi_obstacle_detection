#!/usr/bin/env bash
# install_jetson.sh -- install all dependencies on the Jetson Orin Nano (processor).
#
# Covers: processor build (gcc), the 3D visualizer (freeglut/GLU), and optional
# Bluetooth. CUDA is NOT apt-installed -- on Jetson it ships with JetPack; this
# script just checks that nvcc is present.
#
#   WITH_BT=0 ./scripts/install_jetson.sh      # skip Bluetooth packages
#   WITH_VIZ=0 ./scripts/install_jetson.sh     # skip the 3D viewer deps
set -euo pipefail

WITH_BT=${WITH_BT:-1}
WITH_VIZ=${WITH_VIZ:-1}

command -v apt-get >/dev/null || { echo "this script expects Ubuntu/L4T (apt)"; exit 1; }

echo "[install-jetson] apt-get update ..."
sudo apt-get update

PKGS=(build-essential python3)
if [ "$WITH_VIZ" = 1 ]; then PKGS+=(freeglut3-dev libglu1-mesa-dev mesa-utils); fi
if [ "$WITH_BT" = 1 ];  then PKGS+=(bluez libbluetooth-dev); fi

echo "[install-jetson] installing: ${PKGS[*]}"
sudo apt-get install -y "${PKGS[@]}"

echo
if command -v nvcc >/dev/null; then
    echo "[install-jetson] CUDA OK: $(nvcc --version | sed -n 's/^.*release /release /p')"
else
    echo "[install-jetson] WARN: nvcc not found. CUDA on Jetson comes with JetPack;"
    echo "                 install via SDK Manager or 'sudo apt install nvidia-jetpack'."
    echo "                 (The classical pipeline + viz work fine without CUDA.)"
fi

echo
echo "[install-jetson] dependency install complete.  Next:  make run-jetson"
