#pragma once

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace xiaoai_plus::wakeup {

enum class Step {
  kIdle,
  kActive,
};

struct Hooks {
  std::function<void(const std::string&)> after_disarm;
  std::function<void(const std::string&)> on_arm;
};

class Gate {
 public:
  Gate(std::chrono::milliseconds timeout, Hooks hooks);
  ~Gate();

  Step step() const;
  bool TryWakeup(const std::string& keyword);
  void Activate(const std::string& reason);
  void Disarm(const std::string& reason);
  void RefreshTimeout();

  void SetAiSpeaking(bool is_speaking);
  void Close();

 private:
  void RefreshTimeoutLocked();
  void TimeoutLoop();

  std::chrono::milliseconds timeout_;
  Hooks hooks_;

  mutable std::mutex mu_;
  std::condition_variable timeout_cv_;
  Step step_{Step::kIdle};
  bool is_ai_speaking_{false};
  std::chrono::steady_clock::time_point timeout_deadline_{};
  bool closed_{false};
  uint64_t timeout_epoch_{0};
  std::thread timeout_thread_;
};

}  // namespace xiaoai_plus::wakeup
