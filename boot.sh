#!/bin/sh
set -eu

# 纯自启动脚本：开机时拉起 xiaoai_plus_speaker

APP_DIR="/data/xiaoai-plus"
BIN_PATH="/data/xiaoai-plus/xiaoai_plus_speaker"
CFG_PATH="/data/xiaoai-plus/config.ini"
LOG_PATH="/data/xiaoai-plus/xiaoai_plus.log"
WAIT_HOST="223.5.5.5"
WAIT_SECONDS=60

if [ ! -x "${BIN_PATH}" ]; then
  echo "错误：未找到可执行文件 ${BIN_PATH}" >&2
  exit 1
fi

if [ ! -f "${CFG_PATH}" ]; then
  echo "错误：未找到配置文件 ${CFG_PATH}" >&2
  exit 1
fi

if command -v ping >/dev/null 2>&1; then
  i=0
  while [ "${i}" -lt "${WAIT_SECONDS}" ]; do
    if ping -c 1 "${WAIT_HOST}" >/dev/null 2>&1; then
      echo "网络已就绪"
      break
    fi
    if [ "${i}" -eq 0 ]; then
      echo "等待网络就绪中..."
    fi
    i=$((i + 1))
    sleep 1
  done
fi

PIDS="$(ps | grep '[x]iaoai_plus_speaker' | awk '{print $1}')"
if [ -n "${PIDS}" ]; then
  echo "检测到旧进程，正在停止..."
  kill ${PIDS} >/dev/null 2>&1 || true
  sleep 1
fi

cd "${APP_DIR}"
"${BIN_PATH}" -c "${CFG_PATH}" >>"${LOG_PATH}" 2>&1 &
echo "启动完成，日志文件：${LOG_PATH}"
