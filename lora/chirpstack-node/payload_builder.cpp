#include "payload_builder.hpp"

#include <algorithm>
#include <cmath>

static bool is_hex_ieee_key(const std::string& key) {
  return key.size() >= 18 && key.rfind("0x", 0) == 0;
}

static std::string lower_hex_ieee_key(std::string key) {
  for(char& c : key) {
    if(c >= 'A' && c <= 'F') { c = (char)(c - 'A' + 'a'); }
  }
  return key;
}

static void append_u8(std::vector<uint8_t>& b, uint8_t v) {
  b.push_back(v);
}

static void append_u16_be(std::vector<uint8_t>& b, uint16_t v) {
  b.push_back((uint8_t)((v >> 8) & 0xff));
  b.push_back((uint8_t)(v & 0xff));
}

static void append_u32_be(std::vector<uint8_t>& b, uint32_t v) {
  b.push_back((uint8_t)((v >> 24) & 0xff));
  b.push_back((uint8_t)((v >> 16) & 0xff));
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
  if(is_hex_ieee_key(key)) {
    std::string lk = lower_hex_ieee_key(key);
    it = devices.find(lk);
    if(it != devices.end()) { return &it.value(); }
  }
  err = "device key not in snapshot: " + key;
  return nullptr;
}

static const nlohmann::json* find_key_casefold(const nlohmann::json& obj, const std::string& key) {
  if(!obj.is_object()) { return nullptr; }
  auto it = obj.find(key);
  if(it != obj.end()) { return &it.value(); }
  if(is_hex_ieee_key(key)) {
    it = obj.find(lower_hex_ieee_key(key));
    if(it != obj.end()) { return &it.value(); }
  }
  return nullptr;
}

static const nlohmann::json* find_alarm_thresholds_for_device(const nlohmann::json& snap_root,
    const nlohmann::json& dev, const std::string& device_key) {
  if(snap_root.contains("alarms") && snap_root["alarms"].is_object()) {
    const auto& a = snap_root["alarms"];
    if(a.contains("thresholds") && a["thresholds"].is_object()) {
      if(const nlohmann::json* cfg = find_key_casefold(a["thresholds"], device_key)) {
        if(cfg->is_object()) { return cfg; }
      }
    }
  }

  if(snap_root.contains("alarm_thresholds") && snap_root["alarm_thresholds"].is_object()) {
    if(const nlohmann::json* cfg = find_key_casefold(snap_root["alarm_thresholds"], device_key)) {
      if(cfg->is_object()) { return cfg; }
    }
  }

  if(dev.contains("alarm_thresholds") && dev["alarm_thresholds"].is_object()) {
    return &dev["alarm_thresholds"];
  }

  return nullptr;
}

static const nlohmann::json* find_field_threshold_rule(const nlohmann::json& cfg,
    const std::string& fname) {
  auto get = [&](const char* key) -> const nlohmann::json* {
    if(cfg.contains(key)) { return &cfg[key]; }
    return nullptr;
  };

  if(const nlohmann::json* direct = get(fname.c_str())) { return direct; }

  if(fname == "temperature") { return get("temperature_c"); }
  if(fname == "humidity") { return get("humidity_pct"); }
  if(fname == "motion") {
    if(const nlohmann::json* v = get("occupancy")) { return v; }
    return get("motion");
  }
  if(fname == "occupancy") { return get("motion"); }
  if(fname == "illumination") {
    if(const nlohmann::json* v = get("brightness")) { return v; }
  }
  if(fname == "brightness") {
    if(const nlohmann::json* v = get("illumination")) { return v; }
  }
  if(fname == "pm1_0") { return get("pm1_0_ugm3"); }
  if(fname == "pm2_5") { return get("pm2_5_ugm3"); }
  if(fname == "pm4_0") { return get("pm4_0_ugm3"); }
  if(fname == "pm10") { return get("pm10_ugm3"); }

  return nullptr;
}

