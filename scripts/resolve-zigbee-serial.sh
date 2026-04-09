#!/usr/bin/env bash
# Print the host path to the Zigbee coordinator for Docker device mapping.
# - If ZIGBEE_SERIAL_DEVICE exists on disk, print it (highest priority).
# - Else scan /dev/serial/by-id for known coordinator USB names (Sonoff, TI, ConBee, …).
# Stdout: one path only. Stderr: hints and errors. Exit 1 = none found, 2 = ambiguous.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
if [[ -f .env ]]; then
  set -a
  # shellcheck disable=SC1091
  source .env
  set +a
fi

EXPLICIT="${ZIGBEE_SERIAL_DEVICE:-}"

if [[ -n "$EXPLICIT" && -e "$EXPLICIT" ]]; then
  echo "$EXPLICIT"
  exit 0
fi

if [[ -n "$EXPLICIT" && ! -e "$EXPLICIT" ]]; then
  echo "lorbee: ZIGBEE_SERIAL_DEVICE=$EXPLICIT missing; trying auto-detect …" >&2
fi

BYID="/dev/serial/by-id"
[[ -d "$BYID" ]] || {
  echo "lorbee: $BYID not found" >&2
  exit 1
}

matches=()
append_unique() {
  local p="$1" x
  for x in "${matches[@]}"; do [[ "$x" == "$p" ]] && return; done
  matches+=("$p")
}
shopt -s nullglob
for p in "$BYID"/usb-* "$BYID"/ttyUSB-*; do
  [[ -e "$p" ]] || continue
  base="$(basename "$p")"
  case "$base" in
  *ITead_Sonoff* | *Texas_Instruments* | *ConBee* | *conbee* | *dresden* | *Nabu_Casa* | *slzb* | *SLZB* | *Electrolama* | *zzh*)
    append_unique "$p"
    ;;
  *Zigbee* | *zigbee*)
    append_unique "$p"
    ;;
  esac
done

if ((${#matches[@]} == 0)); then
  echo "lorbee: no known Zigbee coordinator under $BYID (plug in dongle or set ZIGBEE_SERIAL_DEVICE)" >&2
  exit 1
fi

if ((${#matches[@]} > 1)); then
  echo "lorbee: multiple Zigbee-like serial devices; set ZIGBEE_SERIAL_DEVICE in .env to one of:" >&2
  printf '  %s\n' "${matches[@]}" >&2
  exit 2
fi

echo "${matches[0]}"
