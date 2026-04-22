# LoRaWAN uplink (ChirpStack) — modular add-on

End-device software for an **Adafruit RFM9x** (SX1276) on the Pi **SPI** bus, speaking **LoRaWAN OTAA** so a **LoRaWAN gateway** (e.g. Sentrius-class or any Semtech-packet-forwarder gateway) forwarding to **ChirpStack** can receive frames.

This folder is part of **LorBeePlugin**, the **remote edge** stack: one Pi per site, optional LoRa to a **central** ChirpStack (and other LoRa sensors on the same gateway network). **Full-system picture:** **[../assets/architecture.png](../assets/architecture.png)** and **[../docs/architecture.md](../docs/architecture.md)**.

The Zigbee/MQTT stack stays unchanged. LoRa runs only when you enable the Compose **profile** `lora`.

## How ChirpStack, the gateway, and this Pi relate

There is **no cable or pairing step** between the LoRa module and the **gateway**. In LoRaWAN:

1. **This Pi** is an **end device**: it transmits encrypted **LoRaWAN** frames over the air (868 MHz in EU868, etc.).
2. The **gateway** is only a **radio bridge**: if it is in range and configured for the same regional band, it receives uplinks and forwards them to **ChirpStack** (usually over IP — Ethernet/Wi‑Fi).
3. **ChirpStack** is the **network + application server**: it verifies the **Join Request** / **MIC**, stores **keys**, and shows **application payloads** in the web UI.

So "linking" means: **same band + in RF range**, and in ChirpStack you register a device whose **DevEUI / JoinEUI / AppKey** match **`keys.env` on this Pi**. The gateway does not need to know your Pi's IP; it only forwards what it hears to ChirpStack.

## Registering this Pi in ChirpStack

**Each Raspberry Pi that runs the LoRa container is a separate LoRaWAN device.** If you have three remote sites, you create **three** devices in ChirpStack and three different **`keys.env`** files (or the same file pattern on three Pis with different values). Do **not** copy one DevEUI/AppKey to every Pi.

1. On the **central** ChirpStack UI (running on your main PC / RPi5 / server — **not** installed by this repo), ensure your **gateway** is added and **online**.
2. Create an **Application** (or pick an existing one).
3. **Add device** → **OTAA**:
   - Generate or enter a unique **DevEUI** (16 hex chars).
   - **JoinEUI** (ex-AppEUI): often **all zeros** (`0000000000000000`) for private setups, or match your ChirpStack defaults.
   - **Application key** (32 hex chars) — ChirpStack generates one; copy it.
4. On **this Pi**, put the same values in **`lora/chirpstack-node/keys.env`** (gitignored):
   - `LORAWAN_DEV_EUI=...`
   - `LORAWAN_JOIN_EUI=...` (if you use zeros, keep the line as in `keys.env.example`)
   - `LORAWAN_APP_KEY=...`
5. Device **profile** in ChirpStack: **LoRaWAN MAC** version **1.0.x** or **1.1** (if 1.0.x, leave `LORAWAN_NWK_KEY` unset in `keys.env`).
6. **Regional parameters** must match **`lorawan.region`** in `config.yaml` / overrides (e.g. **EU868**).
7. Start the LoRa container on the Pi; watch **`docker compose logs chirpstack-lora-node`** for join. If you recreated the device, use ChirpStack's **reset join nonces** once if join is rejected.

Other **non-Zigbee LoRa sensors** on the same gateway are **additional** ChirpStack devices — same idea: one device record per physical radio node, each with its own keys.

Future note: a separate repository may document the **central** side (ChirpStack + Telegraf + InfluxDB + Grafana on the main machine). This repo only covers the **edge** Compose stack.

## Configuration layout (modular)

| File | Purpose |
|------|---------|
| **`chirpstack-node/config.yaml`** | **Defaults** for hardware, region, payload layout — safe to commit. |
| **`chirpstack-node/keys.env`** | **Secrets** (DevEUI, AppKey, …) **and optional hardware overrides** — gitignored; copy from `keys.env.example`. |