static bool read_field_value_json(const std::string& fname, const nlohmann::json& dev,
    nlohmann::json& out) {
  out = nullptr;

  if(fname == "temperature") {
    if(dev.contains("temperature") && dev["temperature"].is_number()) {
      out = dev["temperature"].get<double>();
      return true;
    }
    if(dev.contains("temperature_c") && dev["temperature_c"].is_number()) {
      out = dev["temperature_c"].get<double>();
      return true;
    }
    return false;
  }

  if(fname == "humidity") {
    if(dev.contains("humidity") && dev["humidity"].is_number()) {
      out = dev["humidity"].get<double>();
      return true;
    }
    if(dev.contains("humidity_pct") && dev["humidity_pct"].is_number()) {
      out = dev["humidity_pct"].get<double>();
      return true;
    }
    return false;
  }

  if(fname == "occupancy" || fname == "motion") {
    if(!dev.contains("occupancy")) { return false; }
    if(dev["occupancy"].is_boolean()) {
      out = dev["occupancy"].get<bool>();
      return true;
    }
    if(dev["occupancy"].is_number()) {
      out = dev["occupancy"].get<double>() != 0.0;
      return true;
    }
    return false;
  }

  if(fname == "illumination" || fname == "brightness") {
    if(dev.contains("illumination") && dev["illumination"].is_string()) {
      out = dev["illumination"].get<std::string>();
      return true;
    }
    if(dev.contains("brightness") && dev["brightness"].is_string()) {
      out = dev["brightness"].get<std::string>();
      return true;
    }
    return false;
  }

  if(fname == "contact") {
    if(!dev.contains("contact")) { return false; }
    if(dev["contact"].is_boolean()) {
      out = dev["contact"].get<bool>();
      return true;
    }
    if(dev["contact"].is_number()) {
      out = dev["contact"].get<double>() != 0.0;
      return true;
    }
    return false;
  }

  if(fname == "pm1_0" || fname == "pm2_5" || fname == "pm4_0" || fname == "pm10") {
    if(dev.contains(fname) && dev[fname].is_number()) {
      out = dev[fname].get<double>();
      return true;
    }
    std::string suffixed = fname + "_ugm3";
    if(dev.contains(suffixed) && dev[suffixed].is_number()) {
      out = dev[suffixed].get<double>();
      return true;
    }
    return false;
  }

  if(fname == "aqi" || fname == "pressure_hpa" || fname == "gas_resistance_ohm") {
    if(dev.contains(fname) && dev[fname].is_number()) {
      out = dev[fname].get<double>();
      return true;
    }
    return false;
  }

  return false;
}

static bool json_equal_scalar(const nlohmann::json& a, const nlohmann::json& b) {
  if(a.is_number() && b.is_number()) {
    return std::fabs(a.get<double>() - b.get<double>()) < 1e-9;
  }
  if(a.is_boolean() && b.is_boolean()) {
    return a.get<bool>() == b.get<bool>();
  }
  if(a.is_string() && b.is_string()) {
    return a.get<std::string>() == b.get<std::string>();
  }
  return false;
}

static bool threshold_rule_triggered(const nlohmann::json& value, const nlohmann::json& rule) {
  if(rule.is_number()) {
    return value.is_number() && value.get<double>() > rule.get<double>();
  }

  if(rule.is_boolean() || rule.is_string()) {
    return json_equal_scalar(value, rule);
  }

  if(!rule.is_object()) { return false; }

  bool triggered = false;

  auto cmp_num = [&](const char* key, bool (*cmp)(double, double)) {
    if(!rule.contains(key) || !rule[key].is_number() || !value.is_number()) { return; }
    if(cmp(value.get<double>(), rule[key].get<double>())) { triggered = true; }
  };

  cmp_num("gt", [](double a, double b) { return a > b; });
  cmp_num("gte", [](double a, double b) { return a >= b; });
  cmp_num("lt", [](double a, double b) { return a < b; });
  cmp_num("lte", [](double a, double b) { return a <= b; });

  if(rule.contains("eq")) {
    if(json_equal_scalar(value, rule["eq"])) { triggered = true; }
  }
  if(rule.contains("neq")) {
    if(!json_equal_scalar(value, rule["neq"])) { triggered = true; }
  }

  return triggered;
}

