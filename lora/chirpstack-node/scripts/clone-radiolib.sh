#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TAG="${RADIOLIB_VERSION:-7.6.0}"
cd "$ROOT"
if [[ -f RadioLib/CMakeLists.txt ]]; then
  echo "RadioLib already present in $ROOT/RadioLib"
  exit 0
fi
git clone --depth 1 --branch "$TAG" https://github.com/jgromes/RadioLib.git RadioLib
echo "Cloned RadioLib $TAG -> $ROOT/RadioLib"
