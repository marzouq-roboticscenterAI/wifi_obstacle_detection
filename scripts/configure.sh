#!/usr/bin/env bash
# configure.sh -- regenerate the deployment block in jetson_processor/config.h
# (room geometry, Pi/RX position, and the TX anchor table) without hand-editing C.
#
# Interactive (just run it):
#     ./scripts/configure.sh
# Non-interactive (scriptable) -- pass geometry via env and anchors via -a:
#     ROOM_W=6 ROOM_H=5 RX_X=3 RX_Y=0 \
#     ./scripts/configure.sh \
#         -a TX-NW,aa:bb:cc:dd:ee:01,0,5 \
#         -a TX-NE,aa:bb:cc:dd:ee:02,6,5 \
#         -a TX-W,aa:bb:cc:dd:ee:03,0,2.5 \
#         -a TX-E,aa:bb:cc:dd:ee:04,6,2.5
#
# Anchor format:  NAME,MAC,X,Y      (MAC = aa:bb:cc:dd:ee:ff, X/Y in metres)
# Writes a backup to config.h.bak. Set BUILD=1 to rebuild the processor after.
set -euo pipefail

cd "$(dirname "$0")/.."
CFG="jetson_processor/config.h"
BEGIN_MARK="=== BEGIN GENERATED DEPLOYMENT CONFIG ==="
END_MARK="=== END GENERATED DEPLOYMENT CONFIG ==="

# defaults (overridable via env)
ROOM_W=${ROOM_W:-6.0}; ROOM_H=${ROOM_H:-5.0}
GRID_RES=${GRID_RES:-0.10}
RX_X=${RX_X:-3.0}; RX_Y=${RX_Y:-0.0}
LINK_SIGMA=${LINK_SIGMA:-0.45}
BUILD=${BUILD:-0}

A_NAME=(); A_MAC=(); A_X=(); A_Y=()

# --- helpers (defined before use) --------------------------------------------
validate_mac() {  # aa:bb:cc:dd:ee:ff
    echo "$1" | grep -qiE '^([0-9a-f]{2}[:-]){5}[0-9a-f]{2}$' \
        || { echo "invalid MAC: $1"; exit 1; }
}
mac_to_c() { echo "$1" | awk -F'[:-]' \
    '{printf "{0x%s,0x%s,0x%s,0x%s,0x%s,0x%s}",$1,$2,$3,$4,$5,$6}'; }
flt() { case "$1" in *.*) printf '%sf' "$1";; *) printf '%s.0f' "$1";; esac; }
add_anchor() {  # NAME,MAC,X,Y
    local IFS=','; read -r nm mac x y <<<"$1"
    [ -n "${nm:-}" ] && [ -n "${mac:-}" ] && [ -n "${x:-}" ] && [ -n "${y:-}" ] \
        || { echo "bad -a '$1' (need NAME,MAC,X,Y)"; exit 1; }
    validate_mac "$mac"
    A_NAME+=("$nm"); A_MAC+=("$mac"); A_X+=("$x"); A_Y+=("$y")
}

# --- parse -a anchor args ----------------------------------------------------
ARGS_GIVEN=0
while getopts "a:h" opt; do
    case "$opt" in
        a) add_anchor "$OPTARG"; ARGS_GIVEN=1 ;;
        *) grep -E '^#( |$)' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    esac
done

# --- interactive prompts if no -a args ---------------------------------------
if [ "$ARGS_GIVEN" = 0 ]; then
    ask() { local p=$1 d=$2 v; read -rp "$p [$d]: " v || true; echo "${v:-$d}"; }
    echo "== Room & receiver geometry (metres) =="
    ROOM_W=$(ask "Room width  (x max)" "$ROOM_W")
    ROOM_H=$(ask "Room depth  (y max)" "$ROOM_H")
    GRID_RES=$(ask "Grid resolution"   "$GRID_RES")
    RX_X=$(ask "Pi (RX) x"             "$RX_X")
    RX_Y=$(ask "Pi (RX) y"             "$RX_Y")
    LINK_SIGMA=$(ask "Link path width (sigma)" "$LINK_SIGMA")
    echo
    echo "== TX anchors == (enter blank name to finish; need >=2, ideally 3-4)"
    while :; do
        read -rp "  anchor name (blank to stop): " nm || true
        [ -z "${nm:-}" ] && break
        read -rp "    MAC (aa:bb:cc:dd:ee:ff): " mac; validate_mac "$mac"
        read -rp "    x (m): " x
        read -rp "    y (m): " y
        A_NAME+=("$nm"); A_MAC+=("$mac"); A_X+=("$x"); A_Y+=("$y")
    done
fi

[ "${#A_NAME[@]}" -ge 2 ] || { echo "ERROR: need at least 2 anchors."; exit 1; }

# --- build the generated block -----------------------------------------------
BLOCK="$(mktemp)"
{
    echo "/* Room geometry, receiver (Pi) position, and TX anchors."
    echo " * Regenerate with: scripts/configure.sh */"
    printf '#define ROOM_W_M       %s\n'  "$(flt "$ROOM_W")"
    printf '#define ROOM_H_M       %s\n'  "$(flt "$ROOM_H")"
    printf '#define GRID_RES_M     %s\n'  "$(flt "$GRID_RES")"
    printf '#define RX_X           %s\n'  "$(flt "$RX_X")"
    printf '#define RX_Y           %s\n'  "$(flt "$RX_Y")"
    printf '#define LINK_SIGMA_M   %s\n'  "$(flt "$LINK_SIGMA")"
    echo
    echo "static const anchor_t ANCHORS[] = {"
    for i in "${!A_NAME[@]}"; do
        printf '    { %s, %s, %s, "%s" },\n' \
            "$(mac_to_c "${A_MAC[$i]}")" "$(flt "${A_X[$i]}")" \
            "$(flt "${A_Y[$i]}")" "${A_NAME[$i]}"
    done
    echo "};"
    echo "#define N_ANCHORS ((int)(sizeof(ANCHORS)/sizeof(ANCHORS[0])))"
} > "$BLOCK"

# --- splice into config.h between the markers --------------------------------
grep -q "$BEGIN_MARK" "$CFG" && grep -q "$END_MARK" "$CFG" \
    || { echo "markers not found in $CFG (was it hand-edited away?)"; exit 1; }

cp "$CFG" "$CFG.bak"
awk -v begin="$BEGIN_MARK" -v end="$END_MARK" -v f="$BLOCK" '
    $0 ~ begin { print; while ((getline l < f) > 0) print l; skip=1; next }
    $0 ~ end   { print; skip=0; next }
    !skip      { print }
' "$CFG.bak" > "$CFG"
rm -f "$BLOCK"

echo "[configure] wrote $CFG (backup at $CFG.bak):"
echo "  room ${ROOM_W} x ${ROOM_H} m, RX (${RX_X},${RX_Y}), ${#A_NAME[@]} anchors:"
for i in "${!A_NAME[@]}"; do
    echo "    ${A_NAME[$i]}  ${A_MAC[$i]}  (${A_X[$i]}, ${A_Y[$i]})"
done

if [ "$BUILD" = 1 ]; then
    echo "[configure] rebuilding processor ..."
    make -C jetson_processor
fi
echo "[configure] done. Rebuild the processor to apply (make -C jetson_processor)."
