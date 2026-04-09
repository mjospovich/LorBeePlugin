#!/usr/bin/env bash
# Backward-compatible name — runs full stack boot sequence (see stack-after-boot.sh).
exec "$(cd "$(dirname "$0")" && pwd)/stack-after-boot.sh"
