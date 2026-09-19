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
    set -euo pipefail
    git config --global --add safe.directory "*"

    # Build static libasound.a if not already installed in container
    if [ ! -f /usr/lib/arm-linux-gnueabihf/libasound.a ]; then
      echo "==> Compiling static libasound.a for ARMv7..."
      ALSA_VERSION="1.2.12"
      curl -fL "https://www.alsa-project.org/files/pub/lib/alsa-lib-${ALSA_VERSION}.tar.bz2" -o /tmp/alsa-lib.tar.bz2 || \
      curl -fL "https://github.com/alsa-project/alsa-lib/releases/download/v${ALSA_VERSION}/alsa-lib-${ALSA_VERSION}.tar.bz2" -o /tmp/alsa-lib.tar.bz2
      tar -xjf /tmp/alsa-lib.tar.bz2 -C /tmp
      cd "/tmp/alsa-lib-${ALSA_VERSION}"
      ./configure \
        --host=arm-linux-gnueabihf \
        --enable-static \
        --disable-shared \
        --prefix=/usr \
        --libdir=/usr/lib/arm-linux-gnueabihf \
        --disable-python
      make -j$(nproc)
      make install
      cp -a /usr/lib/arm-linux-gnueabihf/libasound.a /opt/sysroot/usr/lib/ 2>/dev/null |
