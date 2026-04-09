# LorBeePlugin — Docker deployment

This stack runs on a **real Raspberry Pi** with **Docker Engine** and **Docker Compose v2** (the **`docker compose`** CLI plugin). The **reference host OS is [DietPi](https://dietpi.com/)**. You can use **Raspberry Pi OS Lite** or another Debian derivative; the Compose file does not depend on DietPi-specific features.

**Compose check:** after installing Docker, run **`docker compose version`**. If you get **`unknown command: docker compose`**, install **`docker-compose-plugin`** (see the root **README** → *Docker + Compose*). This repo does not use a wrapper script — fix the host once.

## DietPi is not pulled as a Docker image

**We do not run DietPi inside a container.**

1. Flash **DietPi** (or another OS) to the SD card and boot the Pi. **LorBeeOS** (separate project) will eventually provide a ready-made image for Pi Zero 2W; DIY installs use DietPi + this repo.
2. Install **Docker** on the host (DietPi: `dietpi-software` → Docker, or [DietPi Docker docs](https://dietpi.com/docs/software/programming/#docker-compose)).
3. **Clone this stack**, run **`bash scripts/setup-first-run.sh`** (or **`make setup-first-run`** if **`make`** is installed), then start with **`bash scripts/stack-after-boot.sh`** (recommended; same as **`make up-safe`**) or **`docker compose up -d`** if **`.env`** already has a correct **`ZIGBEE_SERIAL_DEVICE`**. On a Pi that **powers on unattended**, see **[zigbee2mqtt.md — Cold boot](zigbee2mqtt.md#cold-boot-docker-starts-before-the-coordinator-is-ready)** for **`stack-after-boot.sh`**, and the **systemd timer** so Zigbee2MQTT starts **after** the USB coordinator is ready.
4. Docker **pulls application images**: Mosquitto, Zigbee2MQTT, Node-RED; optional **LoRa** image is built locally from `lora/chirpstack-node/Dockerfile`.

Those images share the **host kernel** — normal Docker, not a nested full OS.

## Why host networking

Services use **`network_mode: host`** for MQTT on **`localhost:1883`** and USB coordinator passthrough. Zigbee and SPI LoRa use host devices in **privileged** containers.

## LorBeeOS (separate product — in progress)

**LorBeeOS** is a planned **flashable Pi image** (DietPi-based) that will bundle this stack for minimal manual setup. **LorBeePlugin** remains the source of truth for the Compose project and DIY installs.

**Where this stack sits in a full deployment:** see **[architecture.md](architecture.md)** and **[`../assets/architecture.png`](../assets/architecture.png)** (remote edge Pi vs central ChirpStack / gateway).

## Related

- [architecture.md](architecture.md)  
- [dietpi-spi.md](dietpi-spi.md)  
- [lorbeeos-paths.md](lorbeeos-paths.md)  
