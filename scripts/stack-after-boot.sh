#!/usr/bin/env bash
# Pi cold boot: wait for Zigbee USB, start the default stack (Mosquitto + Zigbee2MQTT + Node-RED),
# optional delayed zigbee2mqtt restart, then optional LoRa. Used by systemd timer and make targets.
#
# We use explicit service names for the first `up` so COMPOSE_PROFILES=lora in .env does not
# start chirpstack-lora-node before Zigbee serial is exported.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
if [[ -f .env ]]; then
  set -a
  # shellcheck disable=SC1091
  source .env
  set +a
fi
export ZIGBEE_SERIAL_DEVICE="$(bash "$ROOT/scripts/wait-for-zigbee-serial.sh")"
sleep "${ZIGBEE_SERIAL_SETTLE_SEC:-5}"

echo "lorbee: docker compose up -d mosquitto zigbee2mqtt nodered"
docker compose up -d mosquitto zigbee2mqtt nodered

# Default: no second restart. Bouncing the coordinator right after boot (SIGTERM during init) correlates with
# Winston "write after end", confused Z-Stack bring-up, and slow recovery for battery end devices after any outage.
# USB wait + settle above is the main fix. Enable only if a specific stick needs it: ZIGBEE2MQTT_POST_UP_RESTART=1
if [[ "${ZIGBEE2MQTT_POST_UP_RESTART:-0}" == "1" ]]; then
  echo "lorbee: waiting ${ZIGBEE2MQTT_PRE_RESTART_SLEEP_SEC:-45}s before zigbee2mqtt restart"
  sleep "${ZIGBEE2MQTT_PRE_RESTART_SLEEP_SEC:-45}"
  echo "lorbee: restarting zigbee2mqtt"
  if docker compose restart zigbee2mqtt 2>/dev/null; then
    :
  else
    docker compose up -d zigbee2mqtt
  fi
else
  echo "lorbee: skipping post-up zigbee2mqtt restart (set ZIGBEE2MQTT_POST_UP_RESTART=1 to enable)"
fi

if [[ "${COMPOSE_PROFILES:-}" == *sps30* ]] || [[ "${LORBEE_SPS30_ON_BOOT:-0}" == "1" ]]; then
  echo "lorbee: docker compose --profile sps30 up -d sps30-collector"
  docker compose --profile sps30 up -d sps30-collector || true
fi

if [[ "${COMPOSE_PROFILES:-}" == *lora* ]] || [[ "${LORBEE_LORA_ON_BOOT:-0}" == "1" ]]; then
  echo "lorbee: docker compose --profile lora up -d chirpstack-lora-node"
  docker compose --profile lora up -d chirpstack-lora-node || true
fi
