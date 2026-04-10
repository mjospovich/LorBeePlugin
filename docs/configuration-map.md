# Configuration map

Where to change behaviour for **LorBeePlugin** (this Docker Compose stack). For install order, see the root **README.md**.

## Quick reference

| Goal | Primary location | Notes |
|------|------------------|--------|
| Host paths, timezone, edge id, Zigbee **host** serial path | **`.env`** (create via **`.env.example`**) | Run **`bash scripts/setup-first-run.sh`** to set common keys. |
| Zigbee **channel** (11–26), adapter, MQTT URL, frontend, `adapter_delay` | **`zigbee2mqtt/data/configuration.yaml`** | **`advanced.channel`**: use a different channel per nearby coordinator (see **`docs/zigbee2mqtt.md`**). Setup wizard sets channel/adapter; **`--yes`**: optional **`LORBEE_ZIGBEE_CHANNEL`** / **`LORBEE_ZIGBEE_ADAPTER`** in **`.env.example`**. Host serial path: **`.env`** → **`ZIGBEE_SERIAL_DEVICE`**; container port **`/dev/ttyUSB0`**. |
| Merged sensor snapshot → file / MQTT / HTTP API | **Node-RED** flows in **`nodered/data/flows.json`** | See **`docs/nodered.md`**. |
| MQTT broker listen / persistence | **`mosquitto/config/mosquitto.conf`** | See **`docs/mosquitto.md`**. |
| SPI enabled on the Pi (LoRa) | **Host firmware** | **DietPi:** **`docs/dietpi-spi.md`** (`dietpi-config`). Then **`ls /dev/spidev0.*`**. |
| LoRaWAN: pins, region, uplink interval (easy path) | **`lora/chirpstack-node/keys.env`** | **`RFM_*`**, **`LORAWAN_REGION`**, etc. override **`config.yaml`** at runtime. **`bash scripts/setup-first-run.sh`** → LoRa prompts. **One ChirpStack device per Pi** — see **`lora/README.md`**. |
| LoRaWAN: payload shape, snapshot mapping | **`lora/chirpstack-node/config.yaml`** | Optional **Compose profile `lora`**. See **`lora/README.md`**. |
| LoRaWAN: DevEUI / AppKey (secrets) | **`lora/chirpstack-node/keys.env`** | Same file as pin overrides; copy from **`keys.env.example`** if missing. |
| Packed LoRa layout documentation (air interface contract) | **`config/lorbee/payload.manifest.example.yaml`** | Align with **`config.yaml`** and your ChirpStack codec. See **`config/lorbee/README.md`**. |
| Cold boot: wait for USB, optional LoRa on boot | **`.env`** (`ZIGBEE_SERIAL_WAIT_SEC`, `COMPOSE_PROFILES`, …) + **systemd** timer | **`docs/pi-cold-boot.md`**, **`scripts/install-zigbee-boot-timer.sh`** |

## Stack layout (repo root)

| Path | Role |
|------|------|
| **`docker-compose.yml`** | Service definitions (Mosquitto, Zigbee2MQTT, Node-RED, optional LoRa). |
| **`Makefile`** | Optional shortcuts for the same **`scripts/*.sh`** targets (**`make`** is often not installed on minimal DietPi — use **`bash scripts/…`** from the root **README**). |
| **`scripts/setup-first-run.sh`** | **`.env`**, Zigbee **`configuration.yaml`** (channel, adapter), Zigbee2MQTT template, optional LoRa **`keys.env`**, data dirs. |
| **`scripts/stack-after-boot.sh`** | Used by **`make up-safe`** and systemd: wait for serial → **`docker compose up`**. |
| **`scripts/resolve-zigbee-serial.sh`** | Resolves **`ZIGBEE_SERIAL_DEVICE`** from **`.env`** or **`/dev/serial/by-id`**. |
| **`data/snapshot/`** | Host directory for **`latest.json`** (merged sensors); mounted into Node-RED and optional LoRa. |

## Runtime data (do not commit)

Gitignored or sensitive: **`.env`**, **`zigbee2mqtt/data/configuration.yaml`** and DB/state, **`nodered/data/flows_cred.json`** and **`.config.*.json`** (editor/runtime secrets), **`nodered/data/*.backup`**, **`mosquitto/data/*`** (except **`.gitkeep`**), **`data/snapshot/*.json`**, **`lora/chirpstack-node/keys.env`**, **`data/lorawan-state/*`** (except **`.gitkeep`**).

## Related: LorBeeOS

**LorBeeOS** (separate project) is a flashable image that embeds this stack with fixed OS paths. When running **LorBeePlugin** alone on DietPi, you only need **`docs/lorbeeos-paths.md`** if you mirror that layout (e.g. **`LORBEE_SNAPSHOT_HOST`** / **`LORBEE_STACK_ROOT`**).
