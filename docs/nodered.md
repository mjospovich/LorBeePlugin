# Node-RED (LorBeePlugin stack)

**Compose service:** `nodered`  
**Image:** `nodered/node-red:latest`  
**Network:** `host` — editor and HTTP-in nodes on **1880**

## What it does

[Node-RED](https://nodered.org/) runs a single flow tab: subscribe to Zigbee2MQTT topics, merge device payloads into one JSON object, and expose it via **MQTT (retained)**, **file** under the repo’s **`data/snapshot/`** directory on the host, and **HTTP**. See **[lorbee-data.md](lorbee-data.md)** for paths, topic names, and ChirpStack workflow.

## Files in this repo

| Path | Purpose |
|------|---------|
| `nodered/data/flows.json` | Flow definitions: **Sensor snapshot** tab + Mosquitto broker config. |
| `nodered/data/settings.js` | Runtime settings (port 1880, user dir `/data`, etc.). |
| `nodered/data/package.json` | Optional extra Node-RED nodes (npm); currently empty. |
| `data/snapshot/latest.json` (repo root) | **Runtime** — last merged snapshot (gitignored); mounted at `/data/snapshot/latest.json` in the container. |

`flows_cred.json` (if present) stores encrypted credentials and is gitignored.

## User ID in Docker

The service runs as **`NODE_RED_USER`** (`uid:gid`, default `1000:1000` in [`.env.example`](../.env.example)). It must match ownership of `./nodered/data` and be able to write **`./data/snapshot/`** on the host (run `make init` after clone; fix `chown` if uploads fail).

**`EDGE_ID`** (`.env`) is passed into the container; it appears in the snapshot as **`edge_id`** and in the primary MQTT topic `lorbee/edges/<EDGE_ID>/sensors/latest`.

## Sensor snapshot (merged JSON)

| Output | Description |
|--------|-------------|
| MQTT | **`lorbee/edges/<EDGE_ID>/sensors/latest`** (primary, **retained**). Legacy alias: **`edge/sensors/latest`**. |
| File | `/data/snapshot/latest.json` in the container → **`data/snapshot/latest.json`** at repo root on the host. |
| HTTP | **`GET /api/lorbee/v1/sensors`** — same JSON; legacy **`GET /api/sensors`**. `Cache-Control: no-cache`, `Access-Control-Allow-Origin: *`. |

Merge logic: subscribe to **`zigbee2mqtt/#`** (skip **`zigbee2mqtt/bridge/*`** except a dedicated **`zigbee2mqtt/bridge/devices`** input), build **`z2m_friendly_to_ieee`** from the bridge list, and store each device under a **canonical lowercase IEEE** key when known (drops duplicate friendly-name / IEEE keys for the same radio). Global context key: **`sensor_snapshot`**.

### Snapshot schema (conceptual)

| Field | Meaning |
|-------|---------|
| `schema_version` | **2** — IEEE-canonical `devices` keys when bridge map is available. |
| `source` | `lorbee.edge-snapshot` |
| `edge_id` | From **`EDGE_ID`** in `.env` (sanitized); identifies this Pi when many edges share infrastructure. |
| `updated_at` | ISO-8601 timestamp of last merge. |
| `device_labels` | Map **IEEE → friendly name** from `bridge/devices` (same names as in Zigbee2MQTT UI). |
| `devices` | Object keyed by Zigbee IEEE (`0x…`) when known; values are state payloads plus optional **`friendly_name`** (duplicate of the label for convenience). |

## Dependencies

- **Mosquitto** (`depends_on`).

## Operations

```bash
docker compose logs -f nodered
```

Editor: `http://<pi-ip>:1880`.

## Troubleshooting

### `EACCES: permission denied` writing `/data/snapshot/latest.json`

The container user **`NODE_RED_USER`** must match the **numeric** owner of `./nodered/data` on the host (bind mounts use host uid/gid). If your login is not uid 1000, set `NODE_RED_USER` from `id -u` and `id -g`, then recreate the container:

```bash
grep NODE_RED_USER .env
id -u && id -g
docker compose up -d --force-recreate nodered
```

### `Failed to parse JSON string` on `zigbee2mqtt/#`

Some topics under `zigbee2mqtt/#` are not JSON. The flow uses MQTT-in **datatype utf8** and parses in the function node so non-JSON messages are dropped quietly instead of erroring at the input.

## Local routes

| URL | Purpose |
|-----|---------|
| `http://<pi>:1880` | Flow editor |
| `http://<pi>:1880/api/lorbee/v1/sensors` | Merged JSON snapshot (canonical) |
| `http://<pi>:1880/api/sensors` | Same JSON (legacy path) |
