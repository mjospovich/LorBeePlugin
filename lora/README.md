# LoRaWAN uplink (ChirpStack) — modular add-on

End-device software for an **Adafruit RFM9x** (SX1276) on the Pi **SPI** bus, speaking **LoRaWAN OTAA** so a **LoRaWAN gateway** (e.g. Sentrius-class or any Semtech-packet-forwarder gateway) forwarding to **ChirpStack** can receive frames.

This folder is part of **LorBeePlugin**, the **remote edge** stack: one Pi per site, optional LoRa to a **central** ChirpStack (and other LoRa sensors on the same gateway network). **Full-system picture:** **[../assets/architecture.png](../assets/architecture.png)** and **[../docs/architecture.md](../docs/architecture.md)**.

The Zigbee/MQTT stack stays unchanged. LoRa runs only when you enable the Compose **profile** `lora`.

## How ChirpStack, the gateway, and this Pi relate

There is **no cable or pairing step** between the LoRa module and the **gateway**. In LoRaWAN:

1. **This Pi** is an **end device**: it transmits encrypted **LoRaWAN** frames over the air (868 MHz in EU868, etc.).
2. The **gateway** is only a **radio bridge**: if it is in range and configured for the same regional band, it receives uplinks and forwards them to **ChirpStack** (usually over IP — Ethernet/Wi‑Fi).
3. **ChirpStack** is the **network + application server**: it verifies the **Join Request** / **MIC**, stores **keys**, and shows **application payloads** in the web UI.

So “linking” means: **same band + in RF range**, and in ChirpStack you register a device whose **DevEUI / JoinEUI / AppKey** match **`keys.env` on this Pi**. The gateway does not need to know your Pi’s IP; it only forwards what it hears to ChirpStack.

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
7. Start the LoRa container on the Pi; watch **`docker compose logs chirpstack-lora-node`** for join. If you recreated the device, use ChirpStack’s **reset join nonces** once if join is rejected.

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
| **0** | `/dev/spidev0.0` | SPI “channel 0” / CE0 side of SPI0 |
| **1** | `/dev/spidev0.1` | SPI “channel 1” / CE1 side (**default** in this repo’s `config.yaml` and wizard) |

**How to choose:** If **`ls /dev/spidev0.*`** lists both, **start with `1`** (matches the shipped example and many wiring guides). If you only see **`spidev0.0`**, choose **0**. If the radio fails to initialise (`ERR_CHIP_NOT_FOUND`, SPI errors), try the other index after checking wiring and SPI enable.

### SPI CS vs Linux (important)

On Raspberry Pi, **GPIO7** and **GPIO8** are **hardware chip selects** for SPI0 (`/dev/spidev0.1` and `spidev0.0`). The kernel reserves them. RadioLib uses **lgpio** to bit-bang the CS line you configure; if you set **CS = GPIO7**, you get **`GPIO busy`** / **`Could not claim pin 7`** and **`ERR_CHIP_NOT_FOUND`**.

**Fix:** Wire **CS** to any unused GPIO **except** 7, 8, and pins already used for SPI/MISO/MOSI/SCK, **DIO0**, or **RST** (e.g. **BCM17** / pin 11). Set `hardware.pins.cs` in `config.yaml` to match, restart the container. Keep MOSI/MISO/SCK unchanged.

Adafruit’s diagram often shows CS on CE1 for Arduino-style boards; on **Linux + Pi** you need a **separate GPIO** for NSS unless you use a custom device-tree setup.

## Quick start (Docker — recommended for “clone and run”)

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

**`device key not in snapshot`:** the IEEE in **`payload.entries`** has no matching key under JSON **`devices`**. Often a **startup race**: the LoRa container joins before **Node-RED** has written **`latest.json`** (Zigbee2MQTT may already publish MQTT seconds earlier). The firmware waits **`LORAWAN_STARTUP_GRACE_SEC`** (default **30 s**) after join before the first uplink, and if an uplink is still skipped it retries after **`LORAWAN_SNAPSHOT_RETRY_SEC`** (default **15 s**) instead of the full **`uplink_interval_sec`**. Set in **`keys.env`**. Other causes: stack not fully up, sensor offline, or **wrong IEEE** in `config.yaml`. **`device_labels`** can list the ID while **`devices`** is empty — the packed builder only reads **`devices`**.


