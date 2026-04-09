# LorBee edge data layout and ChirpStack workflow

This document is the **single entry point** for developers: where merged Zigbee data lives, how MQTT topics are named, and what you must do when you add a sensor so the **central ChirpStack** (main PC / RPi5 — not part of this repo) can still decode uplinks. **System context:** **[architecture.md](architecture.md)** and **[`../assets/architecture.png`](../assets/architecture.png)**.

## What is hotplug vs what needs a config step

| Layer | New Zigbee device joined in Zigbee2MQTT | Action |
|-------|----------------------------------------|--------|
| **MQTT** (`zigbee2mqtt/...`) | Appears automatically | None |
| **Merged snapshot** (file + HTTP + `lorbee/...` MQTT) | Device shows up under `devices` keyed by **IEEE** | None |
| **LoRa `legacy` payload** (4-byte temp/hum) | Only if that device is chosen by `device_ieee` or “first with temp+hum” | May need **`snapshot.device_ieee`** in `lora/chirpstack-node/config.yaml` |
| **LoRa `packed` payload** | Bytes on air are defined by **`payload.entries`** | Add a row: **`device`** (IEEE from snapshot), **`fields`**, **`id`**; then update **ChirpStack codec** to match order and ids |

So: **the edge always “sees” new sensors immediately**. **Over LoRa**, the air format is finite; adding a new sensor to the radio payload is a **deliberate** step (config + codec), documented below.

## Canonical paths (host)

| Path | Content |
|------|---------|
| **Merged JSON** (`latest.json`) | Default host path **`./data/snapshot/latest.json`** (relative to the stack). Override with **`LORBEE_SNAPSHOT_HOST`** in `.env`. On a **LorBeeOS** SD image this is often **`/var/lib/lorbee/snapshot/latest.json`**. |
| **`config/lorbee/payload.manifest.example.yaml`** | Template describing **packed** LoRa layout; keep in sync with `lora/chirpstack-node/config.yaml` and ChirpStack. |
| **`nodered/data/`** | Node-RED user dir (flows, credentials). |

Create the snapshot directory: `make init` (see [README.md](../README.md)). Full path layout: **[lorbeeos-paths.md](lorbeeos-paths.md)**.

If you still have an old file at `nodered/data/snapshot/latest.json`, move it once:  
`mv nodered/data/snapshot/latest.json data/snapshot/` (then restart the `nodered` container).

## MQTT topics (retained snapshot)

| Topic | Meaning |
|-------|---------|
| **`lorbee/edges/<EDGE_ID>/sensors/latest`** | Primary retained merged JSON. `<EDGE_ID>` comes from `.env` **`EDGE_ID`** (default `default`). Use a **unique** value per Raspberry Pi if many edges share one broker. |
| **`edge/sensors/latest`** | Legacy alias (same payload). Prefer the `lorbee/...` topic for new integrations. |

Broker: Mosquitto on **`localhost:1883`** (host network from containers).

## HTTP

| URL | Notes |
|-----|--------|
| `http://<pi>:1880/api/lorbee/v1/sensors` | **Preferred** merged JSON API |
| `http://<pi>:1880/api/sensors` | Legacy alias |

## Snapshot JSON (conceptual)

| Field | Meaning |
|-------|---------|
| `schema_version` | **2** — IEEE-canonical `devices` keys |
| `source` | `lorbee.edge-snapshot` |
| `edge_id` | From **`EDGE_ID`** env (sanitized) |
| `updated_at` | ISO-8601 |
| `device_labels` | IEEE → Zigbee2MQTT friendly name |
| `devices` | Per-radio state (Zigbee2MQTT payload shapes) |

## Adding a new sensor for ChirpStack (packed mode)

1. Pair the device in Zigbee2MQTT and wait for state on MQTT.
2. Open **`data/snapshot/latest.json`** (or `GET /api/lorbee/v1/sensors`) and copy the device’s **IEEE** key (e.g. `0x00158d0001234567`).
3. Edit **`lora/chirpstack-node/config.yaml`**: under **`payload.entries`**, append an entry with that IEEE, ordered **`fields`** supported by the encoder (`temperature`, `humidity`, `occupancy`, `motion`, `illumination`, `brightness`, `linkquality`, `battery`, `voltage` — see [lora/README.md](../lora/README.md)).
4. Mirror the same plan in **`config/lorbee/payload.manifest.example.yaml`** (or your own copy) so the repo documents what is on-air.
5. On ChirpStack (main PC), update the **device profile codec**: same **`id`** bytes and **field order** as in YAML. The sample codec in [lora/README.md](../lora/README.md) shows the pattern (`PLAN` / `LABELS` must match your entries).
6. Restart the LoRa container: `docker compose --profile lora up -d chirpstack-lora-node` (or your equivalent).

If the payload would exceed **`max_bytes`** or regional limits, remove entries, shorten fields, or send less often — the node logs and skips oversize builds.

## Related docs

- [architecture.md](architecture.md) — full pipeline diagram  
- [nodered.md](nodered.md) — flow behavior  
- [lora/README.md](../lora/README.md) — OTAA, binary layout, codecs  
