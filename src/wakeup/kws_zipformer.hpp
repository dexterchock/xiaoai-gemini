#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "config/config.hpp"
#include "wakeup/kws_engine.hpp"

namespace xiaoai_plus::wakeup {

class ZipformerKwsEngine final : public IKwsEngine {
 public:
  explicit ZipformerKwsEngine(config::Wakeup cfg);
  ~ZipformerKwsEngine() override;

  std::optional<KwsHit> AcceptPcm16(const uint8_t* pcm, size_t size_bytes, int sample_rate,
                                    int channels, int bits_per_sample) override;
  void Reset() override;

 private:
  struct Impl;

  config::Wakeup cfg_;
  std::unique_ptr<Impl> impl_;
};

}  // namespace xiaoai_plus::wakeup
