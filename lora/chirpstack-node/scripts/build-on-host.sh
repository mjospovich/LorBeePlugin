#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
"$ROOT/scripts/clone-radiolib.sh"
sudo apt-get update
sudo apt-get install -y build-essential cmake liblgpio-dev libyaml-cpp-dev
rm -rf build
cmake -S . -B build -DRADIOLIB_ROOT="$ROOT/RadioLib"
cmake --build build -j2
echo "Binary: $ROOT/build/lorawan-node"
