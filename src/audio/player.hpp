#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <sys/types.h>
#include <thread>
#include <vector>

#include "config/config.hpp"

namespace xiaoai_plus::audio {

class AplayPlayer {
 public:
  AplayPlayer(config::Audio cfg, int queue_frames);
  ~AplayPlayer();

  bool Start();
  bool Play(const std::vector<uint8_t>& chunk);
  void Interrupt();
  void Close();

  void SetOnChunkPlayed(std::function<void(const std::vector<uint8_t>&)> cb);

 private:
  bool OpenPipeLocked();
  void ClosePipeLocked(bool kill_now);
  void WriteLoop();

  config::Audio cfg_;
  int queue_frames_{128};

  std::mutex mu_;
  std::condition_variable cv_;
  std::deque<std::vector<uint8_t>> queue_;

  std::mutex cb_mu_;
  std::function<void(const std::vector<uint8_t>&)> on_chunk_played_;

  std::atomic<bool> running_{false};
  std::mutex pipe_mu_;
  int aplay_fd_{-1};
  pid_t aplay_pid_{-1};
  std::thread thread_;
};

}  // namespace xiaoai_plus::audio
