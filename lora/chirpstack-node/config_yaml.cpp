#include "config.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>

// ---------------------------------------------------------------------------
// Sensor Type Registry — single source of truth for encoder + decoder.
// ---------------------------------------------------------------------------

static std::vector<SensorTypeDef> build_registry() {
  std::vector<SensorTypeDef> r;
  r.push_back({SensorType::Climate,    "climate",     {"temperature", "humidity"}});
  r.push_back({SensorType::Motion,     "motion",      {"occupancy", "illumination"}});
  r.push_back({SensorType::Contact,    "contact",     {"contact"}});
  r.push_back({SensorType::AirQuality, "air_quality", {"pm1_0", "pm2_5", "pm4_0", "pm10"}});
  return r;
}

const std::vector<SensorTypeDef>& sensor_type_registry() {
  static const std::vector<SensorTypeDef> reg = build_registry();
  return reg;
}

const SensorTypeDef* sensor_type_by_name(const std::string& name) {
  for(const auto& td : sensor_type_registry()) {
    if(name == td.name) { return &td; }
  }
  return nullptr;
}

const SensorTypeDef* sensor_type_by_id(uint8_t id) {
  for(const auto& td : sensor_type_registry()) {
    if(static_cast<uint8_t>(td.type) == id) { return &td; }
  }
  return nullptr;
}

// ---------------------------------------------------------------------------
// Config validation / defaults / loading
// ---------------------------------------------------------------------------

bool app_config_valid(const AppConfig& cfg, std::string& err) {
  if(cfg.payload.format == PayloadFormat::Packed && cfg.payload.entries.empty()) {
    err = "payload.format packed requires non-empty payload.entries in config.yaml";
    return false;
  }
  return true;
}

static void defaults(AppConfig& c) {
  c.hw = {};
  c.hw.spi_channel = 1;
  c.hw.pin_cs = 17;
  c.hw.pin_dio0 = 22;
  c.hw.pin_rst = 25;
  c.lorawan = {};
  c.lorawan.region = "EU868";
  c.lorawan.sub_band = 0;
  c.lorawan.uplink_interval_sec = 300;
  c.snapshot = {};
  c.snapshot.path = "/data/snapshot/latest.json";
  c.snapshot.device_ieee = "";
  c.payload = {};
  c.payload.format = PayloadFormat::Legacy;
  c.payload.include_status = false;
  c.payload.max_bytes = 222;
  c.payload.entries.clear();
}

static bool file_readable(const char* path) {
  if(!path || !path[0]) { return false; }
  std::ifstream f(path);
  return f.good();
}

std::string resolve_lora_config_path() {
  if(const char* e = std::getenv("LORA_CONFIG")) {
    if(e[0]) { return std::string(e); }
  }
  if(file_readable("config.yaml")) { return "config.yaml"; }
  if(file_readable("/etc/lora/config.yaml")) { return "/etc/lora/config.yaml"; }
  return {};
}

static bool parse_entry_type(const YAML::Node& en, PayloadEntry& pe, std::string& err) {
  bool has_type = en["type"].IsDefined();
  bool has_fields = en["fields"] && en["fields"].IsSequence();

  if(has_type) {
    std::string tname = en["type"].as<std::string>();
    const SensorTypeDef* td = sensor_type_by_name(tname);
    if(!td) {
      err = "payload.entries: unknown sensor type '" + tname +
            "'. Known types:";
      for(const auto& r : sensor_type_registry()) {
        err += " ";
        err += r.name;
      }
      return false;
    }
    pe.sensor_type = td->type;
    pe.fields = td->fields;
    if(has_fields) {
      err = "payload.entries: entry has both 'type' and 'fields'; "
            "use 'type' only (fields are derived from the sensor type registry)";
      return false;
    }
    return true;
  }

  if(has_fields) {
    for(const auto& fe : en["fields"]) {
      pe.fields.push_back(fe.as<std::string>());
    }
    // Try to match fields to a known sensor type for backward compat.
    for(const auto& td : sensor_type_registry()) {
      if(td.fields == pe.fields) {
        pe.sensor_type = td.type;
        return true;
      }
    }
    err = "payload.entries: 'fields' list does not match any known sensor type. "
          "Use 'type: <name>' instead. Known types:";
    for(const auto& r : sensor_type_registry()) {
      err += " " + std::string(r.name) + "=[";
      for(size_t i = 0; i < r.fields.size(); i++) {
        if(i) { err += ","; }
        err += r.fields[i];
      }
      err += "]";
    }
    return false;
  }

  err = "payload.entries: each entry must have a 'type' field (e.g. type: climate)";
  return false;
}