### `legacy` (default)

Uses **`temperature`** and **`humidity`** only:

- If **`snapshot.device_ieee`** is set, that key under `devices` is used (IEEE normalization matches lowercase `0x…`).
- Otherwise the **first** device object in the map that has both fields is used.

**Application payload: 4 bytes, big-endian**

| Bytes | Meaning |
|-------|--------|
| 0–1 | `int16` temperature **centidegrees** (°C × 100); missing → **0x7FFF** |
| 2–3 | `uint16` humidity **centipercent** (% × 100); missing → **0xFFFF** |

### `packed`

Set in **`config.yaml`** under **`payload:`** (see comments in `config.yaml` / `config.example.yaml`):

| YAML | Meaning |
|------|--------|
| **`format: packed`** | Use multi-sensor binary layout below. |
| **`entries`** | Ordered list: each **`device`** is a key in `snapshot.devices` (IEEE), **`fields`** = send order. Optional **`id`**: **0–255** (one byte on-air). Omit **`id` on all entries** → auto **0, 1, 2, …**; **do not** mix explicit and omitted `id` in the same file. |
| **`include_status`** | If **true**, after each entry’s `fields`, append **linkquality** (u8), **battery** (u8, 0–100), **voltage** (u16 BE, mV); **0xFF** / **0xFFFF** if missing. |
| **`max_bytes`** | Build fails (uplink skipped, logged) if longer — tune for your data rate / regional max. |

**Packed layout (v2, current):**

- Byte **0**: **`0x03`**
- Byte **1**: flags — bit **0** = **`include_status`** was **true** in config.
- For each **`entries[]`** row, **in YAML order**:
  - **`entry_id`**: **1 byte** (0–255) — same value as **`id`** in YAML (or auto 0, 1, 2, …). Use this in the ChirpStack codec to attach human-readable labels.
  - Then each **`fields`** value in order (encodings below).
  - If **`include_status: true`**, append **linkquality**, **battery**, **voltage** for that device.

**Field encodings:**

| Field | Bytes |
|-------|--------|
| **`temperature`** | `int16` BE, °C×100; missing → `0x7FFF` |
| **`humidity`** | `uint16` BE, %×100; missing → `0xFFFF` |
| **`occupancy`** / **`motion`** | `uint8` 0/1; missing → `0xFF` |
| **`illumination`** / **`brightness`** | `uint8` 0 unknown, 1 dark, 2 medium, 3 bright |
| **`linkquality`**, **`battery`**, **`voltage`** | Same as in **`fields`** if listed, or via **`include_status`** triplet |

Older builds used **`0x02`** without a per-entry **id** byte — use the legacy branch in the codec below only if you still run old firmware.

The service logs **`uplink application payload: N bytes`** before every `sendReceive`.

**Uplink interval:** `lorawan.uplink_interval_sec` (default **300** = 5 minutes).

**Environment overrides:** `LORAWAN_PAYLOAD_FORMAT` (`legacy`|`packed`), `PAYLOAD_INCLUDE_STATUS` (`0`|`1`), `PAYLOAD_MAX_BYTES` (decimal).

### ChirpStack: what you control vs what the network adds

Only the **application bytes** (the **FRMPayload** on **FPort**, e.g. your 4-byte or packed blob) are produced by this node. **You cannot strip** fields like **`deduplicationId`**, **`tenantId`**, **`rssi`**, **`gatewayId`**, **`spreadingFactor`**, etc. from “the message” in the radio sense — those are **metadata** ChirpStack (and the gateway) attach when they **store or display** the event. They are not extra bytes on your **LoRa airtime**; airtime is roughly **LoRaWAN MAC header + your payload + MIC** (plus optional port).

To **save airtime**, shorten the **application payload** (fewer `entries` / `fields`, avoid redundant status, lower `max_bytes` and respect DR limits), use a **higher data rate** / shorter **SF** where coverage allows, and send **less often** (`uplink_interval_sec`).

**ChirpStack codec — legacy 4-byte (`format: legacy`):**

