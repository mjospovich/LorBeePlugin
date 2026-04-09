# Documentation index

All docs for **LorBeePlugin** live in this folder.

| Document | Description |
|----------|-------------|
| [configuration-map.md](configuration-map.md) | **Where to edit what** — `.env`, Zigbee2MQTT, Node-RED, LoRa, MQTT. |
| [architecture.md](architecture.md) | Edge vs central, **[`assets/architecture.png`](../assets/architecture.png)**, data flow, ChirpStack one-device-per-Pi. |
| [dietpi-spi.md](dietpi-spi.md) | Enable **SPI** on **DietPi** for the LoRa module (`dietpi-config`). |
| [docker-deployment.md](docker-deployment.md) | **DietPi** + Docker on the host; not DietPi-in-a-container; planned LorBeeOS image. |
| [lorbee-data.md](lorbee-data.md) | **LorBeeOS** data: snapshot file, MQTT topics, hotplug vs LoRa, ChirpStack steps. |
| [lorbeeos-paths.md](lorbeeos-paths.md) | **LorBeeOS** env vars (`LORBEE_STACK_ROOT`, `LORBEE_SNAPSHOT_HOST`) and OS-style paths. |
| [mosquitto.md](mosquitto.md) | MQTT broker: role, config files, ports. |
| [zigbee2mqtt.md](zigbee2mqtt.md) | Zigbee coordinator bridge: serial device, topics, UI. |
| [pi-cold-boot.md](pi-cold-boot.md) | Pi reboot: install boot timer + `.env` (operator checklist). |
| [nodered.md](nodered.md) | Flow runtime, sensor snapshot (MQTT / file / HTTP), schema. |
| [../lora/README.md](../lora/README.md) | Optional LoRaWAN OTAA node (ChirpStack), Docker profile `lora`. |

Start with **architecture** (and **`assets/architecture.png`**), then the service you are configuring.

## Port summary (host network)

| Port | Service | Purpose |
|------|---------|---------|
| **1883** | Mosquitto | MQTT |
| **1880** | Node-RED | Editor + `GET /api/lorbee/v1/sensors` (and legacy `/api/sensors`) |
| **8080** | Zigbee2MQTT | Web UI |

Hostname in examples is often `rasp-mk` or the Pi’s LAN IP—replace with yours.
