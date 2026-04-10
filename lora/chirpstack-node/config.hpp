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
  std::string path = "/data/snapshot/latest.json";
  std::string device_ieee;
};

enum class PayloadFormat { Legacy, Packed };

// ---------------------------------------------------------------------------
// Sensor Type Registry — standardized mapping shared by encoder + decoder.
// Add new types at the end; never reuse or reorder existing IDs.
// See lora/README.md § "Sensor Type Registry".
// ---------------------------------------------------------------------------
enum class SensorType : uint8_t {
  Climate = 0x01,   // temperature (i16 BE) + humidity (u16 BE)  — 4 data bytes
  Motion  = 0x02,   // occupancy (u8)       + illumination (u8)  — 2 data bytes
  Contact = 0x03,   // contact (u8)                              — 1 data byte
};

struct SensorTypeDef {
  SensorType type;
  const char* name;
  std::vector<std::string> fields;
};

/// Canonical registry. The encoder writes the type byte on-air; the decoder
/// uses the same table to know what bytes follow. Keep in sync with the
/// ChirpStack codec TYPES map (see lora/README.md).
const std::vector<SensorTypeDef>& sensor_type_registry();

/// Look up a SensorTypeDef by its string name (e.g. "climate"). nullptr if unknown.
const SensorTypeDef* sensor_type_by_name(const std::string& name);

/// Look up by numeric wire ID. nullptr if unknown.
const SensorTypeDef* sensor_type_by_id(uint8_t id);

/// One device block in a packed uplink (see lora/README.md).
struct PayloadEntry {
  /// Instance label (0–255) — the decoder uses this + sensor_type to identify the entry.
  uint8_t id = 0;
  std::string device;
  /// Sensor type determines which fields are encoded and in what order.
  SensorType sensor_type = SensorType::Climate;
  /// Resolved field list (derived from sensor_type via registry, not user-specified).
  std::vector<std::string> fields;
};

struct PayloadConfig {
  PayloadFormat format = PayloadFormat::Legacy;
  bool include_status = false;
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
