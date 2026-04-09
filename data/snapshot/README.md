# Runtime sensor snapshot (host)

This directory holds the **canonical merged Zigbee snapshot** written by Node-RED:

- **`latest.json`** — full edge state (IEEE keys). Runtime only; not committed to git.

**Host path** is configurable: set **`LORBEE_SNAPSHOT_HOST`** in `.env` (default `./data/snapshot` relative to the stack). On **LorBeeOS** images this is often **`/var/lib/lorbee/snapshot`**. See [docs/lorbeeos-paths.md](../docs/lorbeeos-paths.md).

Inside containers the mount is always **`/data/snapshot/`**.

Run **`make init`** after clone so the host directory exists.
