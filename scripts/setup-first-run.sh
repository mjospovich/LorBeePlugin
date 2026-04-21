#!/usr/bin/env bash
# Interactive (or batch) first-run setup: .env, Zigbee2MQTT template, data dirs.
# Run from repo root: bash scripts/setup-first-run.sh   |   bash scripts/setup-first-run.sh --yes
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

INTERACTIVE=1
WITH_LORA=0
WITH_SPS30=0
WITH_BME680=0
while [[ $# -gt 0 ]]; do
  case "$1" in
  -y | --yes)
    INTERACTIVE=0
    shift
    ;;
  --with-lora)
    WITH_LORA=1
    shift
    ;;
  --with-sps30)
    WITH_SPS30=1
    shift
    ;;
  --with-bme680)
    WITH_BME680=1
    shift
    ;;
  -h | --help)
    cat <<'EOF'
LorBeePlugin — first-run setup

  bash scripts/setup-first-run.sh              Interactive prompts (default)
  bash scripts/setup-first-run.sh --yes        Non-interactive: Zigbee + core .env only
  bash scripts/setup-first-run.sh --yes --with-lora   Non-interactive + default LoRa pins + COMPOSE_PROFILES=lora
  bash scripts/setup-first-run.sh --yes --with-sps30  Non-interactive + auto-detect SPS30 serial
  bash scripts/setup-first-run.sh --yes --with-bme680 Non-interactive + default BME680 I2C + COMPOSE_PROFILES=bme680
  make setup-first-run                         Same as the first form (needs: apt install make)

Creates or updates .env, optional LoRa pin/region keys in keys.env, optional SPS30 serial
device, optional BME680 I2C settings, copies zigbee2mqtt configuration template if missing,
optional Zigbee channel/adapter in configuration.yaml, runs ensure-data-dirs. Does not start Docker.

Non-interactive Zigbee RF: set LORBEE_ZIGBEE_CHANNEL=11-26 and/or LORBEE_ZIGBEE_ADAPTER
(zstack|ember|deconz|zigate) before  --yes .

LoRa hardware uses RFM_* / LORAWAN_* entries in lora/chirpstack-node/keys.env (overrides
config.yaml at runtime). Edit LORAWAN_DEV_EUI / LORAWAN_APP_KEY there to match ChirpStack.

After setup:  bash scripts/stack-after-boot.sh   (or make up-safe)
              sudo bash scripts/install-zigbee-boot-timer.sh
EOF
    exit 0
    ;;
  *)
    echo "lorbee-setup: unknown option: $1 (try --help)" >&2
    exit 1
    ;;
  esac
done

if [[ -n "${LORBEE_SETUP_NONINTERACTIVE:-}" ]]; then
  INTERACTIVE=0
fi
if [[ -n "${LORBEE_SETUP_WITH_LORA:-}" ]]; then
  WITH_LORA=1
fi
if [[ -n "${LORBEE_SETUP_WITH_SPS30:-}" ]]; then
  WITH_SPS30=1
fi
if [[ -n "${LORBEE_SETUP_WITH_BME680:-}" ]]; then
  WITH_BME680=1
fi

say() { printf '%s\n' "$*"; }
prompt() {
  local def="$2"
  local text="$1"
  [[ -n "$def" ]] && text="$text [$def]"
  read -r -p "$text: " ans || true
  if [[ -z "${ans:-}" ]]; then
    printf '%s\n' "$def"
  else
    printf '%s\n' "$ans"
  fi
}

yes_no() {
  local def="${2:-y}"
  local p="$1"
  [[ "$def" =~ ^[Yy] ]] && p="$p [Y/n]" || p="$p [y/N]"
  read -r -p "$p " ans || true
  if [[ -z "${ans:-}" ]]; then
    [[ "$def" =~ ^[Yy] ]]
    return
  fi
  [[ "${ans,,}" =~ ^y(es)?$ ]]
}

prompt_int() {
  local text="$1" min="$2" max="$3" def="$4"
  local v
  while true; do
    v="$(prompt "$text" "$def")"
    if [[ "$v" =~ ^[0-9]+$ ]] && ((v >= min && v <= max)); then
      printf '%s\n' "$v"
      return 0
    fi
    say "Enter an integer from $min to $max." >&2
  done
}

