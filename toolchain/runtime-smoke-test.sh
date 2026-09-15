#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BINARY_PATH="${PROJECT_ROOT}/build-armv7/xiaoai_plus_speaker"
CONFIG_PATH="${PROJECT_ROOT}/dist/runtime-test-config.ini"
IMAGE_NAME="xiaoai-plus-runtime-test:dev"
RUN_SECONDS=8

if [[ ! -x "${BINARY_PATH}" ]]; then
  echo "错误：未找到可执行文件 ${BINARY_PATH}" >&2
  echo "请先执行：make -C toolchain compile" >&2
  exit 1
fi

mkdir -p "${PROJECT_ROOT}/dist"

cat > "${CONFIG_PATH}" <<'EOF'
[realtime]
api_key = runtime_test_api_key
model = gemini-3.1-flash-live-preview
bot_name = runtime_test_bot
system_role = you are running in docker smoke test.
speaking_style = concise and clear.
voice = Kore

[wakeup]
say_hello = hello from runtime test

[audio]
playback_gain = 1.0
EOF

echo "[runtime-smoke] build runtime image: ${IMAGE_NAME}"
echo "[runtime-smoke] ensure base toolchain image: xiaoai-plus-toolchain:dev"
docker build \
  -t xiaoai-plus-toolchain:dev \
  "${PROJECT_ROOT}/toolchain"

docker build \
  -t "${IMAGE_NAME}" \
  -f "${PROJECT_ROOT}/toolchain/runtime-test.Dockerfile" \
  "${PROJECT_ROOT}/toolchain"

echo "[runtime-smoke] start app in container for ${RUN_SECONDS}s"
docker run --rm \
  -v "${PROJECT_ROOT}:/workspace" \
  -w /workspace \
  "${IMAGE_NAME}" \
  bash -lc '
set -euo pipefail
qemu-arm-static ./build-armv7/xiaoai_plus_speaker -c dist/runtime-test-config.ini &
pid=$!
sleep '"${RUN_SECONDS}"'
if ! kill -0 "$pid" 2>/dev/null; then
  wait "$pid"
fi
kill -TERM "$pid"
wait "$pid"
'

echo "[runtime-smoke] success"
