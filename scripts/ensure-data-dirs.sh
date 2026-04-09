#!/usr/bin/env bash
# LorBeeOS stack: ensure the host path for merged sensor JSON exists (see LORBEE_SNAPSHOT_HOST).
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
echo "LorBeeOS stack: snapshot directory ready at $SNAP"

LORA_STATE="$ROOT/data/lorawan-state"
mkdir -p "$LORA_STATE"
chmod 755 "$LORA_STATE" 2>/dev/null || true
echo "LorBeeOS stack: LoRaWAN session dir ready at $LORA_STATE"
