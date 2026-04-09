# Edge static configuration (examples)

Files here are **templates** you copy or reference when customizing an edge. Nothing in this folder is mounted into containers by default.

| File | Purpose |
|------|---------|
| [`payload.manifest.example.yaml`](payload.manifest.example.yaml) | Describes what goes on the LoRa air interface for **packed** payloads. Keep this in sync with `lora/chirpstack-node/config.yaml` (`payload:`) and with your ChirpStack **codec** on the main PC. |

See [docs/lorbee-data.md](../../docs/lorbee-data.md) for MQTT, HTTP, and ChirpStack workflow, and [docs/lorbeeos-paths.md](../../docs/lorbeeos-paths.md) for **LorBeeOS** filesystem layout on a dedicated image.
