#include "snapshot.hpp"

#include <cmath>
#include <fstream>
#include <sstream>

static bool read_temp_hum(const nlohmann::json& dev, double& temp_c, double& hum_pct) {
  if(!dev.is_object()) { return false; }
  temp_c = NAN;
  hum_pct = NAN;
  if(dev.contains("temperature") && dev["temperature"].is_number()) {
    temp_c = dev["temperature"].get<double>();
  } else if(dev.contains("temperature_c") && dev["temperature_c"].is_number()) {
    temp_c = dev["temperature_c"].get<double>();
  }
  if(dev.contains("humidity") && dev["humidity"].is_number()) {
    hum_pct = dev["humidity"].get<double>();
  } else if(dev.contains("humidity_pct") && dev["humidity_pct"].is_number()) {
    hum_pct = dev["humidity_pct"].get<double>();
  }
  return !std::isnan(temp_c) && !std::isnan(hum_pct);
}

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
  double t = NAN;
  double h = NAN;
  if(!read_temp_hum(dev, t, h)) { return false; }
  try {
    out.temperature = t;
    out.humidity = h;
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
