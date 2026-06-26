#!/usr/bin/env bash
# share_net.sh -- run on the JETSON. Optional.
#
# The Pi's onboard WiFi is busy doing CSI sensing and the Pi<->Jetson Ethernet
# link is just a private data link (no internet by default). If you want the Pi
# to reach the internet anyway (apt, SSH from your LAN, NTP time sync), this
# shares the JETSON's uplink to the Pi over that Ethernet link via NAT.
#
# The Jetson gets its own internet however it likes (its WiFi, or another NIC);
# this forwards that to the Pi. Not required for the tracker to run.
set -euo pipefail

ETH=${ETH:-eth0}                       # the link to the Pi
JETSON_IP=${JETSON_IP:-192.168.100.1}
# uplink = whichever interface holds the default route (usually the Jetson's WiFi)
UPLINK=${UPLINK:-$(ip route show default 2>/dev/null | awk '/default/{print $5; exit}')}
[ -n "$UPLINK" ] || { echo "no default-route uplink found; set UPLINK=<iface>"; exit 1; }

echo "[share-net] sharing $UPLINK -> Pi over $ETH"
sudo sysctl -w net.ipv4.ip_forward=1 >/dev/null

add_rule() { sudo iptables -C "$@" 2>/dev/null || sudo iptables -A "$@"; }
sudo iptables -t nat -C POSTROUTING -o "$UPLINK" -j MASQUERADE 2>/dev/null \
  || sudo iptables -t nat -A POSTROUTING -o "$UPLINK" -j MASQUERADE
add_rule FORWARD -i "$ETH" -o "$UPLINK" -j ACCEPT
add_rule FORWARD -i "$UPLINK" -o "$ETH" -m state --state RELATED,ESTABLISHED -j ACCEPT

echo "[share-net] done."
echo "  On the PI, point its default route at the Jetson:"
echo "     sudo ip route add default via $JETSON_IP"
echo "     echo 'nameserver 1.1.1.1' | sudo tee /etc/resolv.conf"
