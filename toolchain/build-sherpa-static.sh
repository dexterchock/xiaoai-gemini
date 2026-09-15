#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE_NAME="xiaoai-plus-toolchain:dev"
SHERPA_VERSION="v1.12.25"
SHERPA_SRC_DIR="${PROJECT_ROOT}/assets/sherpa-onnx-src"
SHERPA_STATIC_DIR="${PROJECT_ROOT}/assets/sherpa_onnx_static"

# Ensure Docker image exists.
make -C "${PROJECT_ROOT}/toolchain" build

# Clone sherpa-onnx source if not present.
if [ ! -d "${SHERPA_SRC_DIR}" ]; then
  echo "==> Cloning sherpa-onnx ${SHERPA_VERSION} ..."
  git clone --depth 1 --branch "${SHERPA_VERSION}" \
    https://github.com/k2-fsa/sherpa-onnx.git "${SHERPA_SRC_DIR}"
fi

echo "==> Building sherpa-onnx static libraries for ARM ..."
docker run --rm \
  -v "${PROJECT_ROOT}":/workspace \
  -w /workspace/assets/sherpa-onnx-src \
  "${IMAGE_NAME}" \
  bash -lc '
    mkdir -p build-arm-static && cd build-arm-static
    SYSROOT=/opt/sysroot cmake .. \
      -DCMAKE_TOOLCHAIN_FILE=/workspace/armv7-linux-gnueabihf.cmake \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/workspace/assets/sherpa_onnx_static \
      -DBUILD_SHARED_LIBS=OFF \
      -DSHERPA_ONNX_ENABLE_C_API=ON \
      -DSHERPA_ONNX_ENABLE_BINARY=OFF \
      -DSHERPA_ONNX_ENABLE_TESTS=OFF \
      -DSHERPA_ONNX_ENABLE_PYTHON=OFF \
      -DSHERPA_ONNX_ENABLE_TTS=OFF \
      -DSHERPA_ONNX_ENABLE_SPEAKER_DIARIZATION=OFF \
      -DSHERPA_ONNX_ENABLE_PORTAUDIO=OFF \
      -DSHERPA_ONNX_ENABLE_WEBSOCKET=OFF
    cmake --build . -j$(nproc)
    cmake --install .
  '

echo "==> Static libraries installed to ${SHERPA_STATIC_DIR}"
ls -lh "${SHERPA_STATIC_DIR}/lib/"*.a 2>/dev/null || ls -lhR "${SHERPA_STATIC_DIR}/lib/"