Environment variables **override** YAML when set (`RFM_SPI_CHANNEL`, `RFM_PIN_CS`, `RFM_PIN_DIO0`, `RFM_PIN_RST`, `LORAWAN_REGION`, `LORAWAN_SUB_BAND`, `LORAWAN_UPLINK_INTERVAL_SEC`, …).  
**Interactive setup:** from the repo root run **`bash scripts/setup-first-run.sh`** and choose the LoRa section to write those overrides without hand-editing YAML.  
`LORA_CONFIG` points to the YAML file (default in Docker: `/etc/lora/config.yaml`).

## Prerequisites (host)

1. **Enable SPI** on the Raspberry Pi and **reboot**:
   - **DietPi (recommended for LorBee):** **`sudo dietpi-config`** → **Advanced Options** → **SPI state** → **Enable** → reboot. Full walkthrough: **[../docs/dietpi-spi.md](../docs/dietpi-spi.md)**.
   - **Raspberry Pi OS:** **`sudo raspi-config`** → **Interface Options** → **SPI** → **Yes** → reboot.
   - **Manual:** ensure **`dtparam=spi=on`** in the active **`config.txt`** (often **`/boot/firmware/config.txt`** on Bookworm+).
2. Confirm kernel nodes exist:

```bash
ls -l /dev/spidev0.*
```

