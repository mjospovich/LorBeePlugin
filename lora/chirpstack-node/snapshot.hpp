#pragma once

#include "config.hpp"

#include <nlohmann/json.hpp>

#include <string>

struct SnapshotReadout {
  bool ok = false;
  float temperature = 0.f;
  float humidity = 0.f;
  std::string error;
};

bool load_snapshot_json(const std::string& path, nlohmann::json& out, std::string& err);

/// Read temperature + humidity from Node-RED snapshot JSON (`devices` map).
SnapshotReadout read_snapshot(const AppConfig& cfg);
