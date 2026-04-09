# Zigbee2MQTT

**Compose service:** `zigbee2mqtt`  
**Image:** `koenkk/zigbee2mqtt:latest`  
**Network:** `host` — web UI on **8080**, MQTT via **localhost:1883**

## What it does

[Zigbee2MQTT](https://www.zigbee2mqtt.io/) drives the **USB Zigbee coordinator** (e.g. Sonoff Zigbee 3.0 USB Dongle Plus with Z-Stack firmware), maintains the Zigbee network, and maps each device to MQTT topics under the configured base topic (default **`zigbee2mqtt`**).

## Files in this repo

| Path | Purpose |
|------|---------|
| `zigbee2mqtt/data/configuration.yaml` | **Runtime** active config (gitignored). Copy from example on first deploy. |
| `zigbee2mqtt/data/configuration.yaml.example` | Template: MQTT URL, serial port `/dev/ttyUSB0`, `adapter: zstack`, frontend port **8080**. |

Runtime files (gitignored): `state.json`, `database.db*`, `coordinator_backup.json`, `log/`.

### Adapter type (`serial.adapter`)

The example uses **`zstack`** (Texas Instruments Z-Stack / common CC2652 sticks, Sonoff ZBDongle-P, etc.). Change **`serial.adapter`** if your coordinator uses a different stack:

| Coordinator / firmware (typical) | `adapter` value |
|----------------------------------|-----------------|
| Texas Instruments Z-Stack (Sonoff ZBDongle-P, zzh, SLZB with Z-Stack, many CC2652 USB) | `zstack` |
| Silicon Labs EmberZNet (e.g. some EFR32 coordinators, Sonoff ZBDongle-E) | `ember` |
| Dresden Elektronik ConBee / RaspBee | `deconz` |
| Zigate USB | `zigate` |

Confirm against the [Zigbee2MQTT supported adapters](https://www.zigbee2mqtt.io/guide/adapters/) list for your exact model and firmware.

### Channel (2.4 GHz) — neighbours and Wi‑Fi

Zigbee on 2.4 GHz uses **IEEE channels 11–26** (`advanced.channel` in `configuration.yaml`). **Adjacent deployments** (another home, a second edge Pi, a neighbour’s Hue bridge) on the **same channel** share the same radio band: more collisions, flakier links, and harder diagnosis. **Give each independent network a different channel** when their radios can hear each other.

| Situation | Recommendation |
|-----------|----------------|
| **You control all coordinators** | Assign **different channels** per site / per Pi (e.g. 15, 20, 25). |
| **Neighbour runs Zigbee you don’t control** | Pick a channel **away** from theirs if you can infer it (Zigbee2MQTT logs / UI, Wi‑Fi analyser, trial and error). |
| **Strong Wi‑Fi on 2.4 GHz** | Wi‑Fi **width** overlaps several Zigbee channels. Very roughly: Wi‑Fi ch **1** spans ~Zigbee **11–14**, ch **6** ~**16–19**, ch **11** ~**21–24** (not exact; 40 MHz Wi‑Fi is wider). Prefer Zigbee channels with **less Wi‑Fi energy** at the coordinator. |
| **First-time setup** | Run **`bash scripts/setup-first-run.sh`** to set **`advanced.channel`** interactively, or edit **`zigbee2mqtt/data/configuration.yaml`** before pairing. |
| **Already paired** | Changing channel **changes the operating RF channel**; end devices may need time to follow or may need **re-pairing** in bad cases. Prefer locking channel **before** production pairing. |

**Defaults:** `configuration.yaml.example` uses **channel 15** — fine for a lab; **change it** for production if it clashes.

**Do not** commit real **`network_key`**, **`pan_id`**, or **`ext_pan_id`** — Zigbee2MQTT can generate these on first start; they define logical network identity as well as security.

### Other common options (reference)

| Goal | Where | Notes |
|------|--------|--------|
| RF channel | `advanced.channel` | **11–26**; see table above. |
| Coordinator firmware stack | `serial.adapter` | `zstack`, `ember`, `deconz`, `zigate` — must match hardware. |
| TX power (if supported) | `advanced.transmit_power` | Adapter-specific; omit for firmware default. |
| MQTT topic prefix | `mqtt.base_topic` | Change if several bridges share one broker and you need separation. |
| Online/offline in UI | `availability.enabled` | Default off; enable + tune `passive.timeout` for long coordinator outages (see below). |

## Serial device

The compose file passes **`ZIGBEE_SERIAL_DEVICE`** (see [`.env.example`](../.env.example)) to the container as **`/dev/ttyUSB0`**. On the host, use a stable path:

```bash
ls -la /dev/serial/by-id/
```

Set e.g. `ZIGBEE_SERIAL_DEVICE=/dev/serial/by-id/usb-ITead_Sonoff_...` in `.env`.

**Auto-detect (swap-friendly):** If that path is **missing** (wrong unplugged copy) or you **omit** `ZIGBEE_SERIAL_DEVICE`, the boot helpers **`scripts/resolve-zigbee-serial.sh`** and **`scripts/wait-for-zigbee-serial.sh`** pick the **only** matching coordinator under **`/dev/serial/by-id`** (Sonoff, Texas Instruments Z-Stack, ConBee, SLZB, zzh, …). If **more than one** match, set **`ZIGBEE_SERIAL_DEVICE`** explicitly.

## MQTT topics (typical)

| Pattern | Content |
|---------|---------|
| `zigbee2mqtt/<friendly_name>` | Device state (JSON). |
| `zigbee2mqtt/bridge/*` | Bridge health, logging, requests—not merged into the sensor snapshot in Node-RED. |

## Web UI

- URL: `http://<pi-ip>:8080` — pairing, network map, device rename.

### “Last seen” / “Availability” = **Disabled**

In the device table, **Disabled** usually means the **availability feature is turned off** in `configuration.yaml` (Zigbee2MQTT defaults to `availability.enabled: false`). It does **not** by itself prove the radio link is dead — only that the bridge is not publishing timeouts or last-seen tracking for that column.

Enable it so you get real **online/offline** and MQTT retained topics under `zigbee2mqtt/<name>/availability`:

```yaml
availability:
  enabled: true
  passive:
    timeout: 4320   # minutes; increase if the coordinator can be off for days (default passive is 25 h)
```

After the coordinator has been off **longer than** the configured passive timeout, battery devices are marked **offline** until they send **any** Zigbee frame that reaches Zigbee2MQTT (a check-in). That can take **hours** for temperature/humidity end devices that only report on interval or change.

## Dependencies

- **Mosquitto** must be running (`depends_on`).
- Coordinator must be visible at the mapped host device path.

## Operations

```bash
docker compose logs -f zigbee2mqtt
```

After editing `configuration.yaml` on disk, restart: `docker compose restart zigbee2mqtt`.

## Keeping paired devices after `docker compose down`, **Pi reboot**, or power cycle

**Persistence:** Pairing lives in two places: the **coordinator dongle** (network parameters + trust center state) and **Zigbee2MQTT’s data directory** on disk (`database.db`, `state.json`, `configuration.yaml`, and for Texas Instruments adapters **`coordinator_backup.json`**). Your Compose file bind-mounts **`${LORBEE_STACK_ROOT}/zigbee2mqtt/data` → `/app/data`**, so **turning Docker off/on** or **rebooting the Pi** should **not** by itself wipe the network — the same rules apply as long as the dongle, disk files, and serial path stay consistent.

**Reboot vs Docker-only:** A full **Raspberry Pi power cycle** often surfaces problems that **`compose down/up`** does not, almost always around **USB** (see next subsection). If sensors only fail after a cold boot, treat it as **coordinator not ready / wrong tty**, not as “Zigbee forgot pairing.”

### Raspberry Pi reboot — extra gotchas

| Issue | Why it bites after power-on |
|--------|-----------------------------|
| **`/dev/ttyUSB0` is not your dongle** | After reboot the kernel can assign **different numbers** (`ttyUSB1`, …) or order hubs differently. Compose still maps **`ZIGBEE_SERIAL_DEVICE` → `/dev/ttyUSB0` inside the container**, but the **host** path must always be the **coordinator**. Wrong device → failed or bogus serial session; recovery sometimes looks like “repair everything.” **Use `/dev/serial/by-id/...` in `.env`** so the path is stable across reboots. |
| **Dongle not enumerated yet** | Docker may start **Zigbee2MQTT** before the USB stick has appeared. With **`restart: unless-stopped`**, the container may exit and **come back** once the device exists; if the first attempts confuse state, run **`docker compose restart zigbee2mqtt`** once after boot and check logs. |
| **USB power / undervoltage** | A Pi under load or a marginal supply can make the dongle **drop off** or reset; the network on disk and devices can get out of sync with what the radio actually runs. A **powered USB hub** or better PSU often fixes “only after reboot” flakiness. |
| **USB autosuspend** | Rare on Pi, but Linux can suspend USB devices; if the serial port goes away briefly, the bridge can fail. If you see disconnects in kernel logs, consider disabling autosuspend for that device (udev or kernel cmdline — distro-specific). |

### Cold boot: Docker starts before the coordinator is ready

On a **full power cycle**, the **Docker daemon** usually starts **`zigbee2mqtt`** as soon as Mosquitto is healthy — often **before** the USB dongle exists or before the **CC2652** inside has finished booting. The container may then open the serial port in a **bad window**: Zigbee2MQTT or the stack can end up in a state where the **radio no longer matches** what end devices expect, and the UI makes you think you must **re-pair** (same symptom as a **new PAN**).

**Mitigations (use together):**

1. **`ZIGBEE_SERIAL_DEVICE=/dev/serial/by-id/...`** in `.env` (you already should).  
2. **Boot helper script** — **`scripts/stack-after-boot.sh`**: wait for USB, **`docker compose up -d mosquitto zigbee2mqtt nodered`**, then **optional** delayed **`restart zigbee2mqtt`** only if **`ZIGBEE2MQTT_POST_UP_RESTART=1`** in `.env` (default is **off** — a second restart right after boot can worsen coordinator and battery-device recovery). Then optional LoRa.
   - Optional `.env`: **`ZIGBEE_SERIAL_WAIT_SEC`**, **`ZIGBEE_SERIAL_SETTLE_SEC`** (see [`.env.example`](../.env.example)).
3. **systemd timer (recommended on the Pi)** — runs the restart helper **once per boot** after a short delay so you do not have to SSH in after a power cut.
   - **One command (operators):** from the repo root, **`sudo bash scripts/install-zigbee-boot-timer.sh`** or **`make install-boot-timer`**.
   - **Full checklist** (`.env`, LoRa, verify): **[pi-cold-boot.md](pi-cold-boot.md)**.
   - **Manual install** (same result): copy **`deploy/systemd/lorbee-zigbee-after-boot.{service,timer}`** to **`/etc/systemd/system/`**, replace **`STACK_ROOT_PLACEHOLDER`** in the **`.service`** with the absolute repo path, then **`systemctl daemon-reload`** and **`systemctl enable --now lorbee-zigbee-after-boot.timer`**.
   - Optional **`.env`**: **`COMPOSE_PROFILES=lora`** or **`LORBEE_LORA_ON_BOOT=1`** so the helper also runs **`docker compose --profile lora up -d chirpstack-lora-node`**.
4. **`adapter_delay`** in **`zigbee2mqtt/data/configuration.yaml`** (see **`configuration.yaml.example`**) — **300–800 ms** can help if the port opens but the coordinator answers late; tune empirically.
5. **Avoid yanking power** when possible: **`docker compose stop zigbee2mqtt`** (or full stack stop) before shutdown so **`database.db`** flushes cleanly; unclean power can rarely corrupt SQLite and force a “new network” experience.

### Typical reasons you end up re-pairing

| Cause | What happens |
|--------|----------------|
| **Wrong or empty data path** | **`LORBEE_STACK_ROOT`** in `.env` must point at the same tree every time (see [lorbeeos-paths.md](lorbeeos-paths.md)). If it points at a fresh clone or typo path, Compose mounts an **empty** `zigbee2mqtt/data` → Zigbee2MQTT thinks it is a **new** install and can form a **new** network; routers/end devices still trust the **old** PAN and stop talking. |
| **Replacing `configuration.yaml` with the example** | A reset or hand-edited file that no longer matches the **network identity** stored with your data (and on the dongle) behaves like a **new** network. With **`version: 5`**, critical fields may live in **`database.db`** as well — **back up and restore yaml + db together**; never wipe one and expect the other alone to suffice. |
| **Deleting gitignored runtime files** | Removing **`database.db`**, **`coordinator_backup.json`**, or the whole **`zigbee2mqtt/data`** directory wipes Zigbee2MQTT’s view of devices even if the dongle still holds keys — behavior is messy; often you must re-pair. **Back up that folder** before experiments. |
| **Unstable USB path** | If **`ZIGBEE_SERIAL_DEVICE`** is the raw **`/dev/ttyUSB0`** name, **reboots are high-risk**: the same name may point to another adapter or appear **later** than Zigbee2MQTT’s first start. **`/dev/serial/by-id/...`** is the main fix for Pi on/off (see table above). |
| **Coordinator firmware reflash / different dongle** | New trust center → re-pair everything. |
| **`:latest` image jumps** | A major Zigbee2MQTT upgrade can migrate data; rare failures corrupt **`database.db`**. Prefer a **pinned image tag** in production and back up **`zigbee2mqtt/data`** before upgrading. |
| **Brutal stop** | Prefer **`docker compose stop`** (SIGTERM, flush) over **`kill`** on stuck containers so Zigbee2MQTT can persist state. |

### What to verify after a restart or reboot

1. **`ls -la /dev/serial/by-id/`** on the Pi — your coordinator’s **`by-id`** symlink exists **before** you rely on Zigbee (if it appears late, fix boot order or delay / restart the container).  
2. **`zigbee2mqtt/data`** on the host still contains **`database.db`**, **`configuration.yaml`** (your real one), and **`coordinator_backup.json`** (Z-Stack / TI).  
3. **`docker compose logs zigbee2mqtt`** — no “starting with fresh network” / **Error: Failed to open serial port** / wrong device; devices should reappear as **interviewed**, not unknown.  
4. **Mosquitto is up before Zigbee2MQTT** — Compose waits on a broker **healthcheck** so MQTT is accepting connections before the bridge starts (reduces races on cold boot).

### If devices are “there” but silent

Try **Zigbee2MQTT → device → Reconfigure** or permit join and **wake the device** (button press) before assuming you need a full remove + repair. Full re-pair is the last resort when the **network key / PAN** on disk and dongle no longer match the devices.

### Multi-day power outage — what is realistic

**Not “impossible,” but not 100% guaranteed without trade-offs.** Zigbee battery **end devices** spend most of their time asleep. After the **coordinator** has been off for **days**, recovery when power returns is usually:

1. **Coordinator + `zigbee2mqtt/data` intact** → same network identity; devices **should** come back when they next wake and talk to the coordinator. **You cannot remotely force** a sleeping sensor to wake on a fixed schedule without **mains-powered routers** in the mesh or accepting **long** gaps until the device’s own reporting interval.
2. **LQI numbers in the UI** can be **stale** from the last session — they are not proof of live RF until new traffic arrives.
3. **Hardening for unattended / remote sites:** stable **`/dev/serial/by-id/...`**, **`adapter_delay`** if needed, **avoid unnecessary `zigbee2mqtt` restarts** after boot, add at least one **mains-powered Zigbee router/repeater** so the mesh is not “coordinator ↔ battery only,” back up **`zigbee2mqtt/data`**, and use **availability** + MQTT monitoring so you know when check-ins resume.

If the network **identity** was lost (empty data dir, wrong `LORBEE_STACK_ROOT`, corrupted DB, different dongle), **re-pair** is unavoidable — that is a limitation of how Zigbee trust centers work, not something application code can fully paper over.
