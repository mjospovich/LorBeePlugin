#include "payload_builder.hpp"

#include <algorithm>
#include <cmath>

static void append_u8(std::vector<uint8_t>& b, uint8_t v) {
  b.push_back(v);
}

static void append_u16_be(std::vector<uint8_t>& b, uint16_t v) {
  b.push_back((uint8_t)((v >> 8) & 0xff));
  b.push_back((uint8_t)(v & 0xff));
}

static void append_i16_be(std::vector<uint8_t>& b, int16_t v) {
  append_u16_be(b, (uint16_t)v);
}

static const nlohmann::json* find_device(const nlohmann::json& devices, const std::string& key,
    std::string& err) {
  auto it = devices.find(key);
  if(it != devices.end()) { return &it.value(); }
  if(key.size() >= 18 && key.rfind("0x", 0) == 0) {
    std::string lk = key;
    for(char& c : lk) {
      if(c >= 'A' && c <= 'F') { c = (char)(c - 'A' + 'a'); }
    }
    it = devices.find(lk);
    if(it != devices.end()) { return &it.value(); }
  }
  err = "device key not in snapshot: " + key;
  return nullptr;
}

static void append_status_block(const nlohmann::json& dev, std::vector<uint8_t>& buf) {
  uint8_t lq = 0xff;
  if(dev.contains("linkquality") && dev["linkquality"].is_number_integer()) {
    int v = (int)dev["linkquality"].get<int64_t>();
    if(v >= 0 && v <= 255) { lq = (uint8_t)v; }
  }
  append_u8(buf, lq);
  uint8_t bat = 0xff;
  if(dev.contains("battery") && dev["battery"].is_number_integer()) {
    int v = (int)dev["battery"].get<int64_t>();
    if(v >= 0 && v <= 100) { bat = (uint8_t)v; }
  }
  append_u8(buf, bat);
  uint16_t volt = 0xffff;
  if(dev.contains("voltage") && dev["voltage"].is_number_integer()) {
    int v = (int)dev["voltage"].get<int64_t>();
    v = std::max(0, std::min(v, 65535));
    volt = (uint16_t)v;
  }
  append_u16_be(buf, volt);
}

static bool encode_data_field(const std::string& fname, const nlohmann::json& dev,
    std::vector<uint8_t>& buf, std::string& err) {
  if(fname == "temperature") {
    if(!dev.contains("temperature") || !dev["temperature"].is_number()) {
      append_i16_be(buf, (int16_t)0x7fff);
      return true;
    }
    double t = dev["temperature"].get<double>();
    long v = std::lround(t * 100.0);
    v = std::max(-32768L, std::min(v, 32767L));
    append_i16_be(buf, (int16_t)v);
    return true;
  }
  if(fname == "humidity") {
    if(!dev.contains("humidity") || !dev["humidity"].is_number()) {
      append_u16_be(buf, 0xffff);
      return true;
    }
    long h = std::lround(dev["humidity"].get<double>() * 100.0);
    h = std::max(0L, std::min(h, 65535L));
    append_u16_be(buf, (uint16_t)h);
    return true;
  }
  if(fname == "occupancy" || fname == "motion") {
    uint8_t b = 0xff;
    if(dev.contains("occupancy")) {
      if(dev["occupancy"].is_boolean()) {
        b = dev["occupancy"].get<bool>() ? 1 : 0;
      } else if(dev["occupancy"].is_number()) {
        b = dev["occupancy"].get<double>() != 0.0 ? 1 : 0;
      }
    }
    append_u8(buf, b);
    return true;
  }
  if(fname == "illumination" || fname == "brightness") {
    uint8_t e = 0;
    if(dev.contains("illumination") && dev["illumination"].is_string()) {
      std::string s = dev["illumination"].get<std::string>();
      if(s == "dark") {
        e = 1;
      } else if(s == "medium") {
        e = 2;
      } else if(s == "bright") {
        e = 3;
      }
    }
    append_u8(buf, e);
    return true;
  }
  if(fname == "contact") {
    uint8_t b = 0xff;
    if(dev.contains("contact")) {
      if(dev["contact"].is_boolean()) {
        b = dev["contact"].get<bool>() ? 1 : 0;
      } else if(dev["contact"].is_number()) {
        b = dev["contact"].get<double>() != 0.0 ? 1 : 0;
      }
    }
    append_u8(buf, b);
    return true;
  }
  if(fname == "linkquality") {
    if(!dev.contains("linkquality") || !dev["linkquality"].is_number_integer()) {
      append_u8(buf, 0xff);
      return true;
    }
    int v = (int)dev["linkquality"].get<int64_t>();
    if(v < 0 || v > 255) { v = 0xff; }
    append_u8(buf, (uint8_t)v);
    return true;
  }
  if(fname == "battery") {
    if(!dev.contains("battery") || !dev["battery"].is_number_integer()) {
      append_u8(buf, 0xff);
      return true;
    }
    int v = (int)dev["battery"].get<int64_t>();
    if(v < 0 || v > 100) { v = 0xff; }
    append_u8(buf, (uint8_t)v);
    return true;
  }
  if(fname == "voltage") {
    if(!dev.contains("voltage") || !dev["voltage"].is_number_integer()) {
      append_u16_be(buf, 0xffff);
      return true;
    }
    int v = (int)dev["voltage"].get<int64_t>();
    v = std::max(0, std::min(v, 65535));
    append_u16_be(buf, (uint16_t)v);
    return true;
  }
  err = "unknown payload field name: " + fname;
  return false;
}

bool build_packed_uplink_payload(const AppConfig& cfg, const nlohmann::json& snap_root,
    std::vector<uint8_t>& out, std::string& err) {
  out.clear();
  if(!snap_root.contains("devices") || !snap_root["devices"].is_object()) {
    err = "snapshot missing devices object";
    return false;
  }
  const auto& devices = snap_root["devices"];

  // v4: self-describing entries — each entry carries its sensor_type byte
  // so the decoder doesn't need a positional PLAN, just the type registry.
  append_u8(out, 0x04);
  append_u8(out, cfg.payload.include_status ? 1u : 0u);

  for(const PayloadEntry& en : cfg.payload.entries) {
    append_u8(out, en.id);
    append_u8(out, static_cast<uint8_t>(en.sensor_type));

    std::string lerr;
    const nlohmann::json* pdev = find_device(devices, en.device, lerr);
    if(!pdev) {
      err = lerr;
      return false;
    }
    const nlohmann::json& dev = *pdev;
    for(const std::string& fn : en.fields) {
      if(!encode_data_field(fn, dev, out, err)) { return false; }
    }
    if(cfg.payload.include_status) { append_status_block(dev, out); }
  }

  if(out.size() > cfg.payload.max_bytes) {
    err = "packed payload length " + std::to_string(out.size()) + " exceeds max_bytes " +
        std::to_string(cfg.payload.max_bytes);
    return false;
  }
  return true;
}
