#pragma once

#include <cstdint>
#include <string>
#include <vector>

/// SPI + GPIO for RFM9x (BCM numbering). See config.yaml / config.example.yaml.
struct LoraHardwareConfig {
  uint8_t spi_channel = 1;
  uint32_t pin_cs = 17;
  uint32_t pin_dio0 = 22;
  uint32_t pin_rst = 25;
};

/// Non-secret LoRaWAN radio / timing (region must match gateway + ChirpStack).
struct LoraWanRadioConfig {
  std::string region = "EU868";
  uint8_t sub_band = 0;
  uint32_t uplink_interval_sec = 300;
};

/// Node-RED merged sensor snapshot (see docs/architecture.md).
struct SnapshotConfig {
  /// Path to latest.json (host: repo path; Docker: mount at this path).
  std::string path = "/data/snapshot/latest.json";
  /// If non-empty, use this `devices` key only; else first device with temp+humidity.
  std::string device_ieee;
};

enum class PayloadFormat { Legacy, Packed };

/// One device block in a packed uplink: `devices` key + ordered data fields (see lora/README.md).
struct PayloadEntry {
  /// One-byte handle in the air payload so the ChirpStack codec can tell entries apart (0–255).
  /// If omitted in YAML, set to 0, 1, 2, … in order.
  uint8_t id = 0;
  std::string device;
  std::vector<std::string> fields;
};

/// Application payload layout (only the bytes you send; not LoRaWAN MAC overhead).
struct PayloadConfig {
  PayloadFormat format = PayloadFormat::Legacy;
  /// After each entry’s `fields`, append linkquality (u8), battery (u8), voltage (u16 BE).
  bool include_status = false;
  /// Reject packed payloads larger than this (EU868 DR0 ~51 B; leave headroom for your DR).
  uint32_t max_bytes = 222;
  std::vector<PayloadEntry> entries;
};

struct AppConfig {
  LoraHardwareConfig hw;
  LoraWanRadioConfig lorawan;
  SnapshotConfig snapshot;
  PayloadConfig payload;
};

/// Resolve config path: $LORA_CONFIG, else ./config.yaml, else /etc/lora/config.yaml.
std::string resolve_lora_config_path();

/// Load YAML from disk. If path is nullptr/empty, fills defaults only.
bool load_app_config(const char* config_path, AppConfig& out, std::string& err);

/// Apply environment overrides (12-factor): any set var wins over YAML.
void apply_env_overrides(AppConfig& cfg);

/// Call after load + env (packed mode requires YAML entries).
bool app_config_valid(const AppConfig& cfg, std::string& err);
