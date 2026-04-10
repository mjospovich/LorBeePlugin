#!/usr/bin/env bash
# LorBee stack: ensure host paths for snapshot, LoRaWAN session state, and Mosquitto data exist.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
if [[ -f .env ]]; then
  set -a
  # shellcheck disable=SC1091
  source .env
  set +a
fi
SNAP="${LORBEE_SNAPSHOT_HOST:-./data/snapshot}"
if [[ "$SNAP" != /* ]]; then SNAP="$ROOT/$SNAP"; fi
mkdir -p "$SNAP"
chmod 755 "$SNAP" 2>/dev/null || true
echo "LorBee stack: snapshot directory ready at $SNAP"

LORA_STATE="$ROOT/data/lorawan-state"
mkdir -p "$LORA_STATE"
chmod 755 "$LORA_STATE" 2>/dev/null || true
echo "LorBee stack: LoRaWAN session dir ready at $LORA_STATE"

MQ_DATA="$ROOT/mosquitto/data"
mkdir -p "$MQ_DATA"
chmod 755 "$MQ_DATA" 2>/dev/null || true
echo "LorBee stack: Mosquitto data directory ready at $MQ_DATA"
