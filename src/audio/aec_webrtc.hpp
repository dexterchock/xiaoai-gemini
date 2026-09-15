#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace xiaoai_plus::dsp {

class AecWebrtc {
 public:
  AecWebrtc(int sample_rate_hz, int channels);
  ~AecWebrtc();

  void AnalyzeReverseStream(const uint8_t* pcm, size_t size_bytes);
  std::vector<uint8_t> ProcessCaptureStream(const uint8_t* pcm, size_t size_bytes);

 private:
  struct Impl;

  std::unique_ptr<Impl> impl_;
  std::mutex mu_;
};

}  // namespace xiaoai_plus::dsp
