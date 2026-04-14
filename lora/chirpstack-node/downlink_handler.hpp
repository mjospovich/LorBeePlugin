#pragma once

#include <cstdint>
#include <string>

/// FPort used for downlink commands. Set this in ChirpStack when queuing.
static constexpr uint8_t DOWNLINK_CMD_FPORT = 10;

/// Packed v4 uplink flags — bit positions.
static constexpr uint8_t FLAG_INCLUDE_STATUS = 0x01;
static constexpr uint8_t FLAG_HAS_ACK        = 0x02;

/// Downlink command IDs.
/// See lora/README.md § "Downlink Command Protocol".
enum class DownlinkCmd : uint8_t {
  Ping              = 0x01,
  SetUplinkInterval = 0x02,
  PermitZigbeeJoin  = 0x03,
};

struct DownlinkResult {
  bool valid = false;
  uint8_t cmd_id = 0;
  bool success = false;
  std::string message;
};

/// Mutable runtime state that downlink commands can modify.
struct RuntimeState {
  uint32_t uplink_interval_sec = 300;
  std::string command_out_dir = "/data/downlink-commands";
  std::string mqtt_host = "127.0.0.1";
  uint16_t mqtt_port = 1883;
  DownlinkResult last_result;
};

/// Parse and execute a downlink command payload.
/// Only processes payloads on DOWNLINK_CMD_FPORT; others are logged and ignored.
DownlinkResult handle_downlink(const uint8_t* data, size_t len, uint8_t fPort, RuntimeState& state);