You should see **`/dev/spidev0.0`** and often **`/dev/spidev0.1`**. The setup wizard asks which index to use; see **[SPI device index](#spi-device-index-spidev0x)** below.
3. **Wiring (BCM numbers):** **MOSI / MISO / SCK** on **SPI0**; **RST** (e.g. **GPIO25**); **G0 / DIO0** on a free GPIO (e.g. **GPIO22**); **CS / NSS** on another free GPIO (e.g. **GPIO17**, physical pin 11) — **not GPIO7 or GPIO8** — see *SPI CS vs Linux* below.

### SPI device index (`spidev0.X`)

The Linux kernel exposes SPI0 as character devices **`/dev/spidev0.0`** and **`/dev/spidev0.1`**. They correspond to the two **hardware** chip-select lines (**CE0** → GPIO8, **CE1** → GPIO7). **This LoRa stack does not use those pins for the RFM9x CS line** — RadioLib **bit-bangs** NSS on a **separate** GPIO you configure (e.g. **17**). You still must pick **which `spidev` node** RadioLib opens:

| `RFM_SPI_CHANNEL` / setup choice | Device node | Typical use |
|----------------------------------|-------------|-------------|
| **0** | `/dev/spidev0.0` | SPI "channel 0" / CE0 side of SPI0 |
| **1** | `/dev/spidev0.1` | SPI "channel 1" / CE1 side (**default** in this repo's `config.yaml` and wizard) |

**How to choose:** If **`ls /dev/spidev0.*`** lists both, **start with `1`** (matches the shipped example and many wiring guides). If you only see **`spidev0.0`**, choose **0**. If the radio fails to initialise (`ERR_CHIP_NOT_FOUND`, SPI errors), try the other index after checking wiring and SPI enable.

### SPI CS vs Linux (important)

On Raspberry Pi, **GPIO7** and **GPIO8** are **hardware chip selects** for SPI0 (`/dev/spidev0.1` and `spidev0.0`). The kernel reserves them. RadioLib uses **lgpio** to bit-bang the CS line you configure; if you set **CS = GPIO7**, you get **`GPIO busy`** / **`Could not claim pin 7`** and **`ERR_CHIP_NOT_FOUND`**.

**Fix:** Wire **CS** to any unused GPIO **except** 7, 8, and pins already used for SPI/MISO/MOSI/SCK, **DIO0**, or **RST** (e.g. **BCM17** / pin 11). Set `hardware.pins.cs` in `config.yaml` to match, restart the container. Keep MOSI/MISO/SCK unchanged.

Adafruit's diagram often shows CS on CE1 for Arduino-style boards; on **Linux + Pi** you need a **separate GPIO** for NSS unless you use a custom device-tree setup.

## Quick start (Docker — recommended for "clone and run")

```bash
cd LorBeePlugin   # or your clone directory name
bash scripts/setup-first-run.sh   # includes optional LoRa pin / region prompts; sets COMPOSE_PROFILES=lora if you agree
# Edit keys.env: LORAWAN_DEV_EUI, LORAWAN_APP_KEY (match ChirpStack).
# Or skip the wizard: cp lora/chirpstack-node/keys.env.example lora/chirpstack-node/keys.env
# and set RFM_* / LORAWAN_* there or in config.yaml.

docker compose --profile lora build chirpstack-lora-node
docker compose --profile lora up -d chirpstack-lora-node
docker compose logs -f chirpstack-lora-node
```

Compose bind-mounts **`lora/chirpstack-node/config.yaml`** → `/etc/lora/config.yaml` and **`./data/snapshot`** (repo root) → **`/data/snapshot`** (read-only) so the node can read **`latest.json`**. Edit YAML on disk, restart the container to apply.

The container needs **hardware access**: `privileged: true` (same idea as Zigbee). Build the image **on the Pi** (or on a machine with the same `GOARCH` you deploy to).

## Host build (no Docker)

On Raspberry Pi OS / DietPi:

```bash
cd lora/chirpstack-node
cp keys.env.example keys.env   # edit keys
# Optional: cp config.example.yaml config.yaml
./scripts/build-on-host.sh
export $(grep -v '^#' keys.env | xargs)
sudo LORA_CONFIG="$(pwd)/config.yaml" ./build/lorawan-node
```

(`sudo` is often required for `/dev/gpiochip0` and SPI.)

## OTAA session persistence (avoid ChirpStack nonce resets on every reboot)

The node saves RadioLib **nonces** and **session** buffers under **`data/lorawan-state/`** on the host (mounted at **`/data/lorawan-state`** in Docker as **`nonces.bin`** + **`session.bin`**). On the next start it **restores** the session, so the device usually **stays joined** and you do **not** need to click **reset join nonces** in ChirpStack after a normal Pi or container restart.

- **Turn off** persistence: set **`LORAWAN_PERSIST_SESSION=0`** on the LoRa service (compose `environment`).
- **Change path**: **`LORAWAN_STATE_DIR`** (default `/data/lorawan-state`).
- **Re-provision** the device in ChirpStack or change **AppKey** / **JoinEUI**: delete **`data/lorawan-state/*`** on the Pi so the next run does a clean join (then you may need **reset join nonces** once on the server).

If logs show **saved session does not match nonces** (RadioLib **`SESSION_DISCARDED`**, was `-1120`), **`nonces.bin`** and **`session.bin`** were out of sync—often a **power cut between writes**. The node **re-joins** automatically; if that happens **every** boot, delete **`data/lorawan-state/*.bin`** once after a clean shutdown.

## `ERR_NO_JOIN_ACCEPT` (logs, no uplink)

The node hears no **Join-Accept** after its Join-Request. Typical causes:

| Check | What to verify |
|-------|----------------|
| Keys | **`keys.env`** `LORAWAN_DEV_EUI` / `LORAWAN_APP_KEY` / `LORAWAN_JOIN_EUI` match the device in ChirpStack (hex, no typos). |
| Join | Device profile / application allows **OTAA join**; not disabled or MIC-blocked. |
| Gateway | Gateway **online** in ChirpStack, same **region** (e.g. EU868), Pi in **RF range**. |
| Nonces | After deleting or recreating the device, use **reset join nonces** (or equivalent) so ChirpStack accepts a fresh join. |

The process **retries join** every **`LORAWAN_JOIN_RETRY_SEC`** seconds (default **60**) instead of exiting, so Docker does not restart in a tight loop while you fix the network.

## Snapshot: one row per radio (friendly name vs IEEE)

Zigbee2MQTT can publish the same device under **`zigbee2mqtt/<friendly_name>`** and sometimes **`zigbee2mqtt/0x…`** (IEEE). After a rename, Node-RED could keep **two** keys under `devices` with divergent copies.

The **Sensor snapshot** flow subscribes to **`zigbee2mqtt/bridge/devices`** (retained list). It builds `friendly_name → ieee_address` and **ieee → friendly_name** and then:

- Stores updates under the **canonical lowercase IEEE** key when known (stable for LoRa `payload.entries` and deduplication).
- Adds **`device_labels`** at the root and **`friendly_name`** inside each device object so you still see **Zigbee2MQTT UI names**, not only raw IDs.
- Removes other `devices` entries that map to the same IEEE.
- On each bridge list refresh, **re-keys** existing `devices` to IEEE and merges duplicates.

Snapshot **`schema_version`** is **2** (still backward compatible for readers that only care about `devices`).

## Sensor snapshot → LoRa payload

The node reads **`snapshot.path`** (default `/data/snapshot/latest.json`). **`payload.format`** selects encoding:

**`device key not in snapshot`:** the IEEE in **`payload.entries`** has no matching key under JSON **`devices`**. Often a **startup race**: the LoRa container joins before **Node-RED** has written **`latest.json`** (Zigbee2MQTT may already publish MQTT seconds earlier). The firmware waits **`LORAWAN_STARTUP_GRACE_SEC`** (default **30 s**) after join before the first uplink, and if an uplink is still skipped it retries after **`LORAWAN_SNAPSHOT_RETRY_SEC`** (default **30 s**, range **5–120**) instead of the full **`uplink_interval_sec`**. Set in project **`.env`** (wired via **`docker-compose.yml`**) or **`keys.env`**. Other causes: stack not fully up, sensor offline, or **wrong IEEE** in `config.yaml`. **`device_labels`** can list the ID while **`devices`** is empty — the packed builder only reads **`devices`**.


### `legacy` (default)

Uses **`temperature`** and **`humidity`** only:

- If **`snapshot.device_ieee`** is set, that key under `devices` is used (IEEE normalization matches lowercase `0x…`).
- Otherwise the **first** device object in the map that has both fields is used.

**Application payload: 4 bytes, big-endian**

| Bytes | Meaning |
|-------|--------|
| 0–1 | `int16` temperature **centidegrees** (°C × 100); missing → **0x7FFF** |
| 2–3 | `uint16` humidity **centipercent** (% × 100); missing → **0xFFFF** |

### `packed` (v4 — self-describing entries)

Set in **`config.yaml`** under **`payload:`** (see comments in `config.yaml` / `config.example.yaml`):

| YAML | Meaning |
|------|--------|
| **`format: packed`** | Use multi-sensor binary layout below. |
| **`entries`** | Ordered list: each **`device`** is a key in `snapshot.devices` (IEEE), **`type`** selects a sensor type from the **Sensor Type Registry** (fields are derived automatically). Optional **`id`**: **0–255** (instance label). Omit **`id` on all entries** → auto **1, 2, 3, …**; **do not** mix explicit and omitted `id` in the same file. |
| **`include_status`** | If **true**, after each entry's fields, append **linkquality** (u8), **battery** (u8, 0–100), **voltage** (u16 BE, mV); **0xFF** / **0xFFFF** if missing. |
| **`max_bytes`** | Build fails (uplink skipped, logged) if longer — tune for your data rate / regional max. |

### Sensor Type Registry

The registry is the **single contract** between every edge encoder and the central ChirpStack decoder. Each sensor type has a **numeric ID** (1 byte on-air) and a **fixed field list**. The decoder uses the type byte to know what data follows — no per-edge `PLAN` or `LABELS` needed.

| Type ID | Name | Fields (in order) | Data bytes |
|---------|------|--------------------|------------|
| **`0x01`** | **`climate`** | `temperature` (int16 BE, °C×100) + `humidity` (uint16 BE, %×100) | 4 |
| **`0x02`** | **`motion`** | `occupancy` (uint8, 0/1/0xFF) + `illumination` (uint8, 0–3) | 2 |
| **`0x03`** | **`contact`** | `contact` (uint8, 0/1/0xFF) | 1 |
| **`0x04`** | **`air_quality`** | `pm1_0` + `pm2_5` + `pm4_0` + `pm10` (uint16 BE each, µg/m³×10) | 8 |
| **`0x05`** | **`air_quality_aqi`** | Same four PM fields as **`0x04`** + **`aqi`** (uint16 BE, US EPA index 0–500 from PM2.5) | 10 |
| **`0x06`** | **`environment`** | `temperature` + `humidity` (same as **`0x01`**) + `pressure_hpa` (uint16 BE, whole hPa 300–1100) + `gas_resistance_ohm` (uint32 BE, raw Ω; `0xFFFFFFFF` if missing) | 10 |

**To add a new sensor type:**

1. Pick the next available type ID (e.g. `0x05`).
2. **C++ encoder:** add the type to `SensorType` enum in `config.hpp` and to `build_registry()` in `config_yaml.cpp`. Add field encoding in `payload_builder.cpp` if needed.
3. **ChirpStack decoder:** add the type to the `TYPES` map and its read function in the universal codec below.
4. **Documentation:** add a row to this table and to `config/lorbee/payload.manifest.example.yaml`.

Never reuse or reorder existing type IDs.

**Missing-value sentinels:** `int16` → **`0x7FFF`**, `uint16` → **`0xFFFF`**, `uint8` → **`0xFF`**.

### Packed layout (v4, current)

- Byte **0**: **`0x04`** (version — self-describing entries).
- Byte **1**: flags — bit **0** = **`include_status`** was **true** in config.
- **Repeat** for each **`entries[]`** row (in YAML order, until end of payload):
  - **`entry_id`**: **1 byte** (0–255). Instance label from **`id`** in YAML.
  - **`sensor_type`**: **1 byte**. From the Sensor Type Registry (see table above).
  - **Field data**: bytes as defined by the sensor type.
  - If **`include_status: true`**: **linkquality** (u8), **battery** (u8), **voltage** (u16 BE) — 4 bytes.

Because each entry carries its own `sensor_type`, the decoder **does not need a positional PLAN** and **does not break** when entries are added, removed, or reordered. Comment out entries freely; only the entries in `config.yaml` are sent.

**Config example:**

```yaml
payload:
  format: packed
  include_status: false
  max_bytes: 222
  entries:
    - id: 1
      device: "0x08ddebfffea49ffb"
      type: climate
    - id: 2
      device: "0x0ceff6fffeef6b90"
      type: motion
```

**Field encodings (reference):**

| Field | Bytes |
|-------|--------|
| **`temperature`** | `int16` BE, °C×100; missing → `0x7FFF` |
| **`humidity`** | `uint16` BE, %×100; missing → `0xFFFF` |
| **`occupancy`** / **`motion`** | `uint8` 0/1; missing → `0xFF` |
| **`illumination`** / **`brightness`** | `uint8` 0 unknown, 1 dark, 2 medium, 3 bright |
| **`contact`** | `uint8` 0/1; missing → `0xFF` |
| **`pm1_0`**, **`pm2_5`**, **`pm4_0`**, **`pm10`** | `uint16` BE, µg/m³×10; missing → `0xFFFF` |
| **`aqi`** | `uint16` BE, US EPA AQI 0–500 (from PM2.5); missing → `0xFFFF` |
| **`pressure_hpa`** | `uint16` BE, whole hPa (300–1100); missing → `0xFFFF` |
| **`gas_resistance_ohm`** | `uint32` BE, BME680 raw gas resistance (Ω); missing → `0xFFFFFFFF` |
| **`linkquality`**, **`battery`**, **`voltage`** | Via **`include_status`** triplet only |

### Backward compatibility

| Version byte | Firmware | Notes |
|--------------|----------|-------|
| **`0x04`** | Current | Self-describing entries with sensor_type byte. |
| **`0x03`** | Previous | Per-entry id but positional decoding (requires PLAN sync). |
| **`0x02`** | Older | No per-entry id. |

The universal codec below handles **`0x04`**, **`0x03`** (legacy PLAN fallback), and the 4-byte legacy format.

The service logs **`uplink application payload: N bytes`** before every `sendReceive`.

**Uplink interval:** `lorawan.uplink_interval_sec` (default **300** = 5 minutes).

**Environment overrides:** `LORAWAN_PAYLOAD_FORMAT` (`legacy`|`packed`), `PAYLOAD_INCLUDE_STATUS` (`0`|`1`), `PAYLOAD_MAX_BYTES` (decimal).

### ChirpStack: what you control vs what the network adds

Only the **application bytes** (the **FRMPayload** on **FPort**, e.g. your 4-byte or packed blob) are produced by this node. **You cannot strip** fields like **`deduplicationId`**, **`tenantId`**, **`rssi`**, **`gatewayId`**, **`spreadingFactor`**, etc. from "the message" in the radio sense — those are **metadata** ChirpStack (and the gateway) attach when they **store or display** the event. They are not extra bytes on your **LoRa airtime**; airtime is roughly **LoRaWAN MAC header + your payload + MIC** (plus optional port).

To **save airtime**, shorten the **application payload** (fewer `entries`, avoid redundant status, lower `max_bytes` and respect DR limits), use a **higher data rate** / shorter **SF** where coverage allows, and send **less often** (`uplink_interval_sec`).

### ChirpStack codec — universal (v4 + v3 + legacy)

**One codec for all edges.** Paste this into your ChirpStack device profile. It handles:
- **v4** (`0x04`): self-describing entries — uses the sensor_type byte to decode.
- **v3** (`0x03`): legacy positional — needs a `V3_PLAN` if you still have old firmware.
- **legacy** (4 bytes): temperature + humidity only.

The **`TYPES`** map mirrors the Sensor Type Registry. The **`LABELS`** map is optional — it gives human-friendly keys to entry IDs (e.g. `1 → 'bedroom'`). Customize `LABELS` per deployment if desired; `TYPES` must stay the same across all edges.

```javascript
function decodeUplink(input) {
  var b = input.bytes;

  // --- Legacy 4-byte (format: legacy) ---
  if (b.length === 4) {
    var tRaw = (b[0] << 8) | b[1];
    var t = (tRaw & 0x8000) ? tRaw - 0x10000 : tRaw;
    var h = (b[2] << 8) | b[3];
    if (t === 0x7fff && h === 0xffff) return { data: { valid: false } };
    return { data: { temperature_c: t / 100, humidity_pct: h / 100, valid: true } };
  }

  if (b.length < 2) return { data: {} };
  var o = 0;
  var ver = b[o++];
  var flags = b[o++];
  var includeStatus = (flags & 1) !== 0;
  var hasAck = (flags & 2) !== 0;

  // --- Sensor Type Registry (same on every edge) ---
  var TYPES = {
    0x01: { name: 'climate', fields: ['temperature', 'humidity'] },
    0x02: { name: 'motion',  fields: ['occupancy', 'illumination'] },
    0x03: { name: 'contact', fields: ['contact'] },
    0x04: { name: 'air_quality', fields: ['pm1_0', 'pm2_5', 'pm4_0', 'pm10'] },
    0x05: { name: 'air_quality_aqi', fields: ['pm1_0', 'pm2_5', 'pm4_0', 'pm10', 'aqi'] },
    0x06: { name: 'environment', fields: ['temperature', 'humidity', 'pressure_hpa', 'gas_resistance_ohm'] }
  };

  // Optional: human-friendly labels for entry IDs. Customize per deployment.
  var LABELS = {};

  // --- Field readers ---
  function readI16() {
    var raw = (b[o] << 8) | b[o + 1]; o += 2;
    return (raw & 0x8000) ? raw - 0x10000 : raw;
  }
  function readU16() { var v = (b[o] << 8) | b[o + 1]; o += 2; return v; }
  function readU32() {
    var v = ((b[o] << 24) | (b[o + 1] << 16) | (b[o + 2] << 8) | b[o + 3]) >>> 0;
    o += 4;
    return v;
  }
  function readU8() { return b[o++]; }

  function readField(fname) {
    if (fname === 'temperature') {
      var raw = readI16();
      return raw === 0x7fff ? null : raw / 100;
    }
    if (fname === 'humidity') {
      var raw = readU16();
      return raw === 0xffff ? null : raw / 100;
    }
    if (fname === 'occupancy' || fname === 'motion') {
      var v = readU8();
      return v === 0xff ? null : v !== 0;
    }
    if (fname === 'illumination' || fname === 'brightness') {
      var v = readU8();
      var names = ['unknown', 'dark', 'medium', 'bright'];
      return v < names.length ? names[v] : 'unknown';
    }
    if (fname === 'contact') {
      var v = readU8();
      return v === 0xff ? null : v !== 0;
    }
    if (fname === 'pm1_0' || fname === 'pm2_5' || fname === 'pm4_0' || fname === 'pm10') {
      var raw = readU16();
      return raw === 0xffff ? null : raw / 10;
    }
    if (fname === 'aqi') {
      var aq = readU16();
      return aq === 0xffff ? null : aq;
    }
    if (fname === 'pressure_hpa') {
      var ph = readU16();
      return ph === 0xffff ? null : ph;
    }
    if (fname === 'gas_resistance_ohm') {
      var gr = readU32();
      return gr === 0xffffffff ? null : gr;
    }
    return null;
  }

  function readStatus(row) {
    row.linkquality = readU8();
    var bat = readU8();
    row.battery_pct = bat === 0xff ? null : bat;
    var vRaw = readU16();
    row.voltage_mV = vRaw === 0xffff ? null : vRaw;
  }

  // Map field names to decoded keys
  var FIELD_KEYS = {
    temperature: 'temperature_c',
    humidity: 'humidity_pct',
    occupancy: 'occupancy',
    motion: 'occupancy',
    illumination: 'illumination',
    brightness: 'illumination',
    contact: 'contact',
    pm1_0: 'pm1_0_ugm3',
    pm2_5: 'pm2_5_ugm3',
    pm4_0: 'pm4_0_ugm3',
    pm10: 'pm10_ugm3',
    aqi: 'aqi',
    pressure_hpa: 'pressure_hpa',
    gas_resistance_ohm: 'gas_resistance_ohm'
  };

  var out = {};

  // --- v4: self-describing entries ---
  if (ver === 0x04) {
    if (hasAck && o + 2 <= b.length) {
      var ackCmd = readU8();
      var ackOk = readU8();
      out._ack = { cmd_id: ackCmd, success: ackOk !== 0 };
    }
    while (o < b.length) {
      var eid = readU8();
      var stype = readU8();
      var td = TYPES[stype];
      if (!td) {
        out['unknown_type_' + stype] = { entry_id: eid, error: 'unknown_sensor_type' };
        break;
      }
      var key = LABELS[eid] !== undefined ? LABELS[eid] : td.name + '_' + eid;
      var row = { entry_id: eid, sensor_type: td.name };
      for (var fi = 0; fi < td.fields.length; fi++) {
        var fn = td.fields[fi];
        row[FIELD_KEYS[fn] || fn] = readField(fn);
      }
      if (includeStatus) readStatus(row);
      out[key] = row;
    }
    return { data: out };
  }

  // --- v3: legacy positional (backward compat) ---
  if (ver === 0x03) {
    var V3_PLAN = [
      { id: 1, fields: ['temperature', 'humidity'] },
      { id: 2, fields: ['occupancy', 'illumination'] }
    ];
    var V3_LABELS = { 1: 'air', 2: 'motion' };
    for (var pi = 0; pi < V3_PLAN.length; pi++) {
      var step = V3_PLAN[pi];
      if (o >= b.length) break;
      var eid = readU8();
      var key = V3_LABELS[eid] !== undefined ? V3_LABELS[eid] : ('id_' + eid);
      var row = { entry_id: eid };
      if (eid !== step.id) row._id_mismatch = true;
      for (var fi = 0; fi < step.fields.length; fi++) {
        var fn = step.fields[fi];
        row[FIELD_KEYS[fn] || fn] = readField(fn);
      }
      if (includeStatus) readStatus(row);
      out[key] = row;
    }
    return { data: out };
  }

  return { data: { error: 'unknown_payload_version', ver: ver } };
}
```

If a device profile might send **both** legacy 4-byte and packed frames, the codec above handles it via `b.length === 4`.

## Downlink Command Protocol

The LoRa node can receive **downlink** commands from ChirpStack during the Class-A receive windows (RX1/RX2) that open after every uplink. This allows remote configuration of edge devices from a central dashboard.

### How it works

1. **Central** (ChirpStack on RPi 5) queues a downlink for a specific device.
2. **Next uplink cycle** (≤ `uplink_interval_sec`), the device transmits and opens RX windows.
3. ChirpStack delivers the queued downlink during RX1 or RX2.
4. The firmware parses the command and executes it locally.
5. The **next uplink** includes an **ACK** (command ID + success/fail) so the central side knows the result.

Class A means the device **only listens after sending** — no continuous power draw. Worst-case delivery latency equals the uplink interval (default 300 s).

### FPort

All downlink commands use **fPort 10** (`DOWNLINK_CMD_FPORT`). Downlinks on other fPorts are logged and ignored.

### Downlink payload format

| Byte | Field |
|------|-------|
| 0 | Command ID |
| 1–N | Command-specific payload |

### Command registry

| Cmd ID | Name | Payload | Description |
|--------|------|---------|-------------|
| `0x01` | **Ping** | *(none)* | Edge logs `PONG`. Confirms the downlink path works. |
| `0x02` | **Set uplink interval** | `u16 BE` (seconds) | Changes the uplink interval at runtime (clamped 30–3600 s). Takes effect on the **next** sleep cycle. Runtime only — container restart reloads config. |
| `0x03` | **Permit Zigbee join** | `u8` (duration in seconds, 0 = disable) | Publishes to `zigbee2mqtt/bridge/request/permit_join` via local MQTT. Also writes audit log to `/data/downlink-commands/latest.json`. |

Unknown command IDs are logged with their hex value and ignored.

### Uplink ACK (application-level)

After processing a downlink command, the **next packed uplink** includes an ACK block so the central decoder (and any dashboard) can confirm execution.

**Packed v4 flags byte** (byte 1):

| Bit | Name | Meaning |
|-----|------|---------|
| 0 | `include_status` | Per-entry linkquality/battery/voltage (existing) |
| 1 | `has_ack` | ACK block follows the flags byte |

**When `has_ack` is set**, bytes 2–3 (before the sensor entries) contain:

| Byte | Field |
|------|-------|
| 0 | `ack_cmd_id` — the command ID that was processed |
| 1 | `ack_status` — `0x01` = success, `0x00` = failure |

The ChirpStack codec decodes this as `_ack: { cmd_id: N, success: true/false }` in the uplink object. The ACK is sent **once** per command — the flag clears after the uplink is transmitted.

If the payload would exceed `max_bytes` with the ACK, the ACK is deferred to the next cycle.

**Note:** ACK only works in **packed** payload mode. Legacy 4-byte uplinks do not carry ACK.

### MQTT bridge (Zigbee commands)

Commands that target Zigbee2MQTT (e.g. `permit_join`) are published **directly** to the local Mosquitto broker via `mosquitto_pub` (the LoRa container includes `mosquitto-clients` and uses `network_mode: host`). This means Zigbee commands execute immediately — no sidecar or file watcher needed.

An audit log is also written to `/data/downlink-commands/latest.json` (host: `data/downlink-commands/`).

Example `latest.json`:

```json
{"command":"permit_join","value":true,"time":120,"ts":1713100000,"mqtt":true}
```

### Environment overrides

| Variable | Default | Purpose |
|----------|---------|---------|
| `DOWNLINK_CMD_DIR` | `/data/downlink-commands` | Directory for command audit log files |
| `DOWNLINK_MQTT_HOST` | `127.0.0.1` | MQTT broker host for Zigbee commands |
| `DOWNLINK_MQTT_PORT` | `1883` | MQTT broker port |

### Testing via ChirpStack UI

1. Open ChirpStack → **Applications** → your application → **Devices** → select the edge device.
2. Go to the **Queue** tab.
3. **Enqueue downlink:**
   - **fPort:** `10`
   - **Payload** (hex): see examples below.
   - **Confirmed:** optional (MAC-level ACK only; application ACK is automatic in the next uplink).
4. Wait for the next uplink (check `docker compose logs chirpstack-lora-node`). The downlink is delivered in the RX window after the uplink.

### Test payloads (hex)

| Command | Hex payload | Meaning |
|---------|-------------|---------|
| Ping | `01` | Expect `PONG` in logs; next uplink carries `_ack: { cmd_id: 1, success: true }` |
| Set interval to 60 s | `02003c` | `0x003C` = 60 decimal |
| Set interval to 300 s | `02012c` | `0x012C` = 300 decimal |
| Enable join 120 s | `0378` | `0x78` = 120 seconds; publishes to Z2M MQTT |
| Disable join | `0300` | duration 0 = disable |

### Log output

```
[lorawan-node] downlink: RX1 3 bytes fPort=10
[downlink] cmd=0x02 payload=2 bytes: 00 3c
[downlink] result: cmd=0x02 OK — uplink interval 300 -> 60 s
[lorawan-node] uplink ACK: cmd=0x02 status=1
[lorawan-node] uplink application payload: 18 bytes
```

### Extending

To add new commands:

1. Add a new `DownlinkCmd` enum value in `downlink_handler.hpp`.
2. Implement the handler function in `downlink_handler.cpp`.
3. Add the `case` to the dispatcher `switch`.
4. Document in this table.

## References

- RadioLib: https://github.com/jgromes/RadioLib  
- lgpio: https://github.com/joan2937/lg  
- ChirpStack: https://www.chirpstack.io/
