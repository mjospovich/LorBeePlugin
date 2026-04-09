#include "lora_state.hpp"

#include <RadioLib.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <sys/stat.h>
#include <unistd.h>

static std::string state_dir() {
  const char* e = std::getenv("LORAWAN_STATE_DIR");
  if(e && e[0]) { return std::string(e); }
  return "/data/lorawan-state";
}

static const char* kNonces = "nonces.bin";
static const char* kSession = "session.bin";

bool lorawan_persist_enabled() {
  const char* e = std::getenv("LORAWAN_PERSIST_SESSION");
  if(e && e[0] == '0' && e[1] == '\0') { return false; }
  return true;
}

static bool mkdir_p(const std::string& dir) {
  if(dir.empty()) { return false; }
  std::string acc;
  for(size_t i = 0; i < dir.size(); i++) {
    acc += dir[i];
    if(dir[i] == '/' && acc.size() > 1) {
      if(mkdir(acc.c_str(), 0755) != 0 && errno != EEXIST) { return false; }
    }
  }
  if(mkdir(dir.c_str(), 0755) != 0 && errno != EEXIST) { return false; }
  return true;
}

static bool write_atomic(const std::string& path, const uint8_t* data, size_t len) {
  std::string tmp = path + ".tmp";
  FILE* f = std::fopen(tmp.c_str(), "wb");
  if(!f) { return false; }
  if(std::fwrite(data, 1, len, f) != len) {
    std::fclose(f);
    std::remove(tmp.c_str());
    return false;
  }
  if(std::fclose(f) != 0) {
    std::remove(tmp.c_str());
    return false;
  }
  return std::rename(tmp.c_str(), path.c_str()) == 0;
}

static bool read_file(const std::string& path, uint8_t* out, size_t expect) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if(!f) { return false; }
  size_t n = std::fread(out, 1, expect, f);
  std::fclose(f);
  return n == expect;
}

void lorawan_try_restore(LoRaWANNode& node) {
  if(!lorawan_persist_enabled()) { return; }
  const std::string dir = state_dir();
  const std::string pn = dir + "/" + kNonces;
  const std::string ps = dir + "/" + kSession;

  uint8_t nb[RADIOLIB_LORAWAN_NONCES_BUF_SIZE];
  uint8_t sb[RADIOLIB_LORAWAN_SESSION_BUF_SIZE];
  if(!read_file(pn, nb, sizeof(nb)) || !read_file(ps, sb, sizeof(sb))) {
    return;
  }

  int16_t st = node.setBufferNonces(nb);
  if(st == RADIOLIB_ERR_NONCES_DISCARDED) {
    std::fprintf(stderr, "[lorawan-node] discarding saved nonces (keys/region mismatch)\n");
    return;
  }
  if(st != RADIOLIB_ERR_NONE) {
    std::fprintf(stderr, "[lorawan-node] setBufferNonces: %d\n", (int)st);
    return;
  }

  st = node.setBufferSession(sb);
  if(st != RADIOLIB_ERR_NONE) {
    if(st == RADIOLIB_ERR_SESSION_DISCARDED) {
      std::fprintf(stderr,
          "[lorawan-node] saved session does not match nonces (e.g. power loss between writes). "
          "Fresh OTAA join; rm data/lorawan-state/*.bin if this repeats every boot.\n");
    } else {
      std::fprintf(stderr, "[lorawan-node] setBufferSession: %d (starting fresh join)\n", (int)st);
    }
    return;
  }
  std::fprintf(stderr, "[lorawan-node] restored OTAA state from %s\n", dir.c_str());
}

void lorawan_save_after_join(LoRaWANNode& node) {
  if(!lorawan_persist_enabled()) { return; }
  const std::string dir = state_dir();
  if(!mkdir_p(dir)) {
    std::fprintf(stderr, "[lorawan-node] cannot mkdir %s\n", dir.c_str());
    return;
  }
  const uint8_t* nb = node.getBufferNonces();
  const uint8_t* sb = node.getBufferSession();
  const std::string pn = dir + "/" + kNonces;
  const std::string ps = dir + "/" + kSession;
  if(!write_atomic(pn, nb, RADIOLIB_LORAWAN_NONCES_BUF_SIZE)) {
    std::fprintf(stderr, "[lorawan-node] failed to write %s\n", pn.c_str());
  }
  if(!write_atomic(ps, sb, RADIOLIB_LORAWAN_SESSION_BUF_SIZE)) {
    std::fprintf(stderr, "[lorawan-node] failed to write %s\n", ps.c_str());
  }
}

void lorawan_save_after_uplink(LoRaWANNode& node) {
  if(!lorawan_persist_enabled()) { return; }
  const std::string dir = state_dir();
  if(!mkdir_p(dir)) { return; }
  const uint8_t* sb = node.getBufferSession();
  const std::string ps = dir + "/" + kSession;
  (void)write_atomic(ps, sb, RADIOLIB_LORAWAN_SESSION_BUF_SIZE);
}
