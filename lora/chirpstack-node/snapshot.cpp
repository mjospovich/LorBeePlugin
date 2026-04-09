#include "snapshot.hpp"

#include <cmath>
#include <fstream>
#include <sstream>

bool load_snapshot_json(const std::string& path, nlohmann::json& out, std::string& err) {
  std::ifstream f(path);
  if(!f) {
    err = std::string("cannot open ") + path;
    return false;
  }
  std::stringstream ss;
  ss << f.rdbuf();
  try {
    out = nlohmann::json::parse(ss.str());
  } catch(const std::exception& e) {
    err = std::string("JSON parse: ") + e.what();
    return false;
  }
  return true;
}

static bool fill_from_object(const nlohmann::json& dev, SnapshotReadout& out) {
  if(!dev.is_object()) { return false; }
  if(!dev.contains("temperature") || !dev.contains("humidity")) { return false; }
  try {
    out.temperature = dev["temperature"].get<double>();
    out.humidity = dev["humidity"].get<double>();
    out.ok = true;
    out.error.clear();
    return true;
  } catch(const std::exception& e) {
    out.error = e.what();
    return false;
  }
}

static const nlohmann::json* find_device_key(const nlohmann::json& devs, const std::string& key) {
  auto it = devs.find(key);
  if(it != devs.end()) { return &it.value(); }
  if(key.size() >= 18 && key.rfind("0x", 0) == 0) {
    std::string lk = key;
    for(char& c : lk) {
      if(c >= 'A' && c <= 'F') { c = (char)(c - 'A' + 'a'); }
    }
    it = devs.find(lk);
    if(it != devs.end()) { return &it.value(); }
  }
  return nullptr;
}

SnapshotReadout read_snapshot(const AppConfig& cfg) {
  SnapshotReadout out;
  nlohmann::json j;
  if(!load_snapshot_json(cfg.snapshot.path, j, out.error)) { return out; }
  if(!j.contains("devices") || !j["devices"].is_object()) {
    out.error = "missing or invalid 'devices'";
    return out;
  }
  const auto& devs = j["devices"];
  if(!cfg.snapshot.device_ieee.empty()) {
    const nlohmann::json* dev = find_device_key(devs, cfg.snapshot.device_ieee);
    if(!dev) {
      out.error = std::string("device not found: ") + cfg.snapshot.device_ieee;
      return out;
    }
    if(fill_from_object(*dev, out)) { return out; }
    out.error = "device has no temperature/humidity";
    return out;
  }
  for(auto it = devs.begin(); it != devs.end(); ++it) {
    if(fill_from_object(it.value(), out)) { return out; }
  }
  out.error = "no device with both temperature and humidity";
  return out;
}
