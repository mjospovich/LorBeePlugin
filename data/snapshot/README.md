# Runtime sensor snapshot (host)

This directory holds the **canonical merged Zigbee snapshot** written by Node-RED:

- **`latest.json`** — full edge state (IEEE keys). Runtime only; not committed to git.

**Host path** is configurable: set **`LORBEE_SNAPSHOT_HOST`** in `.env` (default `./data/snapshot` relative to the stack). On **LorBeeOS** images this is often **`/var/lib/lorbee/snapshot`**. See [docs/lorbeeos-paths.md](../docs/lorbeeos-paths.md).

Inside containers the mount is always **`/data/snapshot/`**.

Run **`make init`** after clone so the host directory exists.

## Optional alarm thresholds (for LoRa packed payload)

`chirpstack-node` can set a compact per-entry alarm flag in packed uplinks.
The alarm bit is raised when any configured threshold for that entry is triggered.

You can manage thresholds from Node-RED by writing one of these optional objects
into `latest.json`:

1. Root map (recommended): `alarm_thresholds.<device_key>.<field>`
2. Root map under `alarms`: `alarms.thresholds.<device_key>.<field>`
3. Per-device map: `devices.<device_key>.alarm_thresholds.<field>`

`<device_key>` must match your LoRa payload entry `device` key (IEEE or alias like `sps30`).

Rule forms:
- Number: shorthand for `gt` (alarm when value > number)
- Object: any of `gt`, `gte`, `lt`, `lte`, `eq`, `neq`
- Boolean/String: shorthand for `eq`

Example:

```json
{
  "devices": {
    "sps30": {
      "pm2_5": 8.5,
      "aqi": 35
    }
  },
  "alarm_thresholds": {
    "sps30": {
      "aqi": { "gte": 50 },
      "pm2_5": { "gt": 12.0 }
    },
    "0x0ceff6fffeef6b90": {
      "occupancy": true,
      "illumination": "bright"
    }
  }
}
```

Field aliases are accepted (for example `temperature` / `temperature_c`, `humidity` / `humidity_pct`, `pm2_5` / `pm2_5_ugm3`).

### Recommended persistent config file

To keep alarm rules stable across runtime updates, store them in:

- `/data/snapshot/alarm-thresholds.json` (host path: `data/snapshot/alarm-thresholds.json`)

The Node-RED snapshot flow reads this file and injects it into `latest.json` as `alarm_thresholds` on each snapshot write.
So you only edit one file for thresholds.
