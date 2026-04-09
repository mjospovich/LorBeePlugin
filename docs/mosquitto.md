# Mosquitto (MQTT broker)

**Compose service:** `mosquitto`  
**Image:** `eclipse-mosquitto:latest`  
**Network:** `host` (broker listens on the Pi’s **1883**)

## What it does

[Eclipse Mosquitto](https://mosquitto.org/) is the MQTT message bus. **Zigbee2MQTT** publishes device state; **Node-RED** subscribes and publishes (e.g. merged snapshot). No TLS in the default config—suitable for trusted LAN / lab use.

## Files in this repo

| Path | Purpose |
|------|---------|
| `mosquitto/config/mosquitto.conf` | Broker settings: listener **1883**, persistence under `/mosquitto/data`, logs to stdout. |
| `mosquitto/data/` | **Runtime** persistence (gitignored). Created by the container on first run. |

## Configuration highlights

From `mosquitto/config/mosquitto.conf`:

- `listener 1883` — plain MQTT (all interfaces on host network).
- `allow_anonymous true` — easy local dev; **change for production** (password file or TLS).
- `persistence true` — retained messages and broker state survive restarts.

## Ports

| Port | Protocol | Use |
|------|----------|-----|
| 1883 | MQTT | Clients on the Pi and LAN |

## Dependencies

None at the Docker level (first service to start). **Zigbee2MQTT** and **Node-RED** declare `depends_on: mosquitto` for startup order.

## Operations

```bash
docker compose logs -f mosquitto
```

See root [README](../README.md) for `make logs` / compose commands.
