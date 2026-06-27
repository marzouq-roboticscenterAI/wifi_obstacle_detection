#!/usr/bin/env bash
# net_pi.sh -- bring up the Pi's wired link to the Jetson AND get the Pi online
# through the Jetson (default route + DNS over Ethernet). Run this on the Pi
# (after `make net-jetson` on the Jetson) BEFORE deps-pi / the Nexmon install,
# so the Pi has internet over Ethernet instead of its onboard WiFi (which is
# about to become a CSI sensor). Idempotent; does NOT start the collector.
# `make run-pi` calls this too, so ordering is forgiving.
#
# NOTE: `ip` settings are not persistent -- after the part-1 reboot, run
# `make net-pi` again before `make nexmon-part2`.
#
#   make net-pi
#   ETH=eth0 PI_IP=192.168.100.2 JETSON_IP=192.168.100.1 ./scripts/net_pi.sh
#   SHARE_NET=0 ./scripts/net_pi.sh        # link only, no internet-via-Jetson
set -eo pipefail

ETH=${ETH:-eth0}
PI_IP=${PI_IP:-192.168.100.2}
JETSON_IP=${JETSON_IP:-192.168.100.1}
SHARE_NET=${SHARE_NET:-1}            # 1 = also route internet via the Jetson
DNS=${DNS:-1.1.1.1}

echo "[net-pi] bringing up $ETH = $PI_IP/24 ..."
sudo ip link set "$ETH" up
sudo ip addr replace "$PI_IP/24" dev "$ETH"

if [ "$SHARE_NET" = 1 ]; then
    echo "[net-pi] routing internet via the Jetson ($JETSON_IP) ..."
    sudo ip route replace default via "$JETSON_IP" dev "$ETH" || true
    grep -q "$DNS" /etc/resolv.conf 2>/dev/null \
        || echo "nameserver $DNS" | sudo tee -a /etc/resolv.conf >/dev/null || true
    echo "[net-pi] test:  ping -c2 $JETSON_IP   then   ping -c2 $DNS"
fi

ip -brief addr show "$ETH" 2>/dev/null || true
echo "[net-pi] wired link ready (make sure 'make net-jetson' ran on the Jetson first)."
