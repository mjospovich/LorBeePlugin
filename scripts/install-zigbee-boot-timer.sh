#!/usr/bin/env bash
# Install systemd timer: after Pi boot, wait for Zigbee USB and restart Zigbee2MQTT (optional LoRa up).
# Run from anywhere:   sudo bash /path/to/LorBeePlugin/scripts/install-zigbee-boot-timer.sh
# Or from repo root:   sudo bash scripts/install-zigbee-boot-timer.sh
# Idempotent: safe to re-run after moving the repo (updates paths in the unit).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SVC_SRC="$ROOT/deploy/systemd/lorbee-zigbee-after-boot.service"
TMR_SRC="$ROOT/deploy/systemd/lorbee-zigbee-after-boot.timer"

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  sed -n '1,12p' "$0"
  exit 0
fi

if [[ $EUID -ne 0 ]]; then
  echo "lorbee: need root — run: sudo bash $0" >&2
  exit 1
fi

if [[ ! -f "$SVC_SRC" || ! -f "$TMR_SRC" ]]; then
  echo "lorbee: missing unit files under $ROOT/deploy/systemd/" >&2
  exit 1
fi

echo "lorbee: stack root = $ROOT"
install -m 0644 "$SVC_SRC" "$TMR_SRC" /etc/systemd/system/
sed -i "s|STACK_ROOT_PLACEHOLDER|$ROOT|g" /etc/systemd/system/lorbee-zigbee-after-boot.service

# systemd ExecStart execve() requires +x on the script (Makefile/docs use `bash script` so this is easy to miss).
chmod +x "$ROOT/scripts/stack-after-boot.sh"

systemctl daemon-reload
systemctl enable --now lorbee-zigbee-after-boot.timer

echo ""
echo "lorbee: timer enabled. Each boot it runs scripts/stack-after-boot.sh:"
echo "        wait for Zigbee USB → up mosquitto zigbee2mqtt nodered → optional restart zigbee2mqtt → optional LoRa"
echo "  systemctl status lorbee-zigbee-after-boot.timer"
echo "  journalctl -u lorbee-zigbee-after-boot.service -n 40 --no-pager"
echo ""
echo "lorbee: ensure .env in $ROOT has coordinator + optional wait/settle (see .env.example)."
echo "        For LoRa after Zigbee: COMPOSE_PROFILES=lora and/or LORBEE_LORA_ON_BOOT=1"
echo ""
echo "lorbee: full checklist: docs/pi-cold-boot.md"