```javascript
function decodeUplink(input) {
  var b = input.bytes;
  if (b.length < 4) return { data: {} };
  var tRaw = (b[0] << 8) | b[1];
  var t = (tRaw & 0x8000) ? tRaw - 0x10000 : tRaw;
  var h = (b[2] << 8) | b[3];
  if (t === 0x7fff && h === 0xffff) return { data: { valid: false } };
  return { data: { temperature_c: t / 100, humidity_pct: h / 100, valid: true } };
}
```

**ChirpStack codec — packed v2 (`0x03`, with per-entry `id`):**

Keep **`PLAN`** and **`LABELS`** in sync with **`config.yaml`** (`payload.entries`: same **`id`** values, same **`fields`** order). **`LABELS`** maps **numeric `id`** → a short key in the decoded object (`air`, `motion`, …). Zigbee friendly names are not sent on-air; use **`entry_id`** + **`LABELS`** to know which block is which sensor.

```javascript
function decodeUplink(input) {
  var b = input.bytes;
  if (b.length === 4) {
    var tRaw = (b[0] << 8) | b[1];
    var t = (tRaw & 0x8000) ? tRaw - 0x10000 : tRaw;
    var hRaw = (b[2] << 8) | b[3];
    if (t === 0x7fff && hRaw === 0xffff) return { data: { valid: false } };
    return { data: { temperature_c: t / 100, humidity_pct: hRaw / 100, valid: true } };
  }
  var o = 0;
  if (b.length < 2) return { data: {} };
  var ver = b[o++];
  if (ver !== 0x03) return { data: { error: 'unknown_payload_version', ver: ver } };
  var flags = b[o++];
  var includeStatus = (flags & 1) !== 0;

  var LABELS = { 1: 'air', 2: 'motion' };
  var PLAN = [
    { id: 1, fields: ['temperature', 'humidity'] },
    { id: 2, fields: ['occupancy', 'illumination'] }
  ];

  function readTemp() {
    var tRaw = (b[o] << 8) | b[o + 1]; o += 2;
    var t = (tRaw & 0x8000) ? tRaw - 0x10000 : tRaw;
    return t === 0x7fff ? null : t / 100;
  }
  function readHum() {
    var hRaw = (b[o] << 8) | b[o + 1]; o += 2;
    return hRaw === 0xffff ? null : hRaw / 100;
  }
  function readOcc() {
    var v = b[o++];
    return v === 0xff ? null : v !== 0;
  }
  function readIllum() {
    var v = b[o++];
    var names = ['unknown', 'dark', 'medium', 'bright'];
    return v < names.length ? names[v] : 'unknown';
  }
  function readStatus(row) {
    row.linkquality = b[o++];
    var bat = b[o++];
    row.battery_pct = bat === 0xff ? null : bat;
    var vRaw = (b[o] << 8) | b[o + 1]; o += 2;
    row.voltage_mV = vRaw === 0xffff ? null : vRaw;
  }

  var out = {};
  for (var pi = 0; pi < PLAN.length; pi++) {
    var step = PLAN[pi];
    if (o >= b.length) break;
    var eid = b[o++];
    var key = LABELS[eid] !== undefined ? LABELS[eid] : ('id_' + eid);
    var row = { entry_id: eid };
    if (eid !== step.id) row._id_mismatch = true;
    for (var fi = 0; fi < step.fields.length; fi++) {
      var fn = step.fields[fi];
      if (fn === 'temperature') row.temperature_c = readTemp();
      else if (fn === 'humidity') row.humidity_pct = readHum();
      else if (fn === 'occupancy' || fn === 'motion') row.occupancy = readOcc();
      else if (fn === 'illumination' || fn === 'brightness') row.illumination = readIllum();
    }
    if (includeStatus) readStatus(row);
    out[key] = row;
  }
  return { data: out };
}
```

If a device profile might send **both** legacy 4-byte and packed frames, use **`b.length === 4`** to pick the legacy branch (as above). Adapt to your ChirpStack **codec / function signature** version.

## References

- RadioLib: https://github.com/jgromes/RadioLib  
- lgpio: https://github.com/joan2937/lg  
- ChirpStack: https://www.chirpstack.io/
