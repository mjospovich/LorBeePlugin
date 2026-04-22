/*
 * LoRaWAN Class-A OTAA node for ChirpStack (and compatible LNS).
 * Radio: SX1276/RFM9x on Raspberry Pi SPI (see config.yaml for pins).
 * Stack: RadioLib (https://github.com/jgromes/RadioLib) + PiHal (lgpio).
 */

#include "config.hpp"
#include "downlink_handler.hpp"
#include "lora_state.hpp"
#include "payload_builder.hpp"
#include "snapshot.hpp"

#include <RadioLib.h>
#include <hal/RPi/PiHal.h>

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include <strings.h>

static const char* env_get(const char* k, const char* dflt) {
  const char* v = std::getenv(k);
  return (v && v[0]) ? v : dflt;
}

/// Delay between OTAA join attempts (avoids Docker restart spam on ERR_NO_JOIN_ACCEPT).
static uint32_t join_retry_ms_from_env() {
  unsigned long sec = 60;
  if(const char* s = std::getenv("LORAWAN_JOIN_RETRY_SEC")) {
    if(s[0]) { sec = std::strtoul(s, nullptr, 10); }
  }
  if(sec < 10) { sec = 10; }
  if(sec > 3600) { sec = 3600; }
  return sec * 1000UL;
}

/// After join, wait before first uplink so Node-RED can merge Zigbee MQTT into snapshot (default 30 s).
static uint32_t startup_grace_ms_from_env() {
  unsigned long sec = 30;
  if(const char* s = std::getenv("LORAWAN_STARTUP_GRACE_SEC")) {
    if(s[0]) { sec = std::strtoul(s, nullptr, 10); }
  }
  if(sec > 600) { sec = 600; }
  return sec * 1000UL;
}

/// If packed uplink skipped (missing snapshot file or device), retry sooner than full uplink interval.
static uint32_t snapshot_retry_ms_from_env() {
  unsigned long sec = 30;
  if(const char* s = std::getenv("LORAWAN_SNAPSHOT_RETRY_SEC")) {
    if(s[0]) { sec = std::strtoul(s, nullptr, 10); }
  }
  if(sec < 5) { sec = 5; }
  if(sec > 120) { sec = 120; }
  return sec * 1000UL;
}

static int hex_val(char c) {
  if(c >= '0' && c <= '9') { return c - '0'; }
  if(c >= 'a' && c <= 'f') { return 10 + c - 'a'; }
  if(c >= 'A' && c <= 'F') { return 10 + c - 'A'; }
  return -1;
}

