.PHONY: init setup-first-run setup up up-safe stack-after-boot down ps logs pull lora-build lora-up lora-down lora-logs zigbee-restart-ready install-boot-timer

# Snapshot dir (honours LORBEE_SNAPSHOT_HOST in .env — see docs/lorbeeos-paths.md)
init:
	bash scripts/ensure-data-dirs.sh

# Interactive .env + Zigbee2MQTT template + data dirs (see README)
setup-first-run:
	bash scripts/setup-first-run.sh

setup: setup-first-run

up:
	docker compose up -d

# Wait for Zigbee USB, start mosquitto+zigbee2mqtt+nodered, optional restart zigbee2mqtt, optional LoRa (same as systemd timer).
up-safe:
	bash scripts/stack-after-boot.sh

stack-after-boot:
	bash scripts/stack-after-boot.sh

# Alias for stack-after-boot (old name).
zigbee-restart-ready:
	bash scripts/stack-after-boot.sh

# Install systemd timer on the Pi (Zigbee USB ready + optional LoRa) — requires sudo.
install-boot-timer:
	sudo bash scripts/install-zigbee-boot-timer.sh

down:
	docker compose down

ps:
	docker compose ps

logs:
	docker compose logs -f --tail 100

pull:
	docker compose pull

lora-build:
	docker compose build chirpstack-lora-node

lora-up:
	docker compose --profile lora up -d chirpstack-lora-node

lora-down:
	docker compose --profile lora stop chirpstack-lora-node 2>/dev/null || true
	docker compose --profile lora rm -f chirpstack-lora-node 2>/dev/null || true

lora-logs:
	docker compose --profile lora logs -f --tail 100 chirpstack-lora-node