static uint8_t entry_alarm_field_mask(const nlohmann::json& snap_root, const std::string& device_key,
    const nlohmann::json& dev, const PayloadEntry& en) {
  const nlohmann::json* acfg = find_alarm_thresholds_for_device(snap_root, dev, device_key);
  if(!acfg) { return 0u; }

  uint8_t mask = 0u;
  size_t idx = 0;

  for(const std::string& fname : en.fields) {
    if(idx >= 8) { break; }
    const nlohmann::json* rule = find_field_threshold_rule(*acfg, fname);
    if(!rule) {
      ++idx;
      continue;
    }

    nlohmann::json value;
    if(!read_field_value_json(fname, dev, value)) {
      ++idx;
      continue;
    }

    if(threshold_rule_triggered(value, *rule)) { mask |= (uint8_t)(1u << idx); }
    ++idx;
  }

  return mask;
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
    double t = NAN;
    if(dev.contains("temperature") && dev["temperature"].is_number()) {
      t = dev["temperature"].get<double>();
    } else if(dev.contains("temperature_c") && dev["temperature_c"].is_number()) {
      t = dev["temperature_c"].get<double>();
    }
    if(std::isnan(t)) {
      append_i16_be(buf, (int16_t)0x7fff);
      return true;
    }
    long v = std::lround(t * 100.0);
    v = std::max(-32768L, std::min(v, 32767L));
    append_i16_be(buf, (int16_t)v);
    return true;
  }
  if(fname == "humidity") {
    double hp = NAN;
    if(dev.contains("humidity") && dev["humidity"].is_number()) {
      hp = dev["humidity"].get<double>();
    } else if(dev.contains("humidity_pct") && dev["humidity_pct"].is_number()) {
      hp = dev["humidity_pct"].get<double>();
    }
    if(std::isnan(hp)) {
      append_u16_be(buf, 0xffff);
      return true;
    }
    long h = std::lround(hp * 100.0);
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
  if(fname == "pm1_0" || fname == "pm2_5" || fname == "pm4_0" || fname == "pm10") {
    if(!dev.contains(fname) || !dev[fname].is_number()) {
      append_u16_be(buf, 0xffff);
      return true;
    }
    long v = std::lround(dev[fname].get<double>() * 10.0);
    v = std::max(0L, std::min(v, 65534L));
    append_u16_be(buf, (uint16_t)v);
    return true;
  }
  if(fname == "aqi") {
    if(!dev.contains("aqi") || !dev["aqi"].is_number()) {
      append_u16_be(buf, 0xffff);
      return true;
    }
    int a = (int)std::lround(dev["aqi"].get<double>());
    a = std::max(0, std::min(a, 500));
    append_u16_be(buf, (uint16_t)a);
    return true;
  }
  if(fname == "pressure_hpa") {
    double p = NAN;
    if(dev.contains("pressure_hpa") && dev["pressure_hpa"].is_number()) {
      p = dev["pressure_hpa"].get<double>();
    }
    if(std::isnan(p)) {
      append_u16_be(buf, 0xffff);
      return true;
    }
    int pi = (int)std::lround(p);
    pi = std::max(300, std::min(pi, 1100));
    append_u16_be(buf, (uint16_t)pi);
    return true;
  }
  if(fname == "gas_resistance_ohm") {
    if(!dev.contains("gas_resistance_ohm") || !dev["gas_resistance_ohm"].is_number()) {
      append_u32_be(buf, 0xffffffffu);
      return true;
    }
    double g = dev["gas_resistance_ohm"].get<double>();
    if(g < 0 || g > 4294967295.0) {
      append_u32_be(buf, 0xffffffffu);
      return true;
    }
    append_u32_be(buf, (uint32_t)std::llround(g));
    return true;
  }
  err = "unknown payload field name: " + fname;
  return false;
}

bool build_packed_uplink_payload(const AppConfig& cfg, const nlohmann::json& snap_root,
    std::vector<uint8_t>& out, std::string& err, bool* has_alarm_out) {
  out.clear();
  if(!snap_root.contains("devices") || !snap_root["devices"].is_object()) {
    err = "snapshot missing devices object";
    return false;
  }
  const auto& devices = snap_root["devices"];

  if(has_alarm_out) { *has_alarm_out = false; }

  // v5: self-describing entries — each entry carries its sensor_type byte
  // so the decoder doesn't need a positional PLAN, just the type registry.
  append_u8(out, 0x05);
  append_u8(out, cfg.payload.include_status ? 1u : 0u);

  for(const PayloadEntry& en : cfg.payload.entries) {
    std::string lerr;
    const nlohmann::json* pdev = find_device(devices, en.device, lerr);
    if(!pdev) {
      err = lerr;
      return false;
    }
    const nlohmann::json& dev = *pdev;
    const uint8_t alarm_mask = entry_alarm_field_mask(snap_root, en.device, dev, en);
    if(alarm_mask != 0u && has_alarm_out) { *has_alarm_out = true; }
    uint8_t stype_wire = static_cast<uint8_t>(en.sensor_type) & 0x7fu;

    append_u8(out, en.id);
    append_u8(out, stype_wire);
    append_u8(out, alarm_mask);

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