bool load_app_config(const char* config_path, AppConfig& out, std::string& err) {
  defaults(out);
  if(!config_path || !config_path[0]) {
    return true;
  }
  if(!file_readable(config_path)) {
    err = std::string("config file not found or unreadable: ") + config_path;
    return false;
  }

  try {
    YAML::Node root = YAML::LoadFile(config_path);
    if(root["hardware"]) {
      const YAML::Node hw = root["hardware"];
      if(hw["spi_channel"].IsDefined()) {
        out.hw.spi_channel = (uint8_t)hw["spi_channel"].as<int>();
      }
      if(hw["pins"]) {
        const YAML::Node p = hw["pins"];
        if(p["cs"].IsDefined()) { out.hw.pin_cs = (uint32_t)p["cs"].as<int>(); }
        if(p["dio0"].IsDefined()) { out.hw.pin_dio0 = (uint32_t)p["dio0"].as<int>(); }
        if(p["rst"].IsDefined()) { out.hw.pin_rst = (uint32_t)p["rst"].as<int>(); }
      }
    }
    if(root["lorawan"]) {
      const YAML::Node lw = root["lorawan"];
      if(lw["region"].IsDefined()) { out.lorawan.region = lw["region"].as<std::string>(); }
      if(lw["sub_band"].IsDefined()) { out.lorawan.sub_band = (uint8_t)lw["sub_band"].as<int>(); }
      if(lw["uplink_interval_sec"].IsDefined()) {
        out.lorawan.uplink_interval_sec = (uint32_t)lw["uplink_interval_sec"].as<unsigned>();
      }
    }
    if(root["snapshot"]) {
      const YAML::Node sn = root["snapshot"];
      if(sn["path"].IsDefined()) { out.snapshot.path = sn["path"].as<std::string>(); }
      if(sn["device_ieee"].IsDefined()) { out.snapshot.device_ieee = sn["device_ieee"].as<std::string>(); }
    }
    if(root["payload"]) {
      const YAML::Node pl = root["payload"];
      if(pl["format"].IsDefined()) {
        std::string f = pl["format"].as<std::string>();
        if(f == "packed") {
          out.payload.format = PayloadFormat::Packed;
        } else {
          out.payload.format = PayloadFormat::Legacy;
        }
      }
      if(pl["include_status"].IsDefined()) {
        out.payload.include_status = pl["include_status"].as<bool>();
      }
      if(pl["max_bytes"].IsDefined()) {
        out.payload.max_bytes = pl["max_bytes"].as<unsigned>();
      }
      if(pl["entries"] && pl["entries"].IsSequence()) {
        const YAML::Node eseq = pl["entries"];
        bool any_id = false;
        bool any_omit = false;
        for(const auto& en : eseq) {
          if(en["id"].IsDefined()) {
            any_id = true;
          } else {
            any_omit = true;
          }
        }
        if(any_id && any_omit) {
          err = "payload.entries: set id on every entry or omit id on all (auto 0,1,2,...)";
          return false;
        }
        out.payload.entries.clear();
        uint8_t auto_seq = 1;
        for(const auto& en : eseq) {
          PayloadEntry pe;
          if(en["id"].IsDefined()) {
            int v = en["id"].as<int>();
            if(v < 0) { v = 0; }
            if(v > 255) { v = 255; }
            pe.id = (uint8_t)v;
          } else {
            pe.id = auto_seq;
            if(auto_seq < 255) { ++auto_seq; }
          }
          if(en["device"].IsDefined()) { pe.device = en["device"].as<std::string>(); }
          if(!parse_entry_type(en, pe, err)) { return false; }
          out.payload.entries.push_back(std::move(pe));
        }
      }
    }
  } catch(const std::exception& e) {
    err = std::string("YAML: ") + e.what();
    return false;
  }

  if(out.lorawan.uplink_interval_sec < 30) {
    out.lorawan.uplink_interval_sec = 30;
  }
  if(!app_config_valid(out, err)) { return false; }
  if(out.payload.max_bytes < 8) {
    out.payload.max_bytes = 8;
  }
  return true;
}

static void ovr_u32(uint32_t& dst, const char* env) {
  const char* v = std::getenv(env);
  if(v && v[0]) { dst = (uint32_t)std::strtoul(v, nullptr, 10); }
}

static void ovr_u8(uint8_t& dst, const char* env) {
  const char* v = std::getenv(env);
  if(v && v[0]) { dst = (uint8_t)std::atoi(v); }
}

static void ovr_str(std::string& dst, const char* env) {
  const char* v = std::getenv(env);
  if(v && v[0]) { dst = v; }
}

void apply_env_overrides(AppConfig& cfg) {
  ovr_u8(cfg.hw.spi_channel, "RFM_SPI_CHANNEL");
  ovr_u32(cfg.hw.pin_cs, "RFM_PIN_CS");
  ovr_u32(cfg.hw.pin_dio0, "RFM_PIN_DIO0");
  ovr_u32(cfg.hw.pin_rst, "RFM_PIN_RST");
  ovr_str(cfg.lorawan.region, "LORAWAN_REGION");
  ovr_u8(cfg.lorawan.sub_band, "LORAWAN_SUB_BAND");
  if(std::getenv("LORAWAN_UPLINK_INTERVAL_SEC") && std::getenv("LORAWAN_UPLINK_INTERVAL_SEC")[0]) {
    cfg.lorawan.uplink_interval_sec =
        (uint32_t)std::strtoul(std::getenv("LORAWAN_UPLINK_INTERVAL_SEC"), nullptr, 10);
  }
  if(cfg.lorawan.uplink_interval_sec < 30) { cfg.lorawan.uplink_interval_sec = 30; }
  ovr_str(cfg.snapshot.path, "SNAPSHOT_PATH");
  ovr_str(cfg.snapshot.device_ieee, "SNAPSHOT_DEVICE_IEEE");
  if(const char* pf = std::getenv("LORAWAN_PAYLOAD_FORMAT")) {
    if(pf[0]) {
      if(std::strcmp(pf, "packed") == 0) {
        cfg.payload.format = PayloadFormat::Packed;
      } else {
        cfg.payload.format = PayloadFormat::Legacy;
      }
    }
  }
  if(const char* ps = std::getenv("PAYLOAD_INCLUDE_STATUS")) {
    if(ps[0]) { cfg.payload.include_status = (std::atoi(ps) != 0); }
  }
  if(std::getenv("PAYLOAD_MAX_BYTES") && std::getenv("PAYLOAD_MAX_BYTES")[0]) {
    cfg.payload.max_bytes = (uint32_t)std::strtoul(std::getenv("PAYLOAD_MAX_BYTES"), nullptr, 10);
  }
}
