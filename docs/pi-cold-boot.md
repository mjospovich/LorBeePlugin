# Pi cold boot — Zigbee (and optional LoRa) without manual repair

Operators who did not develop this stack only need this checklist once per machine.

## 1. Normal stack (if not already done)

```bash
cd ~/LorBeePlugin   # or your clone path
cp .env.example .env               # then edit .env
cp zigbee2mqtt/data/configuration.yaml.example zigbee2mqtt/data/configuration.yaml
make init
docker compose up -d
```

**LoRa reads `data/snapshot/latest.json`** (Zigbee merged by Node-RED). If you only start **`chirpstack-lora-node`** and leave **Mosquitto / Zigbee2MQTT / Node-RED** off, the snapshot stops updating and packed payloads can fail with **device key not in snapshot**. Bring the **whole** stack up for normal operation:

```bash
docker compose up -d
docker compose --profile lora up -d chirpstack-lora-node   # if you use LoRa
```

In **`.env`**, at minimum:

| Variable | Purpose |
|----------|---------|
| **`ZIGBEE_SERIAL_DEVICE`** | **`/dev/serial/by-id/...`** for the coordinator (run `ls -la /dev/serial/by-id/`). If unset or wrong, boot helpers **auto-detect** when exactly one known dongle exists. |
| **`NODE_RED_USER`** | Must match `id -u`:`id -g` of the owner of **`nodered/data`**. |
| **`ZIGBEE_SERIAL_WAIT_SEC`** / **`ZIGBEE_SERIAL_SETTLE_SEC`** | Optional; defaults work (see **`.env.example`**). |

## 2. One command — systemd timer (recommended)

Runs **`scripts/stack-after-boot.sh`** once per boot (~45 s after power-on):

1. Waits for the Zigbee coordinator USB device (and exports **`ZIGBEE_SERIAL_DEVICE`**).  
2. **`docker compose up -d mosquitto zigbee2mqtt nodered`** — brings up the **whole default stack** (not only LoRa).  
3. **Optional** **`docker compose restart zigbee2mqtt`** only if **`ZIGBEE2MQTT_POST_UP_RESTART=1`** in `.env` (default is **skipped** — avoids bouncing the coordinator during Z-Stack init and helps battery devices after outages).  
4. If **`.env`** has **`COMPOSE_PROFILES=lora`** or **`LORBEE_LORA_ON_BOOT=1`**: **`docker compose --profile lora up -d chirpstack-lora-node`**.

The first `up` lists services **explicitly** so **`COMPOSE_PROFILES=lora`** does not start the LoRa container *before* Zigbee serial is ready.

```bash
cd ~/LorBeePlugin   # or your clone path
sudo bash scripts/install-zigbee-boot-timer.sh
```

Or: **`make install-boot-timer`** (same thing; asks for `sudo`).

**Uninstall:**

```bash
sudo systemctl disable --now lorbee-zigbee-after-boot.timer
sudo rm -f /etc/systemd/system/lorbee-zigbee-after-boot.{service,timer}
sudo systemctl daemon-reload
```

## 3. Optional — LoRa on the same boot helper

In **`.env`** (either is enough):

- **`COMPOSE_PROFILES=lora`**, and/or  
- **`LORBEE_LORA_ON_BOOT=1`**

The script then also runs **`docker compose --profile lora up -d chirpstack-lora-node`**.

LoRa **OTAA persistence** (no ChirpStack nonce dance every reboot): host dir **`data/lorawan-state/`** — **`make init`** creates it; see **[lora/README.md](../lora/README.md)** (OTAA session persistence).

## 4. Verify after install or reboot

```bash
systemctl status lorbee-zigbee-after-boot.timer
journalctl -u lorbee-zigbee-after-boot.service -n 40 --no-pager
docker compose logs zigbee2mqtt --tail 30
```

**`list-timers`** often shows **`NEXT -`** for **`OnBootSec`** timers until the next reboot — that is normal.

## 5. Without systemd (manual)

From the repo:

- **`make zigbee-restart-ready`** — after SSH in post-boot  
- **`make up-safe`** — instead of plain **`docker compose up -d`** if you start the stack by hand right after power-on  

## Related

- **[zigbee2mqtt.md](zigbee2mqtt.md)** — serial, persistence, troubleshooting  
- **[docker-deployment.md](docker-deployment.md)** — Docker on the Pi  
- **[lora/README.md](../lora/README.md)** — LoRaWAN + ChirpStack  
