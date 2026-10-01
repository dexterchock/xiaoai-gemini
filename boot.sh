#!/bin/sh
set -eu

APP_DIR="/data/xiaoai-plus"
BIN_PATH="/data/xiaoai-plus/xiaoai_plus_speaker"
CFG_PATH="/data/xiaoai-plus/config.ini"
LOG_PATH="/data/xiaoai-plus/xiaoai_plus.log"
AIRPLAY_BIN="/data/xiaoai-plus/shairport-sync"
AIRPLAY_CFG="/data/xiaoai-plus/shairport-sync.conf"
AIRPLAY_LOG="/data/xiaoai-plus/shairport.log"
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

PIDS="$(ps | grep '[x]iaoai_plus_speaker' | awk '{print $1}' || true)"
if [ -n "${PIDS}" ]; then
  echo "检测到旧语音助手进程，正在停止..."
  kill ${PIDS} >/dev/null 2>&1 || true
fi

PIDS_AP="$(ps | grep '[s]hairport-sync' | awk '{print $1}' || true)"
if [ -n "${PIDS_AP}" ]; then
  echo "检测到旧 AirPlay 进程，正在停止..."
  # 进程可能被 SIGSTOP 冻结，先恢复再终止，否则 SIGTERM 不会生效
  kill -CONT ${PIDS_AP} >/dev/null 2>&1 || true
  kill ${PIDS_AP} >/dev/null 2>&1 || true
fi
sleep 1

cd "${APP_DIR}"

# 注意：当前 shairport-sync 编译时未启用 libdaemon，不支持 -d / -j 守护化参数，
# 使用它们会直接退出。这里改用 shell 后台运行，并把日志写入文件以便排查。
if [ -x "${AIRPLAY_BIN}" ] && [ -f "${AIRPLAY_CFG}" ]; then
  echo "启动 AirPlay 接收服务..."
  "${AIRPLAY_BIN}" -c "${AIRPLAY_CFG}" >"${AIRPLAY_LOG}" 2>&1 &
  sleep 1
  if ! ps | grep -q '[s]hairport-sync'; then
    echo "警告：AirPlay 启动失败，请查看 ${AIRPLAY_LOG}" >&2
  fi
else
  echo "跳过 AirPlay：未找到 ${AIRPLAY_BIN} 或 ${AIRPLAY_CFG}" >&2
fi

"${BIN_PATH}" -c "${CFG_PATH}" >>"${LOG_PATH}" 2>&1 &
echo "启动完成，日志文件：${LOG_PATH}"
