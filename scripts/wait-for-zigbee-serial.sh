#!/usr/bin/env bash
# Block until a coordinator serial device exists; print its path on stdout (for export).
# Uses resolve-zigbee-serial.sh (explicit .env path or auto by-id). ZIGBEE_SERIAL_WAIT_SEC max wait.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MAX="${ZIGBEE_SERIAL_WAIT_SEC:-180}"
for ((i = 0; i < MAX; i++)); do
  if DEV="$(bash "$ROOT/scripts/resolve-zigbee-serial.sh" 2>/dev/null)" && [[ -n "$DEV" ]] && [[ -e "$DEV" ]]; then
    printf '%s\n' "$DEV"
    exit 0
  fi
  sleep 1
done
echo "lorbee: timeout ${MAX}s waiting for Zigbee coordinator serial device" >&2
exit 1