# Show LoRa RFM / SPI usage so BME680 wiring stays on I2C (SDA/SCL) or avoids GPIO clashes.
lora_rf_pins_summary() {
  local keys="$1"
  [[ -f "$keys" ]] || return 0
  local cs d0 rst spich
  cs=$(grep -E '^RFM_PIN_CS=' "$keys" 2>/dev/null | tail -1 | cut -d= -f2- | tr -d '\r')
  d0=$(grep -E '^RFM_PIN_DIO0=' "$keys" 2>/dev/null | tail -1 | cut -d= -f2- | tr -d '\r')
  rst=$(grep -E '^RFM_PIN_RST=' "$keys" 2>/dev/null | tail -1 | cut -d= -f2- | tr -d '\r')
  spich=$(grep -E '^RFM_SPI_CHANNEL=' "$keys" 2>/dev/null | tail -1 | cut -d= -f2- | tr -d '\r')
  if [[ -z "${cs:-}" && -z "${d0:-}" && -z "${rst:-}" ]]; then
    return 0
  fi
  say "LoRa RFM (from keys.env): RFM_SPI_CHANNEL=${spich:-?} RFM_PIN_CS=${cs:-?} RFM_PIN_DIO0=${d0:-?} RFM_PIN_RST=${rst:-?}"
  say "  SPI0: BCM 9=MISO, 10=MOSI, 11=SCLK; kernel CE0=GPIO8 CE1=GPIO7 — your radio NSS is GPIO${cs:-?}, not CE0/CE1."
}

# Optional SPI CS for BME680 vs LoRa — same BCM number is invalid.
bme680_spi_cs_conflicts_lora() {
  local cs="$1" keys="$2"
  [[ -f "$keys" ]] || return 1
  local v
  for v in RFM_PIN_CS RFM_PIN_DIO0 RFM_PIN_RST; do
    local p
    p=$(grep -E "^${v}=" "$keys" 2>/dev/null | tail -1 | cut -d= -f2- | tr -d '\r')
    if [[ -n "$p" && "$p" == "$cs" ]]; then
      say "Conflict: BME680 SPI CS GPIO${cs} is already used by ${v} for LoRa." >&2
      return 0
    fi
  done
  return 1
}

zigbee_yaml_channel_get() {
  local f="$1"
  [[ -f "$f" ]] || return 1
  awk '/^[[:space:]]*channel:[[:space:]]*[0-9]+/{print $2; exit}' "$f"
}

zigbee_yaml_patch_channel() {
  local f="$1" ch="$2"
  if grep -qE '^[[:space:]]*channel:[[:space:]]*[0-9]+' "$f"; then
    sed -i 's/^\([[:space:]]*channel:[[:space:]]*\)[0-9][0-9]*/\1'"$ch"'/' "$f"
  else
    say "lorbee-setup: add 'channel: <11-26>' under advanced: in $f" >&2
    return 1
  fi
}

zigbee_yaml_patch_adapter() {
  local f="$1" ad="$2"
  if grep -qE '^[[:space:]]*adapter:[[:space:]]*[^[:space:]]+' "$f"; then
    sed -i -E 's/^([[:space:]]*adapter:[[:space:]]*)[^[:space:]]+/\1'"$ad"'/' "$f"
  else
    say "lorbee-setup: add 'adapter: …' under serial: in $f" >&2
    return 1
  fi
}

upsert_env_var() {
  local file="$1" key="$2" value="$3"
  [[ -f "$file" ]] || return 1
  local tmp found=0
  tmp="$(mktemp)"
  while IFS= read -r line || [[ -n "$line" ]]; do
    if [[ "$line" =~ ^${key}= ]]; then
      printf '%s=%s\n' "$key" "$value"
      found=1
    else
      printf '%s\n' "$line"
    fi
  done <"$file" >"$tmp"
  if [[ "$found" -eq 0 ]]; then
    printf '%s=%s\n' "$key" "$value" >>"$tmp"
  fi
  mv "$tmp" "$file"
}

delete_env_key() {
  local file="$1" key="$2"
  [[ -f "$file" ]] || return 1
  local tmp
  tmp="$(mktemp)"
  while IFS= read -r line || [[ -n "$line" ]]; do
    [[ "$line" =~ ^${key}= ]] && continue
    printf '%s\n' "$line"
  done <"$file" >"$tmp"
  mv "$tmp" "$file"
}

default_tz() {
  if timedatectl show -p Timezone --value 2>/dev/null | grep -qv '^$'; then
    timedatectl show -p Timezone --value 2>/dev/null
  else
    printf '%s\n' "Europe/Zagreb"
  fi
}

default_edge_id() {
  local h
  h="$(hostname -s 2>/dev/null | tr '[:upper:]' '[:lower:]' | sed 's/[^a-z0-9_-]//g')"
  if [[ -z "$h" ]]; then
    printf '%s\n' "edge-pi"
  else
    printf '%s\n' "edge-$h"
  fi
}

