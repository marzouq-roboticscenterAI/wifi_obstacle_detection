#!/usr/bin/env bash
# backup_firmware.sh -- snapshot the STOCK BCM43455 WiFi firmware so it can be
# restored OFFLINE later (after Nexmon patches it). Run with sudo, BEFORE
# installing Nexmon -- once the patched firmware is in place, a snapshot would
# just capture the patched build.
#
# install_nexmon_part2.sh calls this automatically right before flashing.
#
#   sudo ./scripts/backup_firmware.sh           # snapshot to ./firmware_backup
#   FW_BACKUP_DIR=/mnt/usb/fw FORCE=1 sudo ./scripts/backup_firmware.sh
set -eo pipefail

[ "$(id -u)" -eq 0 ] || { echo "run with sudo (need root)."; exit 1; }

FW_DIR=${FW_DIR:-/lib/firmware/brcm}
BACKUP_DIR=${FW_BACKUP_DIR:-"$(cd "$(dirname "$0")/.." && pwd)/firmware_backup"}
FORCE=${FORCE:-0}

[ -d "$FW_DIR" ] || { echo "firmware dir $FW_DIR not found"; exit 1; }

# Don't clobber an existing (pre-patch) backup unless forced.
if ls "$BACKUP_DIR"/brcmfmac43455-sdio.* >/dev/null 2>&1 && [ "$FORCE" != 1 ]; then
    echo "[backup-fw] backup already exists in $BACKUP_DIR (FORCE=1 to overwrite). Keeping it."
    exit 0
fi

# Heuristic guard: warn if the firmware already looks Nexmon-patched.
if grep -aqi 'nexmon' "$FW_DIR"/brcmfmac43455-sdio.bin 2>/dev/null; then
    echo "[backup-fw] WARNING: $FW_DIR/brcmfmac43455-sdio.bin appears to be Nexmon-patched"
    echo "            already -- this snapshot would NOT be the stock firmware."
    [ "$FORCE" = 1 ] || { echo "            aborting (FORCE=1 to snapshot anyway)."; exit 1; }
fi

mkdir -p "$BACKUP_DIR"
echo "[backup-fw] copying $FW_DIR/brcmfmac43455-sdio.* -> $BACKUP_DIR"
cp -a "$FW_DIR"/brcmfmac43455-sdio.* "$BACKUP_DIR"/ 2>/dev/null \
    || { echo "[backup-fw] no brcmfmac43455-sdio.* files found in $FW_DIR"; exit 1; }
ls -l "$BACKUP_DIR"
echo "[backup-fw] done. Restore later (offline) with: sudo ./scripts/restore_firmware.sh"
