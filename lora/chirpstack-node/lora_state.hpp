#pragma once

class LoRaWANNode;

/// Default mount in Docker: /data/lorawan-state (nonces.bin + session.bin).
bool lorawan_persist_enabled();

/// After beginOTAA: load RadioLib nonces/session from disk if valid.
void lorawan_try_restore(LoRaWANNode& node);

/// Persist current RadioLib OTAA buffers (call after activateOTAA and after uplinks).
void lorawan_save_after_join(LoRaWANNode& node);
void lorawan_save_after_uplink(LoRaWANNode& node);