pick_zigbee_device() {
  local byid="/dev/serial/by-id"
  local -a devs=()
  local p
  shopt -s nullglob
  for p in "$byid"/usb-* "$byid"/ttyUSB-*; do
    [[ -e "$p" ]] || continue
    devs+=("$p")
  done
  shopt -u nullglob
  if ((${#devs[@]} == 0)); then
    say "" >&2
    say "No entries under $byid — plug in the coordinator and try again." >&2
    return 1
  fi
  if ((${#devs[@]} == 1)); then
    printf '%s\n' "${devs[0]}"
    return 0
  fi
  say "Multiple serial devices found. Pick one:" >&2
  local i=1
  for p in "${devs[@]}"; do
    printf '  %d) %s\n' "$i" "$p" >&2
    ((i++)) || true
  done
  read -r -p "Enter number (1-$((${#devs[@]}))): " pick || true
  if [[ "${pick:-}" =~ ^[0-9]+$ ]] && ((pick >= 1 && pick <= ${#devs[@]})); then
    printf '%s\n' "${devs[$((pick - 1))]}"
    return 0
  fi
  return 1
}

pick_sps30_device() {
  local byid="/dev/serial/by-id"
  local -a devs=()
  local p
  shopt -s nullglob
  for p in "$byid"/usb-*Sensirion* "$byid"/usb-*SPS30* "$byid"/usb-*sps30*; do
    [[ -e "$p" ]] || continue
    devs+=("$p")
  done
  shopt -u nullglob
  if ((${#devs[@]} == 0)); then
    say "" >&2
    say "No Sensirion serial device found under $byid." >&2
    return 1
  fi
  if ((${#devs[@]} == 1)); then
    printf '%s\n' "${devs[0]}"
    return 0
  fi
  say "Multiple Sensirion serial devices found. Pick one:" >&2
  local i=1
  for p in "${devs[@]}"; do
    printf '  %d) %s\n' "$i" "$p" >&2
    ((i++)) || true
  done
  read -r -p "Enter number (1-$((${#devs[@]}))): " pick || true
  if [[ "${pick:-}" =~ ^[0-9]+$ ]] && ((pick >= 1 && pick <= ${#devs[@]})); then
    printf '%s\n' "${devs[$((pick - 1))]}"
    return 0
  fi
  return 1
}

zigbee_resolve_or_ask() {
  set +e
  local out ec
  out="$(bash "$ROOT/scripts/resolve-zigbee-serial.sh" 2>/dev/null)"
  ec=$?
  set -e
  if [[ "$ec" -eq 0 && -n "$out" ]]; then
    printf '%s\n' "$out"
    return 0
  fi
  if [[ "$ec" -eq 2 ]]; then
    pick_zigbee_device
    return $?
  fi
  return 1
}

say ""
say "=== LorBeePlugin — first-run setup ==="
say ""

ENV_FILE="$ROOT/.env"
EXAMPLE="$ROOT/.env.example"

if [[ ! -f "$EXAMPLE" ]]; then
  say "Missing .env.example — are you in the LorBeePlugin repo root?" >&2
  exit 1
fi

if [[ ! -f "$ENV_FILE" ]]; then
  if [[ "$INTERACTIVE" -eq 1 ]]; then
    if yes_no "Create .env from .env.example?" y; then
      cp "$EXAMPLE" "$ENV_FILE"
    else
      say "Aborted (no .env)." >&2
      exit 1
    fi
  else
    cp "$EXAMPLE" "$ENV_FILE"
  fi
  say "Created .env from .env.example"
elif [[ "$INTERACTIVE" -eq 1 ]]; then
  if ! yes_no ".env already exists. Update core keys (NODE_RED_USER, TZ, EDGE_ID, Zigbee path)?" y; then
    say "Leaving .env unchanged."
    SKIP_ENV=1
  fi
fi

if [[ "${SKIP_ENV:-0}" -ne 1 ]]; then
  NRU="$(id -u):$(id -g)"
  upsert_env_var "$ENV_FILE" "NODE_RED_USER" "$NRU"
  say "Set NODE_RED_USER=$NRU"

  DTZ="$(default_tz)"
  if [[ "$INTERACTIVE" -eq 1 ]]; then
    TZ_VAL="$(prompt "Timezone (TZ)" "$DTZ")"
  else
    TZ_VAL="$DTZ"
  fi
  upsert_env_var "$ENV_FILE" "TZ" "$TZ_VAL"
  say "Set TZ=$TZ_VAL"

  DEI="$(default_edge_id)"
  if [[ "$INTERACTIVE" -eq 1 ]]; then
    EID="$(prompt "Edge id (MQTT topic segment EDGE_ID)" "$DEI")"
  else
    EID="$DEI"
  fi
  upsert_env_var "$ENV_FILE" "EDGE_ID" "$EID"
  say "Set EDGE_ID=$EID"

  say ""
  if [[ "$INTERACTIVE" -eq 1 ]]; then
    say "Zigbee coordinator serial device on the host:"
    say "  1) Auto-detect (recommended if the USB stick is plugged in)"
    say "  2) Type a path manually (e.g. /dev/serial/by-id/...)"
    say "  3) Skip — use  make up-safe  later (waits and resolves at start)"
    read -r -p "Choose 1/2/3 [1]: " zchoice || true
    zchoice="${zchoice:-1}"
  else
    zchoice=1
  fi

  case "${zchoice:-1}" in
  1)
    if ZDEV="$(zigbee_resolve_or_ask)" && [[ -n "$ZDEV" ]]; then
      upsert_env_var "$ENV_FILE" "ZIGBEE_SERIAL_DEVICE" "$ZDEV"
      say "Set ZIGBEE_SERIAL_DEVICE=$ZDEV"
    else
      say "Could not auto-detect a single coordinator."
      if [[ "$INTERACTIVE" -eq 1 ]]; then
        if yes_no "Remove ZIGBEE_SERIAL_DEVICE from .env and rely on make up-safe?" y; then
          delete_env_key "$ENV_FILE" "ZIGBEE_SERIAL_DEVICE"
          say "Removed ZIGBEE_SERIAL_DEVICE — use: make up-safe"
        fi
      else
        delete_env_key "$ENV_FILE" "ZIGBEE_SERIAL_DEVICE"
        say "Removed invalid placeholder — use: make up-safe"
      fi
    fi
    ;;
  2)
    if [[ "$INTERACTIVE" -eq 1 ]]; then
      ZDEV="$(prompt "Full host path to serial device" "")"
      if [[ -n "$ZDEV" && -e "$ZDEV" ]]; then
        upsert_env_var "$ENV_FILE" "ZIGBEE_SERIAL_DEVICE" "$ZDEV"
        say "Set ZIGBEE_SERIAL_DEVICE=$ZDEV"
      else
        say "Path missing or empty — not updating ZIGBEE_SERIAL_DEVICE." >&2
      fi
    fi
    ;;
  3)
    delete_env_key "$ENV_FILE" "ZIGBEE_SERIAL_DEVICE"
    say "ZIGBEE_SERIAL_DEVICE unset — start with: make up-safe"
    ;;
  *)
    say "Unknown choice; not changing Zigbee path." >&2
    ;;
  esac
fi

ZBCFG="$ROOT/zigbee2mqtt/data/configuration.yaml"
ZBEX="$ROOT/zigbee2mqtt/data/configuration.yaml.example"
ZB_CREATED=0
if [[ -f "$ZBEX" && ! -f "$ZBCFG" ]]; then
  cp "$ZBEX" "$ZBCFG"
  ZB_CREATED=1
  say "Created zigbee2mqtt/data/configuration.yaml from example"
elif [[ ! -f "$ZBCFG" ]]; then
  say "Note: no zigbee2mqtt/data/configuration.yaml.example — create configuration.yaml yourself." >&2
fi

if [[ -f "$ZBCFG" ]]; then
  CUR_CH="$(zigbee_yaml_channel_get "$ZBCFG" || true)"
  [[ -z "${CUR_CH:-}" ]] && CUR_CH=15
  if [[ "$INTERACTIVE" -eq 1 ]]; then
    say ""
    say "Zigbee 2.4 GHz uses channels 11–26. Nearby coordinators should use different channels (see docs/zigbee2mqtt.md)."
    if [[ "$ZB_CREATED" -eq 1 ]]; then
      if yes_no "Set Zigbee channel in configuration.yaml now?" y; then
        NEW_CH="$(prompt_int "Zigbee channel (11–26)" 11 26 "$CUR_CH")"
        zigbee_yaml_patch_channel "$ZBCFG" "$NEW_CH"
        say "Set advanced.channel=$NEW_CH"
      fi
    else
      if yes_no "Change Zigbee channel? (can disrupt an existing paired network)" n; then
        NEW_CH="$(prompt_int "Zigbee channel (11–26)" 11 26 "$CUR_CH")"
        zigbee_yaml_patch_channel "$ZBCFG" "$NEW_CH"
        say "Set advanced.channel=$NEW_CH"
      fi
    fi
    if yes_no "Change coordinator adapter (serial.adapter)? Most Sonoff TI sticks use zstack." n; then
      say "  1) zstack — TI Z-Stack / Sonoff ZBDongle-P, CC2652, zzh, …"
      say "  2) ember — Silicon Labs / Sonoff ZBDongle-E, EFR32, …"
      say "  3) deconz — ConBee / RaspBee"
      say "  4) zigate"
      read -r -p "Choice [1]: " achoice || true
      case "${achoice:-1}" in
      2) zigbee_yaml_patch_adapter "$ZBCFG" "ember" && say "Set serial.adapter=ember" ;;
      3) zigbee_yaml_patch_adapter "$ZBCFG" "deconz" && say "Set serial.adapter=deconz" ;;
      4) zigbee_yaml_patch_adapter "$ZBCFG" "zigate" && say "Set serial.adapter=zigate" ;;
      *) zigbee_yaml_patch_adapter "$ZBCFG" "zstack" && say "Set serial.adapter=zstack" ;;
      esac
    fi
  else
    if [[ -n "${LORBEE_ZIGBEE_CHANNEL:-}" ]]; then
      if [[ "${LORBEE_ZIGBEE_CHANNEL}" =~ ^[0-9]+$ ]] && ((LORBEE_ZIGBEE_CHANNEL >= 11 && LORBEE_ZIGBEE_CHANNEL <= 26)); then
        zigbee_yaml_patch_channel "$ZBCFG" "${LORBEE_ZIGBEE_CHANNEL}"
        say "Set advanced.channel=${LORBEE_ZIGBEE_CHANNEL} (LORBEE_ZIGBEE_CHANNEL)"
      else
        say "lorbee-setup: LORBEE_ZIGBEE_CHANNEL must be 11–26 — ignored." >&2
      fi
    fi
    if [[ -n "${LORBEE_ZIGBEE_ADAPTER:-}" ]]; then
      case "${LORBEE_ZIGBEE_ADAPTER}" in
      zstack | ember | deconz | zigate)
        zigbee_yaml_patch_adapter "$ZBCFG" "${LORBEE_ZIGBEE_ADAPTER}"
        say "Set serial.adapter=${LORBEE_ZIGBEE_ADAPTER} (LORBEE_ZIGBEE_ADAPTER)"
        ;;
      *)
        say "lorbee-setup: LORBEE_ZIGBEE_ADAPTER must be zstack|ember|deconz|zigate — ignored." >&2
        ;;
      esac
    fi
  fi
fi

# --- Optional LoRaWAN (RFM9x on SPI): pins + region live in keys.env (env overrides YAML) ---
KEYS_EX="$ROOT/lora/chirpstack-node/keys.env.example"
KEYS="$ROOT/lora/chirpstack-node/keys.env"
LORA_DO=0
LORA_COMPOSE=0

if [[ "$INTERACTIVE" -eq 1 ]]; then
  say ""
  if yes_no "Configure optional LoRaWAN radio (RFM9x on SPI — GPIO pins, region, uplink interval)?" n; then
    LORA_DO=1
  fi
elif [[ "$WITH_LORA" -eq 1 ]]; then
  LORA_DO=1
  LORA_COMPOSE=1
fi

if [[ "$LORA_DO" -eq 1 ]]; then
  if [[ ! -f "$KEYS_EX" ]]; then
    say "Missing lora/chirpstack-node/keys.env.example — cannot configure LoRa." >&2
  else
    say ""
    say "LoRa (RFM9x): enable SPI on the Pi first, then reboot."
    say "  DietPi:  sudo dietpi-config  →  Advanced Options  →  SPI state  →  Enable  →  Reboot"
    say "  Details: docs/dietpi-spi.md"
    say "After reboot, run:  ls -l /dev/spidev0.*  (expect spidev0.0 and often spidev0.1)"
    say ""
    say "NSS/CS for the radio must be a free GPIO (e.g. 17), never 7 or 8 (those are kernel SPI chip selects)."
    say "Each Pi is its own ChirpStack device — set OTAA keys in keys.env after you create the device in ChirpStack (lora/README.md)."
    if [[ ! -f "$KEYS" ]]; then
      cp "$KEYS_EX" "$KEYS"
      say "Created lora/chirpstack-node/keys.env — edit LORAWAN_DEV_EUI and LORAWAN_APP_KEY to match ChirpStack."
    fi

    LCFG="$ROOT/lora/chirpstack-node/config.yaml"
    LCFGEX="$ROOT/lora/chirpstack-node/config.example.yaml"
    if [[ -f "$LCFGEX" && ! -f "$LCFG" ]]; then
      cp "$LCFGEX" "$LCFG"
      say "Created lora/chirpstack-node/config.yaml from config.example.yaml (edit payload / devices for your site)."
    fi

    SPI_CH=1
    CS=17
    D0=22
    RST=25
    REG="EU868"
    SB=0
    UPLINK=300

    if [[ "$INTERACTIVE" -eq 1 ]]; then
      say ""
      say "SPI device for RadioLib (which /dev/spidev0.X to open):"
      say "  0 = /dev/spidev0.0  (hardware CE0)   |   1 = /dev/spidev0.1  (hardware CE1) — repo default is 1"
      say "RFM chip-select is still your GPIO (e.g. 17), not CE0/CE1. If unsure, keep 1 if both nodes exist."
      SPI_CH="$(prompt_int "Enter 0 or 1" 0 1 1)"
      while true; do
        CS="$(prompt_int "NSS / CS GPIO (BCM)" 2 40 17)"
        if [[ "$CS" == "7" || "$CS" == "8" ]]; then
          say "GPIO 7 and 8 are reserved for SPI CE — choose another pin for CS." >&2
        else
          break
        fi
      done
      D0="$(prompt_int "DIO0 / G0 GPIO (BCM)" 2 40 22)"
      RST="$(prompt_int "RST GPIO (BCM)" 2 40 25)"
      REG="$(prompt "LoRaWAN region (e.g. EU868, US915)" "EU868")"
      SB="$(prompt_int "Sub-band (often 0)" 0 7 0)"
      UPLINK="$(prompt_int "Uplink interval (seconds, min 30)" 30 86400 300)"
    fi

    upsert_env_var "$KEYS" "RFM_SPI_CHANNEL" "$SPI_CH"
    upsert_env_var "$KEYS" "RFM_PIN_CS" "$CS"
    upsert_env_var "$KEYS" "RFM_PIN_DIO0" "$D0"
    upsert_env_var "$KEYS" "RFM_PIN_RST" "$RST"
    upsert_env_var "$KEYS" "LORAWAN_REGION" "$REG"
    upsert_env_var "$KEYS" "LORAWAN_SUB_BAND" "$SB"
    upsert_env_var "$KEYS" "LORAWAN_UPLINK_INTERVAL_SEC" "$UPLINK"
    say "Wrote LoRa hardware overrides in keys.env (RFM_* / LORAWAN_REGION / …)."

    if [[ "$INTERACTIVE" -eq 1 ]]; then
      say ""
      if yes_no "Set COMPOSE_PROFILES=lora in .env so docker compose starts the LoRa container with the main stack?" y; then
        LORA_COMPOSE=1
      fi
    fi

    if [[ "$LORA_COMPOSE" -eq 1 ]]; then
      CUR_PROFILES=""
      if grep -qE '^COMPOSE_PROFILES=' "$ENV_FILE" 2>/dev/null; then
        CUR_PROFILES="$(grep -E '^COMPOSE_PROFILES=' "$ENV_FILE" | tail -1 | cut -d= -f2-)"
      fi
      if [[ "$CUR_PROFILES" != *lora* ]]; then
        if [[ -n "$CUR_PROFILES" ]]; then
          upsert_env_var "$ENV_FILE" "COMPOSE_PROFILES" "${CUR_PROFILES},lora"
        else
          upsert_env_var "$ENV_FILE" "COMPOSE_PROFILES" "lora"
        fi
      fi
      say "Added lora to COMPOSE_PROFILES in .env"
    fi
  fi
fi

# --- Optional SPS30 particulate matter sensor (UART/USB) ---
SPS30_DO=0
SPS30_COMPOSE=0

if [[ "$INTERACTIVE" -eq 1 ]]; then
  say ""
  if yes_no "Configure optional SPS30 particulate matter sensor (USB/UART)?" n; then
    SPS30_DO=1
  fi
elif [[ "$WITH_SPS30" -eq 1 ]]; then
  SPS30_DO=1
  SPS30_COMPOSE=1
fi

if [[ "$SPS30_DO" -eq 1 ]]; then
  say ""
  say "SPS30: connect via the Sensirion USB-UART cable. The sensor should appear under /dev/serial/by-id/."

  SPS30_DEV=""
  if [[ "$INTERACTIVE" -eq 1 ]]; then
    say "  1) Auto-detect Sensirion device (recommended if cable is plugged in)"
    say "  2) Type a path manually (e.g. /dev/serial/by-id/usb-Sensirion_...)"
    say "  3) Skip — set SPS30_SERIAL_DEVICE in .env later"
    read -r -p "Choose 1/2/3 [1]: " sps30choice || true
    sps30choice="${sps30choice:-1}"
  else
    sps30choice=1
  fi

  case "${sps30choice:-1}" in
  1)
    if SPS30_DEV="$(pick_sps30_device)" && [[ -n "$SPS30_DEV" ]]; then
      upsert_env_var "$ENV_FILE" "SPS30_SERIAL_DEVICE" "$SPS30_DEV"
      say "Set SPS30_SERIAL_DEVICE=$SPS30_DEV"
    else
      say "Could not auto-detect SPS30. Set SPS30_SERIAL_DEVICE in .env manually."
    fi
    ;;
  2)
    if [[ "$INTERACTIVE" -eq 1 ]]; then
      SPS30_DEV="$(prompt "Full host path to SPS30 serial device" "")"
      if [[ -n "$SPS30_DEV" && -e "$SPS30_DEV" ]]; then
        upsert_env_var "$ENV_FILE" "SPS30_SERIAL_DEVICE" "$SPS30_DEV"
        say "Set SPS30_SERIAL_DEVICE=$SPS30_DEV"
      else
        say "Path missing or empty — not updating SPS30_SERIAL_DEVICE." >&2
      fi
    fi
    ;;
  3)
    say "Skipped — set SPS30_SERIAL_DEVICE in .env when ready."
    ;;
  esac

  if [[ "$INTERACTIVE" -eq 1 ]]; then
    SPS30_INT="$(prompt_int "SPS30 read interval (seconds, min 10)" 10 3600 30)"
    upsert_env_var "$ENV_FILE" "SPS30_INTERVAL" "$SPS30_INT"
    say "Set SPS30_INTERVAL=$SPS30_INT"
  fi

  if [[ "$INTERACTIVE" -eq 1 ]]; then
    say ""
    if yes_no "Add sps30 to COMPOSE_PROFILES so the SPS30 container starts with the main stack?" y; then
      SPS30_COMPOSE=1
    fi
  fi

  if [[ "$SPS30_COMPOSE" -eq 1 ]]; then
    CUR_PROFILES=""
    if grep -qE '^COMPOSE_PROFILES=' "$ENV_FILE" 2>/dev/null; then
      CUR_PROFILES="$(grep -E '^COMPOSE_PROFILES=' "$ENV_FILE" | tail -1 | cut -d= -f2-)"
    fi
    if [[ "$CUR_PROFILES" != *sps30* ]]; then
      if [[ -n "$CUR_PROFILES" ]]; then
        upsert_env_var "$ENV_FILE" "COMPOSE_PROFILES" "${CUR_PROFILES},sps30"
      else
        upsert_env_var "$ENV_FILE" "COMPOSE_PROFILES" "sps30"
      fi
    fi
    say "Added sps30 to COMPOSE_PROFILES in .env"
  fi

  say ""
  say "SPS30: build once on the Pi:"
  say "  docker compose --profile sps30 build sps30-collector"
  say "  docker compose --profile sps30 up -d sps30-collector"
fi

# --- Optional BME680 (I2C) — pin-aware vs LoRa in keys.env ---
BME680_DO=0
BME680_COMPOSE=0
BME680_CONFIGURED=0

if [[ "$INTERACTIVE" -eq 1 ]]; then
  say ""
  if yes_no "Configure optional BME680 environmental sensor (I2C — temp / humidity / pressure / gas resistance)?" n; then
    BME680_DO=1
  fi
elif [[ "$WITH_BME680" -eq 1 ]]; then
  BME680_DO=1
  BME680_COMPOSE=1
fi

if [[ "$BME680_DO" -eq 1 ]]; then
  say ""
  say "BME680: this stack's collector (bme680-collector) supports I2C only — Pimoroni bme680 + /dev/i2c-*."
  say "  SPI would share the SPI0 bus and GPIO namespace with the LoRa RFM9x; use I2C here (SDA/SCL on BCM2/3)."
  lora_rf_pins_summary "$KEYS"
  say ""
  say "Seven pads on many breakouts: 3V3/VIN, GND, SCK, SDI (SDA), CS, SDO, PS — not all are used in I2C mode."
  say "  I2C wiring: SDA→BCM2 (pin 3), SCL→BCM3 (pin 5), 3V3 (pin 1 or 17), GND (e.g. pin 6)."
  say "  CS must be tied high (to 3V3) for I2C; SDO→GND = address 0x76, SDO→3V3 = 0x77."
  say "Default 100 kHz I2C is fine for BME680; you can raise to 400 kHz in firmware if you need faster reads."
  say ""

  B680_BUS=1
  B680_ADDR="0x76"
  B680_INT=60

  if [[ "$INTERACTIVE" -eq 1 ]]; then
    say "Link type for this wizard (collector always uses I2C):"
    say "  1) I2C — recommended with LoRa on SPI (no GPIO overlap with RFM NSS/DIO0/RST)"
    say "  2) SPI only on the bench — not supported by bme680-collector (choose 1 for this Pi)"
    read -r -p "Choose 1/2 [1]: " b680link || true
    b680link="${b680link:-1}"
    if [[ "$b680link" == "2" ]]; then
      say ""
      say "SPI + LoRa on one Pi: both would contend for SPI0 (MOSI/MISO/SCK) and you must unique CS lines;"
      say "this image does not ship a SPI BME680 reader — use I2C (wiring above) or a second host."
      if ! yes_no "Proceed with I2C settings in .env anyway (you will wire I2C on the board)?" y; then
        BME680_DO=0
      fi
    fi
  fi

  if [[ "$BME680_DO" -eq 1 ]]; then
    if [[ "$INTERACTIVE" -eq 1 ]]; then
      B680_BUS="$(prompt_int "I2C bus number (Raspberry Pi: almost always 1 → /dev/i2c-1)" 0 20 1)"
      say "I2C address: 0x76 (SDO→GND) or 0x77 (SDO→3V3) on Bosch-compatible breakouts."
      read -r -p "Hex address [0x76]: " b680a || true
      b680a="${b680a:-0x76}"
      if [[ "$b680a" == "0x77" ]]; then
        B680_ADDR="0x77"
      else
        B680_ADDR="0x76"
      fi
      B680_INT="$(prompt_int "BME680 read interval (seconds, min 10)" 10 3600 60)"
    fi

    upsert_env_var "$ENV_FILE" "BME680_PROTOCOL" "i2c"
    upsert_env_var "$ENV_FILE" "BME680_I2C_BUS" "$B680_BUS"
    upsert_env_var "$ENV_FILE" "BME680_I2C_DEVICE" "/dev/i2c-${B680_BUS}"
    upsert_env_var "$ENV_FILE" "BME680_I2C_ADDR" "$B680_ADDR"
    upsert_env_var "$ENV_FILE" "BME680_INTERVAL" "$B680_INT"
    upsert_env_var "$ENV_FILE" "BME680_MQTT_TOPIC" "bme680/raw"
    say "Wrote BME680 I2C settings to .env (BME680_*)."
    BME680_CONFIGURED=1

    if [[ "$INTERACTIVE" -eq 1 ]]; then
      say ""
      say "Optional: document SPI wiring for a second board (not used by Docker). LoRa CS/DIO/RST must stay unique."
      if yes_no "Record SPI chip-select GPIO for your notes (checks conflict with LoRa keys.env)?" n; then
        B680_SPI_CS="$(prompt_int "BME680 SPI CS GPIO (BCM), if you ever wire SPI — e.g. 8" 2 40 8)"
        if bme680_spi_cs_conflicts_lora "$B680_SPI_CS" "$KEYS"; then
          say "Fix LoRa or BME680 CS in hardware before using both on SPI0." >&2
        else
          say "No clash with RFM_PIN_* in keys.env for GPIO${B680_SPI_CS} (still: collector uses I2C only)."
        fi
      fi
    fi

    if [[ "$INTERACTIVE" -eq 1 ]]; then
      say ""
      if yes_no "Add bme680 to COMPOSE_PROFILES so the collector starts with the main stack?" y; then
        BME680_COMPOSE=1
      fi
    fi

    if [[ "$BME680_COMPOSE" -eq 1 ]]; then
      CUR_PROFILES=""
      if grep -qE '^COMPOSE_PROFILES=' "$ENV_FILE" 2>/dev/null; then
        CUR_PROFILES="$(grep -E '^COMPOSE_PROFILES=' "$ENV_FILE" | tail -1 | cut -d= -f2-)"
      fi
      if [[ "$CUR_PROFILES" != *bme680* ]]; then
        if [[ -n "$CUR_PROFILES" ]]; then
          upsert_env_var "$ENV_FILE" "COMPOSE_PROFILES" "${CUR_PROFILES},bme680"
        else
          upsert_env_var "$ENV_FILE" "COMPOSE_PROFILES" "bme680"
        fi
      fi
      say "Added bme680 to COMPOSE_PROFILES in .env"
    fi

    say ""
    say "BME680: build once on the Pi:"
    say "  docker compose --profile bme680 build bme680-collector"
    say "  docker compose --profile bme680 up -d bme680-collector"
  fi
fi

bash "$ROOT/scripts/ensure-data-dirs.sh"

say ""
say "=== Setup done ==="
say "Next:"
say "  docker compose pull   # optional: refresh images (install docker-compose-plugin if missing — README)"
say "  bash scripts/stack-after-boot.sh   # first stack start (Zigbee USB wait + optional LoRa if profile set)"
say "  sudo bash scripts/install-zigbee-boot-timer.sh   # recommended once on the Pi"
if [[ "$LORA_DO" -eq 1 ]]; then
  say ""
  say "LoRa: edit keys.env (OTAA secrets), then build once on the Pi:"
  say "  docker compose --profile lora build chirpstack-lora-node"
  say "  docker compose --profile lora up -d chirpstack-lora-node"
  say "Docs: lora/README.md"
fi
if [[ "$SPS30_DO" -eq 1 ]]; then
  say ""
  say "SPS30: build and start:"
  say "  docker compose --profile sps30 build sps30-collector"
  say "  docker compose --profile sps30 up -d sps30-collector"
fi
if [[ "${BME680_CONFIGURED:-0}" -eq 1 ]]; then
  say ""
  say "BME680: build and start:"
  say "  docker compose --profile bme680 build bme680-collector"
  say "  docker compose --profile bme680 up -d bme680-collector"
fi
say ""
say "Docs: README.md, docs/configuration-map.md"
say ""
