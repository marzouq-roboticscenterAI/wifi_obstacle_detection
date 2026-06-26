#!/usr/bin/env bash
# restore_firmware.sh -- restore the STOCK BCM43455 WiFi firmware (undo Nexmon),
# entirely OFFLINE. Run with sudo. Tries, in order:
#   1. our pre-patch snapshot   (./firmware_backup, from backup_firmware.sh)
#   2. nexmon's own *.orig backups in the firmware dir
#   3. a cached firmware .deb in /var/cache/apt/archives (offline extract)
# Then re-manages wlan0. Reboot to load the restored firmware.
set -eo pipefail

[ "$(id -u)" -eq 0 ] || { echo "run with sudo (need root)."; exit 1; }

FW_DIR=${FW_DIR:-/lib/firmware/brcm}
BACKUP_DIR=${FW_BACKUP_DIR:-"$(cd "$(dirname "$0")/.." && pwd)/firmware_backup"}
[ -d "$FW_DIR" ] || { echo "firmware dir $FW_DIR not found"; exit 1; }

restored=0

# 1) our pre-patch snapshot
if ls "$BACKUP_DIR"/brcmfmac43455-sdio.* >/dev/null 2>&1; then
    echo "[restore-fw] restoring from snapshot $BACKUP_DIR ..."
    cp -a "$BACKUP_DIR"/brcmfmac43455-sdio.* "$FW_DIR"/
    restored=1
fi

# 2) nexmon's own .orig backups (some versions create them)
if [ "$restored" = 0 ] && ls "$FW_DIR"/brcmfmac43455-sdio.*.orig >/dev/null 2>&1; then
    echo "[restore-fw] restoring from nexmon *.orig backups ..."
    for f in "$FW_DIR"/brcmfmac43455-sdio.*.orig; do cp -a "$f" "${f%.orig}"; done
    restored=1
fi

# 3) offline extract from a cached firmware .deb
if [ "$restored" = 0 ]; then
    deb=$(ls -t /var/cache/apt/archives/firmware-brcm80211_*.deb \
                /var/cache/apt/archives/linux-firmware_*.deb 2>/dev/null | head -1 || true)
    if [ -n "${deb:-}" ]; then
        echo "[restore-fw] extracting stock firmware offline from $(basename "$deb") ..."
        tmp=$(mktemp -d)
        dpkg-deb -x "$deb" "$tmp"
        find "$tmp" -path '*brcm/brcmfmac43455-sdio.*' -exec cp -a {} "$FW_DIR"/ \;
        rm -rf "$tmp"
        restored=1
    fi
fi

if [ "$restored" = 0 ]; then
    echo "[restore-fw] ERROR: no offline source of stock firmware found."
    echo "  - no snapshot in $BACKUP_DIR (run backup_firmware.sh BEFORE nexmon next time)"
    echo "  - no *.orig backups in $FW_DIR"
    echo "  - no cached firmware .deb in /var/cache/apt/archives"
    echo "  Online fallback: sudo apt install --reinstall firmware-brcm80211   (Ubuntu: linux-firmware)"
    exit 1
fi

# hand wlan0 back to NetworkManager
command -v nmcli >/dev/null 2>&1 && nmcli dev set wlan0 managed yes 2>/dev/null || true

echo "[restore-fw] stock firmware restored to $FW_DIR."
if [ -t 0 ]; then
    read -rp "Reboot now to load it? [Y/n] " a || true
    case "${a:-Y}" in [Nn]*) echo "Reboot later: sudo reboot";; *) reboot;; esac
else
    echo "[restore-fw] reboot to finish:  sudo reboot"
fi
