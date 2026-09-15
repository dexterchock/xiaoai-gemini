#pragma once

#include <atomic>
#include <functional>
#include <thread>
#include <vector>

#include "config/config.hpp"

namespace xiaoai_plus::audio {

class ArecordRecorder {
 public:
  explicit ArecordRecorder(config::Audio cfg);
  ~ArecordRecorder();

  bool Start(std::function<void(const std::vector<uint8_t>&)> on_chunk);
  void Stop();

 private:
  void ReadLoop(std::function<void(const std::vector<uint8_t>&)> on_chunk);

  config::Audio cfg_;
  std::atomic<bool> running_{false};
  int pipe_fd_{-1};
  int child_pid_{-1};
  std::thread thread_;
};

}  // namespace xiaoai_plus::audio
