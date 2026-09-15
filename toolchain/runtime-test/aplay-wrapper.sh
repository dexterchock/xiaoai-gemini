#!/usr/bin/env bash
set -euo pipefail

exec qemu-arm-static /usr/bin/aplay "$@"
