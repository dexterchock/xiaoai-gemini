#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

OUTPUT_DIR="${PROJECT_ROOT}/dist/release"
PACKAGE_DIR="${OUTPUT_DIR}/oh2p"
ARCHIVE_NAME="oh2p.tar.gz"
ARCHIVE_PATH="${OUTPUT_DIR}/${ARCHIVE_NAME}"

BINARY_PATH="${PROJECT_ROOT}/build-armv7/xiaoai_plus_speaker"
CONFIG_EXAMPLE_PATH="${PROJECT_ROOT}/config.ini.example"
ASSET_DIR="${PROJECT_ROOT}/assets"

require_file() {
  local f="$1"
  if [[ ! -f "$f" ]]; then
    echo "missing required file: $f" >&2
    exit 1
  fi
}

require_file "${BINARY_PATH}"
require_file "${CONFIG_EXAMPLE_PATH}"
require_file "${ASSET_DIR}/decoder.onnx"
require_file "${ASSET_DIR}/encoder.onnx"
require_file "${ASSET_DIR}/joiner.onnx"
require_file "${ASSET_DIR}/keywords.txt"
require_file "${ASSET_DIR}/tokens.txt"

rm -rf "${PACKAGE_DIR}"
mkdir -p "${PACKAGE_DIR}/assets"

install -m 0755 "${BINARY_PATH}" "${PACKAGE_DIR}/xiaoai_plus_speaker"
install -m 0644 "${CONFIG_EXAMPLE_PATH}" "${PACKAGE_DIR}/config.ini.example"
install -m 0644 "${ASSET_DIR}/decoder.onnx" "${PACKAGE_DIR}/assets/decoder.onnx"
install -m 0644 "${ASSET_DIR}/encoder.onnx" "${PACKAGE_DIR}/assets/encoder.onnx"
install -m 0644 "${ASSET_DIR}/joiner.onnx" "${PACKAGE_DIR}/assets/joiner.onnx"
install -m 0644 "${ASSET_DIR}/keywords.txt" "${PACKAGE_DIR}/assets/keywords.txt"
install -m 0644 "${ASSET_DIR}/tokens.txt" "${PACKAGE_DIR}/assets/tokens.txt"

mkdir -p "${OUTPUT_DIR}"
tar -C "${OUTPUT_DIR}" -czf "${ARCHIVE_PATH}" "oh2p"
(cd "${OUTPUT_DIR}" && sha256sum "${ARCHIVE_NAME}" > "${ARCHIVE_NAME}.sha256")

echo "release archive: ${ARCHIVE_PATH}"
echo "checksum file: ${ARCHIVE_PATH}.sha256"
