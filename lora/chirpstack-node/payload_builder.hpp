#pragma once

#include "config.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

/// Build packed application payload (format byte 0x04, self-describing entries).
/// See lora/README.md § "Sensor Type Registry" and § "Packed layout (v4)".
bool build_packed_uplink_payload(const AppConfig& cfg, const nlohmann::json& snap_root,
    std::vector<uint8_t>& out, std::string& err);
