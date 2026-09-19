#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE_NAME="xiaoai-plus-toolchain:dev"
SHAIRPORT_VERSION="4.3.5"
SHAIRPORT_SRC_DIR="${PROJECT_ROOT}/assets/shairport-sync-src"
OUTPUT_BIN="${PROJECT_ROOT}/assets/shairport-sync"

# If already built and cached, skip
if [ -f "${OUTPUT_BIN}" ]; then
  echo "==> Static shairport-sync already exists at ${OUTPUT_BIN}, skipping build."
  exit 0
fi

# Ensure toolchain docker image exists
make -C "${PROJECT_ROOT}/toolchain" build

# Clone shairport-sync if not present
if [ ! -d "${SHAIRPORT_SRC_DIR}" ]; then
  echo "==> Cloning shairport-sync ${SHAIRPORT_VERSION}..."
  git clone --depth 1 --branch "${SHAIRPORT_VERSION}" \
    https://github.com/mikebrady/shairport-sync.git "${SHAIRPORT_SRC_DIR}"
fi

echo "==> Building static shairport-sync for ARMv7..."
docker run --rm -i \
  -v "${PROJECT_ROOT}":/workspace \
  -w /workspace/assets/shairport-sync-src \
  "${IMAGE_NAME}" \
  bash << 'EOF'
set -euo pipefail
git config --global --add safe.directory "*"

# Compile static libasound.a if missing in container
if [ ! -f /usr/lib/arm-linux-gnueabihf/libasound.a ]; then
  echo "==> Building static libasound.a for ARMv7..."
  mkdir -p /tmp/alsa
  cd /tmp/alsa
  ALSA_VER="1.2.12"
  curl -fsSL "https://www.alsa-project.org/files/pub/lib/alsa-lib-${ALSA_VER}.tar.bz2" -o alsa.tar.bz2 || \
  curl -fsSL "https://github.com/alsa-project/alsa-lib/releases/download/v${ALSA_VER}/alsa-lib-${ALSA_VER}.tar.bz2" -o alsa.tar.bz2
  tar -xjf alsa.tar.bz2
  cd "alsa-lib-${ALSA_VER}"
  ./configure \
    --host=arm-linux-gnueabihf \
    --enable-static \
    --disable-shared \
    --prefix=/usr \
    --libdir=/usr/lib/arm-linux-gnueabihf \
    --disable-python
  make -j$(nproc)
  make install
  cp -a /usr/lib/arm-linux-gnueabihf/libasound.a /opt/sysroot/usr/lib/ 2>/dev/null || true
  rm -rf /tmp/alsa
  cd /workspace/assets/shairport-sync-src
fi

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
EOF

echo "==> Static shairport-sync built successfully: ${OUTPUT_BIN}"
ls -lh "${OUTPUT_BIN}"
