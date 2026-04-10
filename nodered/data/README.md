# Node-RED user directory (bind-mounted)

**Shipped in git:** `flows.json`, `settings.js`, `package.json` — your default flows and editor settings.

**Runtime only (gitignored):** `.config.nodes.json`, `.config.runtime.json`, `.config.users.json`, `flows_cred.json`, `*.backup`. Node-RED creates the `.config.*` files on first start; they contain instance-specific data and a **credential secret** — do not commit or share them.

After clone, run `make init` or `bash scripts/ensure-data-dirs.sh` so `data/snapshot/` exists on the host. See [docs/nodered.md](../../docs/nodered.md).
