# LorBeeOS — path conventions

**LorBeePlugin** (this repository) is only the **Docker Compose stack**. **LorBeeOS** (separate project, in progress) will be a **DietPi-based Pi Zero 2W image** that **includes** this stack. The **reference DIY host is [DietPi](https://dietpi.com/)** with **Docker on the host** — see **[docker-deployment.md](docker-deployment.md)**.

When LorBeeOS ships, its docs will describe the on-image layout (**`/opt/lorbee`**, **`/var/lib/lorbee`**, **`/etc/lorbee`**). This file bridges **“git clone on the Pi”** and **that** fixed path layout if you mirror it yourself.

## Environment variables (docker compose)

| Variable | Role | Typical dev clone | Typical LorBeeOS image |
|----------|------|-------------------|-------------------------|
| **`LORBEE_STACK_ROOT`** | Directory containing `docker-compose.yml`, `mosquitto/`, `zigbee2mqtt/`, `nodered/`, `lora/` | `.` (default) | `/opt/lorbee/stack` |
| **`LORBEE_SNAPSHOT_HOST`** | Host path merged JSON (`latest.json`); bind-mounted at `/data/snapshot` in containers | `./data/snapshot` | `/var/lib/lorbee/snapshot` |
| **`EDGE_ID`** | Logical edge (MQTT topic + JSON field) | `default` or unique per device | Hostname or provisioning value |
| **`NODE_RED_USER`** | `uid:gid` for Node-RED container | Match owner of `nodered/data` | Dedicated `lorbee` user or `1000:1000` |
| **`ZIGBEE_SERIAL_DEVICE`** | Host device for coordinator | `/dev/serial/by-id/...` | Same |

Set these in **`.env`** next to `docker-compose.yml`. Copy from [`.env.example`](../.env.example).

## Why split stack root and snapshot path?

- **Stack root** holds versioned config, Compose file, LoRa `config.yaml`, Node-RED flows shipped with the image.
- **Snapshot path** is high-churn runtime data shared by Node-RED and the LoRa service; on an OS image it often lives under **`/var/lib/lorbee`** so backups and factory resets can treat “state” separately from “software”.

You can keep **`LORBEE_SNAPSHOT_HOST=${LORBEE_STACK_ROOT}/data/snapshot`** (relative or absolute) if you prefer a single tree.

## In-container paths (unchanged)

| Path in container | Meaning |
|-------------------|---------|
| `/data/snapshot/latest.json` | Merged Zigbee JSON (host path = `LORBEE_SNAPSHOT_HOST`) |
| `/data` (Node-RED) | Flows + Node-RED user files (host = `$LORBEE_STACK_ROOT/nodered/data`) |

## First boot / init

From the stack directory:

```bash
make init   # runs scripts/ensure-data-dirs.sh — creates LORBEE_SNAPSHOT_HOST on the host
```

Ensure the snapshot directory is writable by **`NODE_RED_USER`** (and readable by the LoRa container user if you use profile `lora`).

## Related

- [lorbee-data.md](lorbee-data.md) — MQTT topics, HTTP API, ChirpStack workflow  
- [architecture.md](architecture.md) — pipeline diagram  
