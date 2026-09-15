#!/usr/bin/env bash
set -euo pipefail

EXPECTED_VERSION_ID="24.04"
EXPECTED_SYSROOT_DIR="/opt/sysroot"
EXPECTED_PKG_CONFIG_LIBDIR="/opt/sysroot/usr/lib/pkgconfig:/opt/sysroot/lib/pkgconfig"

if [[ -f /etc/os-release ]]; then
  # shellcheck disable=SC1091
  . /etc/os-release
else
  echo "[check] ubuntu-version: fail (missing /etc/os-release)"
  exit 1
fi

if [[ "${VERSION_ID:-}" != "$EXPECTED_VERSION_ID" ]]; then
  echo "[check] ubuntu-version: fail (expected $EXPECTED_VERSION_ID, got ${VERSION_ID:-unset})"
  exit 1
fi
echo "[check] ubuntu-version: ok"

if [[ "${PKG_CONFIG_SYSROOT_DIR:-}" != "$EXPECTED_SYSROOT_DIR" ]]; then
  echo "[check] pkg-config-sysroot-dir: fail (expected $EXPECTED_SYSROOT_DIR, got ${PKG_CONFIG_SYSROOT_DIR:-unset})"
  exit 1
fi
echo "[check] pkg-config-sysroot-dir: ok"

if [[ "${PKG_CONFIG_LIBDIR:-}" != "$EXPECTED_PKG_CONFIG_LIBDIR" ]]; then
  echo "[check] pkg-config-libdir: fail (expected $EXPECTED_PKG_CONFIG_LIBDIR, got ${PKG_CONFIG_LIBDIR:-unset})"
  exit 1
fi
echo "[check] pkg-config-libdir: ok"

if [[ -d /opt/sysroot/usr/include ]]; then
  echo "[check] sysroot-usr-include: ok"
else
  echo "[check] sysroot-usr-include: fail (missing /opt/sysroot/usr/include)"
  exit 1
fi

if [[ -d /opt/sysroot/usr/lib/pkgconfig ]]; then
  echo "[check] sysroot-pkgconfig-dir: ok"
else
  echo "[check] sysroot-pkgconfig-dir: fail (missing /opt/sysroot/usr/lib/pkgconfig)"
  exit 1
fi

if [[ -e /opt/sysroot/lib/ld-linux-armhf.so.3 ]] || [[ -e /opt/sysroot/lib/arm-linux-gnueabihf/ld-linux-armhf.so.3 ]]; then
  echo "[check] firmware-dynamic-linker: ok"
else
  echo "[check] firmware-dynamic-linker: fail (missing ld-linux-armhf.so.3 in expected sysroot locations)"
  exit 1
fi

echo "[success] toolchain verification passed"
