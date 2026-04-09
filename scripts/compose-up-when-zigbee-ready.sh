#!/usr/bin/env bash
# Same as stack-after-boot: wait for USB, up Mosquitto/Zigbee/Node-RED, restart Zigbee, optional LoRa.
exec "$(cd "$(dirname "$0")" && pwd)/stack-after-boot.sh"