static bool parse_hex_u64(const char* s, uint64_t* out) {
  if(!s) { return false; }
  while(*s == ' ' || *s == '\t') { ++s; }
  if(s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { s += 2; }
  if(std::strlen(s) != 16) { return false; }
  uint64_t v = 0;
  for(size_t i = 0; i < 16; i++) {
    int h = hex_val(s[i]);
    if(h < 0) { return false; }
    v = (v << 4) | (uint64_t)h;
  }
  *out = v;
  return true;
}

static bool parse_hex_key16(const char* s, uint8_t key[16]) {
  if(!s) { return false; }
  while(*s == ' ' || *s == '\t') { ++s; }
  if(s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { s += 2; }
  if(std::strlen(s) != 32) { return false; }
  for(size_t i = 0; i < 16; i++) {
    int a = hex_val(s[2 * i]);
    int b = hex_val(s[2 * i + 1]);
    if(a < 0 || b < 0) { return false; }
    key[i] = (uint8_t)((a << 4) | b);
  }
  return true;
}

static const LoRaWANBand_t* band_from_region_name(const char* name) {
  if(!name || strcasecmp(name, "EU868") == 0) { return &EU868; }
  if(strcasecmp(name, "US915") == 0) { return &US915; }
  if(strcasecmp(name, "AU915") == 0) { return &AU915; }
  if(strcasecmp(name, "AS923") == 0) { return &AS923; }
  if(strcasecmp(name, "AS923_2") == 0) { return &AS923_2; }
  if(strcasecmp(name, "AS923_3") == 0) { return &AS923_3; }
  if(strcasecmp(name, "AS923_4") == 0) { return &AS923_4; }
  if(strcasecmp(name, "IN865") == 0) { return &IN865; }
  if(strcasecmp(name, "KR920") == 0) { return &KR920; }
  if(strcasecmp(name, "CN470") == 0) { return &CN470; }
  std::fprintf(stderr, "Unknown region '%s', using EU868\n", name);
  return &EU868;
}

static const char* state_str(int16_t st) {
  switch(st) {
    case RADIOLIB_ERR_NONE: return "ERR_NONE";
    case RADIOLIB_ERR_CHIP_NOT_FOUND: return "ERR_CHIP_NOT_FOUND";
    case RADIOLIB_ERR_PACKET_TOO_LONG: return "ERR_PACKET_TOO_LONG";
    case RADIOLIB_ERR_RX_TIMEOUT: return "ERR_RX_TIMEOUT";
    case RADIOLIB_ERR_CRC_MISMATCH: return "ERR_CRC_MISMATCH";
    case RADIOLIB_ERR_MIC_MISMATCH: return "ERR_MIC_MISMATCH";
    case RADIOLIB_ERR_NETWORK_NOT_JOINED: return "ERR_NETWORK_NOT_JOINED";
    case RADIOLIB_ERR_NO_JOIN_ACCEPT: return "ERR_NO_JOIN_ACCEPT";
    case RADIOLIB_ERR_JOIN_NONCE_INVALID: return "ERR_JOIN_NONCE_INVALID";
    case RADIOLIB_LORAWAN_NEW_SESSION: return "LORAWAN_NEW_SESSION";
    case RADIOLIB_LORAWAN_SESSION_RESTORED: return "LORAWAN_SESSION_RESTORED";
    default: return "see RadioLib status_codes";
  }
}

int main() {
  uint64_t joinEUI = 0;
  uint64_t devEUI = 0;
  uint8_t appKey[16] = { 0 };
  uint8_t nwkKey[16] = { 0 };

  const char* dev_s = std::getenv("LORAWAN_DEV_EUI");
  const char* app_s = std::getenv("LORAWAN_APP_KEY");
  if(!dev_s || !parse_hex_u64(dev_s, &devEUI)) {
    std::fprintf(stderr, "Set LORAWAN_DEV_EUI to 16 hex digits (DevEUI).\n");
    return 1;
  }
  if(!app_s || !parse_hex_key16(app_s, appKey)) {
    std::fprintf(stderr, "Set LORAWAN_APP_KEY to 32 hex digits (AppKey).\n");
    return 1;
  }

  const char* join_s = env_get("LORAWAN_JOIN_EUI", "0000000000000000");
  if(!parse_hex_u64(join_s, &joinEUI)) {
    std::fprintf(stderr, "LORAWAN_JOIN_EUI must be 16 hex digits.\n");
    return 1;
  }

  const char* nwk_s = std::getenv("LORAWAN_NWK_KEY");
  if(nwk_s && nwk_s[0]) {
    if(!parse_hex_key16(nwk_s, nwkKey)) {
      std::fprintf(stderr, "LORAWAN_NWK_KEY must be 32 hex digits.\n");
      return 1;
    }
  } else {
    std::memcpy(nwkKey, appKey, 16);
  }

  AppConfig cfg;
  std::string err;
  std::string cfg_path = resolve_lora_config_path();
  if(cfg_path.empty()) {
    if(!load_app_config(nullptr, cfg, err)) {
      std::fprintf(stderr, "%s\n", err.c_str());
      return 1;
    }
    std::fprintf(stderr, "[lorawan-node] config: (defaults; no config.yaml found)\n");
  } else {
    if(!load_app_config(cfg_path.c_str(), cfg, err)) {
      std::fprintf(stderr, "%s\n", err.c_str());
      return 1;
    }
    std::fprintf(stderr, "[lorawan-node] config: %s\n", cfg_path.c_str());
  }
  apply_env_overrides(cfg);
  {
    std::string verr;
    if(!app_config_valid(cfg, verr)) {
      std::fprintf(stderr, "%s\n", verr.c_str());
      return 1;
    }
  }

  const LoRaWANBand_t* band = band_from_region_name(cfg.lorawan.region.c_str());

  RuntimeState rt_state;
  rt_state.uplink_interval_sec = cfg.lorawan.uplink_interval_sec;
  if(const char* e = std::getenv("DOWNLINK_CMD_DIR")) {
    if(e[0]) { rt_state.command_out_dir = e; }
  }
  if(const char* e = std::getenv("DOWNLINK_MQTT_HOST")) {
    if(e[0]) { rt_state.mqtt_host = e; }
  }
  if(const char* e = std::getenv("DOWNLINK_MQTT_PORT")) {
    if(e[0]) { rt_state.mqtt_port = (uint16_t)std::atoi(e); }
  }

  std::fprintf(stderr,
      "[lorawan-node] SPI CE%u CS=GPIO%u DIO0=GPIO%u RST=GPIO%u region=%s uplink=%us\n",
      (unsigned)cfg.hw.spi_channel, (unsigned)cfg.hw.pin_cs, (unsigned)cfg.hw.pin_dio0,
      (unsigned)cfg.hw.pin_rst, cfg.lorawan.region.c_str(),
      (unsigned)rt_state.uplink_interval_sec);

  PiHal hal(cfg.hw.spi_channel);
  Module mod(&hal, cfg.hw.pin_cs, cfg.hw.pin_dio0, cfg.hw.pin_rst, RADIOLIB_NC);
  SX1276 radio(&mod);
  LoRaWANNode node(&radio, band, cfg.lorawan.sub_band);

  std::fprintf(stderr, "[lorawan-node] radio.begin ...\n");
  int16_t st = radio.begin();
  if(st != RADIOLIB_ERR_NONE) {
    std::fprintf(stderr, "radio.begin failed: %d (%s)\n", (int)st, state_str(st));
    return 1;
  }

  const uint32_t join_retry_ms = join_retry_ms_from_env();
  st = node.beginOTAA(joinEUI, devEUI, nwkKey, appKey);
  if(st != RADIOLIB_ERR_NONE) {
    std::fprintf(stderr, "beginOTAA failed: %d (%s)\n", (int)st, state_str(st));
    return 1;
  }
  lorawan_try_restore(node);

  for(;;) {
    std::fprintf(stderr, "[lorawan-node] OTAA join / session restore ...\n");
    st = node.activateOTAA();
    lorawan_save_after_join(node);
    if(st == RADIOLIB_LORAWAN_NEW_SESSION || st == RADIOLIB_LORAWAN_SESSION_RESTORED) { break; }
    std::fprintf(stderr, "activateOTAA failed: %d (%s)\n", (int)st, state_str(st));
    std::fprintf(stderr,
        "Hint: match DevEUI/AppKey/JoinEUI in ChirpStack; device profile allows join; gateway "
        "online and in range; region EU868; reset join nonces if re-provisioning.\n");
    std::fprintf(stderr, "[lorawan-node] retry join in %u s (set LORAWAN_JOIN_RETRY_SEC)\n",
        (unsigned)(join_retry_ms / 1000));
    hal.delay(join_retry_ms);
  }

  std::fprintf(stderr,
      "[lorawan-node] joined; uplinks every %u s from snapshot %s (payload format=%s, "
      "downlink fPort=%u)\n",
      (unsigned)rt_state.uplink_interval_sec, cfg.snapshot.path.c_str(),
      cfg.payload.format == PayloadFormat::Packed ? "packed" : "legacy",
      (unsigned)DOWNLINK_CMD_FPORT);

  {
    uint32_t grace_ms = startup_grace_ms_from_env();
    if(grace_ms > 0) {
      std::fprintf(stderr,
          "[lorawan-node] startup grace %u s (wait for Node-RED snapshot); "
          "LORAWAN_STARTUP_GRACE_SEC=0 to disable\n",
          (unsigned)(grace_ms / 1000UL));
      hal.delay(grace_ms);
    }
  }

  const uint32_t snap_retry_ms = snapshot_retry_ms_from_env();
  for(;;) {
    int16_t st = RADIOLIB_ERR_NONE;
    bool skipped_uplink = false;

    uint8_t dl_buf[256];
    size_t dl_len = 0;
    LoRaWANEvent_t evt_down = {};

    if(cfg.payload.format == PayloadFormat::Packed) {
      nlohmann::json snapj;
      std::string jerr;
      if(!load_snapshot_json(cfg.snapshot.path, snapj, jerr)) {
        std::fprintf(stderr, "[lorawan-node] snapshot: %s (skip uplink)\n", jerr.c_str());
        skipped_uplink = true;
      } else {
        std::vector<uint8_t> payload;
        std::string perr;
        if(!build_packed_uplink_payload(cfg, snapj, payload, perr)) {
          std::fprintf(stderr, "[lorawan-node] packed payload: %s (skip uplink)\n", perr.c_str());
          skipped_uplink = true;
        } else {
          if(rt_state.last_result.valid && payload.size() + 2 <= cfg.payload.max_bytes
              && payload.size() >= 2) {
            payload[1] |= FLAG_HAS_ACK;
            uint8_t ack_status = rt_state.last_result.success ? 0x01 : 0x00;
            payload.insert(payload.begin() + 2,
                {rt_state.last_result.cmd_id, ack_status});
            std::fprintf(stderr, "[lorawan-node] uplink ACK: cmd=0x%02x status=%u\n",
                rt_state.last_result.cmd_id, (unsigned)ack_status);
            rt_state.last_result.valid = false;
          }
          std::fprintf(stderr, "[lorawan-node] uplink application payload: %zu bytes\n", payload.size());
          st = node.sendReceive(payload.data(), (size_t)payload.size(), 1,
              dl_buf, &dl_len, false, nullptr, &evt_down);
        }
      }
    } else {
      uint8_t payload[4];
      SnapshotReadout snap = read_snapshot(cfg);
      int16_t tcenti;
      uint16_t hcenti;
      if(snap.ok) {
        tcenti = (int16_t)std::lround(snap.temperature * 100.0);
        hcenti = (uint16_t)std::lround(snap.humidity * 100.0);
        std::fprintf(stderr, "[lorawan-node] snapshot: temp=%.2f C hum=%.2f %%\n", snap.temperature,
            snap.humidity);
      } else {
        tcenti = (int16_t)0x7fff;
        hcenti = (uint16_t)0xffff;
        std::fprintf(stderr, "[lorawan-node] snapshot unavailable: %s (sending invalid markers)\n",
            snap.error.c_str());
      }
      payload[0] = (uint8_t)((tcenti >> 8) & 0xff);
      payload[1] = (uint8_t)(tcenti & 0xff);
      payload[2] = (uint8_t)((hcenti >> 8) & 0xff);
      payload[3] = (uint8_t)(hcenti & 0xff);
      std::fprintf(stderr, "[lorawan-node] uplink application payload: %zu bytes\n", sizeof(payload));
      st = node.sendReceive(payload, sizeof(payload), 1,
          dl_buf, &dl_len, false, nullptr, &evt_down);
    }
    if(st < RADIOLIB_ERR_NONE) {
      std::fprintf(stderr, "sendReceive error: %d (%s)\n", (int)st, state_str(st));
    } else if(st > 0) {
      std::fprintf(stderr, "[lorawan-node] downlink: RX%d %zu bytes fPort=%u\n",
          (int)st, dl_len, (unsigned)evt_down.fPort);
      if(dl_len > 0) {
        handle_downlink(dl_buf, dl_len, evt_down.fPort, rt_state);
      }
    }
    lorawan_save_after_uplink(node);

    uint32_t wait_ms = skipped_uplink ? snap_retry_ms : (rt_state.uplink_interval_sec * 1000UL);
    if(skipped_uplink) {
      std::fprintf(stderr,
          "[lorawan-node] retry snapshot in %u s (LORAWAN_SNAPSHOT_RETRY_SEC)\n",
          (unsigned)(wait_ms / 1000UL));
    }
    hal.delay(wait_ms);
  }
}
