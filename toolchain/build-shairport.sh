#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE_NAME="xiaoai-plus-toolchain:dev"
SHAIRPORT_VERSION="4.3.5"
SHAIRPORT_SRC_DIR="${PROJECT_ROOT}/assets/shairport-sync-src"
OUTPUT_BIN="${PROJECT_ROOT}/assets/shairport-sync"

# Ensure toolchain docker image exists
make -C "${PROJECT_ROOT}/toolchain" build

# Clone shairport-sync if not present
if [ ! -d "${SHAIRPORT_SRC_DIR}" ]; then
  echo "==> Cloning shairport-sync ${SHAIRPORT_VERSION}..."
  git clone --depth 1 --branch "${SHAIRPORT_VERSION}" \
    https://github.com/mikebrady/shairport-sync.git "${SHAIRPORT_SRC_DIR}"
fi

echo "==> Building static shairport-sync for ARMv7..."
docker run --rm \
  -v "${PROJECT_ROOT}":/workspace \
  -w /workspace/assets/shairport-sync-src \
  "${IMAGE_NAME}" \
  bash -lc '
    autoreconf -fi
    export PKG_CONFIG_PATH="/usr/lib/arm-linux-gnueabihf/pkgconfig:/usr/share/pkgconfig"
    export PKG_CONFIG_LIBDIR="/usr/lib/arm-linux-gnueabihf/pkgconfig:/usr/share/pkgconfig"
    export PKG_CONFIG_SYSROOT_DIR=""
    ./configure \
      --host=arm-linux-gnueabihf \
      --with-alsa \
      --with-tinysvcmdns \
      --with-ssl=openssl \
      --with-metadata \
      --without-soxr \
      CFLAGS="-O2 -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard" \
      LDFLAGS="-static -L/usr/lib/arm-linux-gnueabihf"
    make -j$(nproc)
    arm-linux-gnueabihf-strip shairport-sync
    cp shairport-sync /workspace/assets/shairport-sync
  '

echo "==> Static shairport-sync built successfully: ${OUTPUT_BIN}"
ls -lh "${OUTPUT_BIN}"
