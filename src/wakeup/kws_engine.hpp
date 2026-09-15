#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace xiaoai_plus::wakeup {

struct KwsHit {
  std::string keyword;
};

class IKwsEngine {
 public:
  virtual ~IKwsEngine() = default;
  virtual std::optional<KwsHit> AcceptPcm16(const uint8_t* pcm, size_t size_bytes, int sample_rate,
                                            int channels, int bits_per_sample) = 0;
  virtual void Reset() = 0;
};

}  // namespace xiaoai_plus::wakeup
