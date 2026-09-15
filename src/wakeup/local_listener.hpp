#pragma once

#include <chrono>
#include <memory>
#include <mutex>
#include <vector>

#include "wakeup/gate.hpp"
#include "wakeup/kws_engine.hpp"
#include "wakeup/trigger.hpp"

namespace xiaoai_plus::wakeup {

class LocalListener {
 public:
  struct Config {
    Gate* gate{nullptr};
    Trigger* trigger{nullptr};
    std::shared_ptr<IKwsEngine> kws_engine;
    int sample_rate{24000};
    int channels{1};
    int bit_depth{16};
    int min_trigger_interval_ms{1500};
  };

  explicit LocalListener(Config cfg);
  void AcceptPcm(const std::vector<uint8_t>& chunk);
  void Close();

 private:
  Config cfg_;
  std::mutex mu_;
  std::chrono::steady_clock::time_point last_trigger_{};
  bool closed_{false};
};

}  // namespace xiaoai_plus::wakeup
