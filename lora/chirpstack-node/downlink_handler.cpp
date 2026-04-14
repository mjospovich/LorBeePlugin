#include "downlink_handler.hpp"

#include <cerrno>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <string>

#include <sys/stat.h>

static bool ensure_dir(const std::string& dir) {
  if(dir.empty()) { return false; }
  struct stat st;
  if(stat(dir.c_str(), &st) == 0) { return S_ISDIR(st.st_mode); }
  return mkdir(dir.c_str(), 0755) == 0 || errno == EEXIST;
}

static std::string hex_byte(uint8_t b) {
  char buf[4];
  std::snprintf(buf, sizeof(buf), "%02x", b);
  return buf;
}

static std::string hex_dump(const uint8_t* data, size_t len) {
  std::string out;
  for(size_t i = 0; i < len; i++) {
    if(i) { out += ' '; }
    out += hex_byte(data[i]);
  }
  return out;
}

static bool write_command_file(const std::string& dir, const char* json) {
  if(!ensure_dir(dir)) { return false; }
  std::string path = dir + "/latest.json";
  std::string tmp = path + ".tmp";
  {
    std::ofstream f(tmp);
    if(!f) { return false; }
    f << json;
    if(!f.good()) { return false; }
  }
  return std::rename(tmp.c_str(), path.c_str()) == 0;
}

/// Publish an MQTT message via mosquitto_pub (host networking, 5 s timeout).
static bool mqtt_publish(const RuntimeState& state, const std::string& topic,
    const std::string& payload) {
  char cmd[512];
  std::snprintf(cmd, sizeof(cmd),
      "timeout 5 mosquitto_pub -h %s -p %u -t '%s' -m '%s' 2>&1",
      state.mqtt_host.c_str(), (unsigned)state.mqtt_port,
      topic.c_str(), payload.c_str());

  FILE* pipe = popen(cmd, "r");
  if(!pipe) {
    std::fprintf(stderr, "[downlink] popen failed for mosquitto_pub\n");
    return false;
  }
  char buf[128];
  std::string output;
  while(std::fgets(buf, sizeof(buf), pipe)) { output += buf; }
  int rc = pclose(pipe);
  if(rc != 0) {
    std::fprintf(stderr, "[downlink] mosquitto_pub failed (rc=%d): %s\n", rc, output.c_str());
    return false;
  }
  std::fprintf(stderr, "[downlink] mqtt publish: %s -> %s\n", topic.c_str(), payload.c_str());
  return true;
}

// ---------------------------------------------------------------------------
// Command handlers
// ---------------------------------------------------------------------------

static DownlinkResult cmd_ping() {
  return {true, static_cast<uint8_t>(DownlinkCmd::Ping), true, "PONG"};
}

static DownlinkResult cmd_set_uplink_interval(const uint8_t* p, size_t len, RuntimeState& state) {
  DownlinkResult r{true, static_cast<uint8_t>(DownlinkCmd::SetUplinkInterval), false, ""};
  if(len < 2) {
    r.message = "need 2 bytes (u16 BE seconds), got " + std::to_string(len);
    return r;
  }
  uint32_t secs = ((uint32_t)p[0] << 8) | p[1];
  if(secs < 30) { secs = 30; }
  if(secs > 3600) { secs = 3600; }
  uint32_t old = state.uplink_interval_sec;
  state.uplink_interval_sec = secs;
  r.success = true;
  r.message = "uplink interval " + std::to_string(old) + " -> " + std::to_string(secs) + " s";
  return r;
}

static DownlinkResult cmd_permit_zigbee_join(const uint8_t* p, size_t len, RuntimeState& state) {
  DownlinkResult r{true, static_cast<uint8_t>(DownlinkCmd::PermitZigbeeJoin), false, ""};
  if(len < 1) {
    r.message = "need 1 byte (duration seconds), got 0";
    return r;
  }
  uint8_t duration = p[0];
  bool enable = duration > 0;

  char mqtt_payload[128];
  std::snprintf(mqtt_payload, sizeof(mqtt_payload),
      "{\"value\":%s,\"time\":%u}", enable ? "true" : "false", (unsigned)duration);

  bool mqtt_ok = mqtt_publish(state,
      "zigbee2mqtt/bridge/request/permit_join", mqtt_payload);

  char log_json[256];
  std::snprintf(log_json, sizeof(log_json),
      "{\"command\":\"permit_join\",\"value\":%s,\"time\":%u,\"ts\":%ld,\"mqtt\":%s}\n",
      enable ? "true" : "false", (unsigned)duration,
      (long)std::time(nullptr), mqtt_ok ? "true" : "false");
  write_command_file(state.command_out_dir, log_json);

  if(!mqtt_ok) {
    r.message = "mosquitto_pub failed for permit_join (command file written as fallback)";
    return r;
  }
  r.success = true;
  r.message = std::string("permit_join ") + (enable ? "enabled" : "disabled") +
              " duration=" + std::to_string(duration) + "s via MQTT";
  return r;
}

// ---------------------------------------------------------------------------
// Dispatcher
// ---------------------------------------------------------------------------

DownlinkResult handle_downlink(const uint8_t* data, size_t len, uint8_t fPort, RuntimeState& state) {
  if(fPort != DOWNLINK_CMD_FPORT) {
    std::fprintf(stderr, "[downlink] ignoring fPort %u (expected %u), %zu bytes: %s\n",
        (unsigned)fPort, (unsigned)DOWNLINK_CMD_FPORT, len, hex_dump(data, len).c_str());
    return {};
  }
  if(len < 1) {
    std::fprintf(stderr, "[downlink] empty command payload on fPort %u\n", (unsigned)fPort);
    return {};
  }

  uint8_t cmd = data[0];
  const uint8_t* payload = data + 1;
  size_t plen = len - 1;

  std::fprintf(stderr, "[downlink] cmd=0x%02x payload=%zu bytes: %s\n",
      cmd, plen, plen > 0 ? hex_dump(payload, plen).c_str() : "(none)");

  DownlinkResult r;
  switch(static_cast<DownlinkCmd>(cmd)) {
    case DownlinkCmd::Ping:
      r = cmd_ping();
      break;
    case DownlinkCmd::SetUplinkInterval:
      r = cmd_set_uplink_interval(payload, plen, state);
      break;
    case DownlinkCmd::PermitZigbeeJoin:
      r = cmd_permit_zigbee_join(payload, plen, state);
      break;
    default:
      r = {true, cmd, false, "unknown command 0x" + hex_byte(cmd)};
      break;
  }

  std::fprintf(stderr, "[downlink] result: cmd=0x%02x %s — %s\n",
      r.cmd_id, r.success ? "OK" : "FAIL", r.message.c_str());

  state.last_result = r;
  return r;
}
