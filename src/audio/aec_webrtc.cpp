#include "audio/aec_webrtc.hpp"

#include <algorithm>
#include <cstring>
#include <memory>
#include <mutex>

#include "common/log.hpp"
#include <webrtc/common.h>
#include <webrtc/modules/audio_processing/include/audio_processing.h>

namespace xiaoai_plus::dsp {

namespace {

const auto kLog = xiaoai_plus::GetLogger("aec");

void LogInitError(const char* action, int rc) {
  if (rc != 0) {
    kLog->warn("aec: {} failed: rc={}", action, rc);
  }
}

void CompactConsumedSamples(std::vector<int16_t>* pending, size_t* read_pos,
                            size_t compact_threshold) {
  if (!pending || !read_pos || *read_pos == 0) {
    return;
  }
  if (*read_pos >= pending->size()) {
    pending->clear();
    *read_pos = 0;
    return;
  }
  if (*read_pos >= compact_threshold) {
    pending->erase(pending->begin(),
                   pending->begin() + static_cast<long>(*read_pos));
    *read_pos = 0;
  }
}

}  // namespace

struct AecWebrtc::Impl {
  std::unique_ptr<webrtc::AudioProcessing> apm;
  webrtc::StreamConfig in_cfg;
  webrtc::StreamConfig out_cfg;
  std::vector<float> reverse_buf;
  std::vector<float> reverse_out;
  std::vector<float> capture_in;
  std::vector<float> capture_out;

  std::vector<int16_t> reverse_pending;
  size_t reverse_read_pos{0};
  std::vector<int16_t> capture_pending;
  size_t capture_read_pos{0};
  std::vector<int16_t> capture_out_i16;
  uint32_t reverse_error_count{0};
  uint32_t capture_error_count{0};
  uint32_t delay_error_count{0};

  Impl(int sample_rate_hz, int channels)
      : apm([]() {
          webrtc::Config apm_cfg;
          apm_cfg.Set<webrtc::DelayAgnostic>(new webrtc::DelayAgnostic(true));
          apm_cfg.Set<webrtc::ExtendedFilter>(new webrtc::ExtendedFilter(true));
          return std::unique_ptr<webrtc::AudioProcessing>(webrtc::AudioProcessing::Create(apm_cfg));
        }()),
        in_cfg(sample_rate_hz, channels, false),
        out_cfg(sample_rate_hz, channels, false) {
    LogInitError("enable echo_cancellation", apm->echo_cancellation()->Enable(true));
    LogInitError("set suppression level",
                 apm->echo_cancellation()->set_suppression_level(
                     webrtc::EchoCancellation::kHighSuppression));

    LogInitError("enable noise_suppression", apm->noise_suppression()->Enable(true));

    LogInitError("enable gain_control", apm->gain_control()->Enable(true));
    LogInitError("set gain_control mode",
                 apm->gain_control()->set_mode(webrtc::GainControl::kAdaptiveDigital));

    LogInitError("initialize", apm->Initialize());

    const size_t frame_samples = FrameSamples();
    if (frame_samples > 0) {
      reverse_buf.resize(frame_samples);
      reverse_out.resize(frame_samples);
      capture_in.resize(frame_samples);
      capture_out.resize(frame_samples);
      reverse_pending.reserve(frame_samples * 12);
      capture_pending.reserve(frame_samples * 12);
      capture_out_i16.reserve(frame_samples * 12);
    }
  }

  size_t FrameSamples() const {
    return static_cast<size_t>(in_cfg.sample_rate_hz() / 100) * in_cfg.num_channels();
  }
};

AecWebrtc::AecWebrtc(int sample_rate_hz, int channels)
    : impl_(std::make_unique<Impl>(sample_rate_hz, channels)) {}

AecWebrtc::~AecWebrtc() = default;

void AecWebrtc::AnalyzeReverseStream(const uint8_t* pcm, size_t size_bytes) {
  if (!pcm || size_bytes < 2 || !impl_) {
    return;
  }

  std::lock_guard<std::mutex> lock(mu_);

  const auto* s = reinterpret_cast<const int16_t*>(pcm);
  const size_t n = size_bytes / 2;
  impl_->reverse_pending.insert(impl_->reverse_pending.end(), s, s + static_cast<long>(n));

  const size_t frame_samples = impl_->FrameSamples();
  if (frame_samples == 0) {
    return;
  }
  const size_t max_pending = frame_samples * 10;
  const size_t reverse_available = impl_->reverse_pending.size() - impl_->reverse_read_pos;
  if (reverse_available > max_pending) {
    impl_->reverse_read_pos += (reverse_available - max_pending);
        kLog->warn("aec: dropped reverse samples (overflow)");
  }
  while (impl_->reverse_pending.size() - impl_->reverse_read_pos >= frame_samples) {
    const int16_t* frame =
        impl_->reverse_pending.data() + static_cast<long>(impl_->reverse_read_pos);
    for (size_t i = 0; i < frame_samples; ++i) {
      impl_->reverse_buf[i] = static_cast<float>(frame[i]) / 32768.0f;
    }
    impl_->reverse_read_pos += frame_samples;

    const float* reverse_src[] = {impl_->reverse_buf.data()};
    float* reverse_dst[] = {impl_->reverse_out.data()};
    const int rc =
        impl_->apm->ProcessReverseStream(reverse_src, impl_->in_cfg, impl_->out_cfg, reverse_dst);
    if (rc != 0) {
      ++impl_->reverse_error_count;
      if (impl_->reverse_error_count == 1 || impl_->reverse_error_count % 200 == 0) {
        kLog->warn("aec: ProcessReverseStream failed: rc={} count={}", rc,
                     impl_->reverse_error_count);
      }
    }
  }
  CompactConsumedSamples(&impl_->reverse_pending, &impl_->reverse_read_pos, frame_samples * 4);
}

std::vector<uint8_t> AecWebrtc::ProcessCaptureStream(const uint8_t* pcm, size_t size_bytes) {
  if (!pcm || size_bytes == 0 || !impl_) {
    return {};
  }

  std::lock_guard<std::mutex> lock(mu_);

  const auto* s = reinterpret_cast<const int16_t*>(pcm);
  const size_t n = size_bytes / 2;
  impl_->capture_pending.insert(impl_->capture_pending.end(), s, s + static_cast<long>(n));

  const size_t frame_samples = impl_->FrameSamples();
  if (frame_samples == 0) {
    return {};
  }
  const size_t max_pending = frame_samples * 10;
  const size_t capture_available = impl_->capture_pending.size() - impl_->capture_read_pos;
  if (capture_available > max_pending) {
    impl_->capture_read_pos += (capture_available - max_pending);
        kLog->warn("aec: dropped capture samples (overflow)");
  }
  while (impl_->capture_pending.size() - impl_->capture_read_pos >= frame_samples) {
    const int16_t* frame =
        impl_->capture_pending.data() + static_cast<long>(impl_->capture_read_pos);
    for (size_t i = 0; i < frame_samples; ++i) {
      impl_->capture_in[i] = static_cast<float>(frame[i]) / 32768.0f;
    }
    impl_->capture_read_pos += frame_samples;

    const float* capture_src[] = {impl_->capture_in.data()};
    float* capture_dst[] = {impl_->capture_out.data()};
    const int delay_rc = impl_->apm->set_stream_delay_ms(0);
    if (delay_rc != 0) {
      ++impl_->delay_error_count;
      if (impl_->delay_error_count == 1 || impl_->delay_error_count % 200 == 0) {
        kLog->warn("aec: set_stream_delay_ms failed: rc={} count={}", delay_rc,
                     impl_->delay_error_count);
      }
    }
    const int rc = impl_->apm->ProcessStream(capture_src, impl_->in_cfg, impl_->out_cfg, capture_dst);
    if (rc != 0) {
      ++impl_->capture_error_count;
      if (impl_->capture_error_count == 1 || impl_->capture_error_count % 200 == 0) {
        kLog->warn("aec: ProcessStream failed: rc={} count={}", rc, impl_->capture_error_count);
      }
      impl_->capture_out_i16.insert(impl_->capture_out_i16.end(), frame,
                                    frame + static_cast<long>(frame_samples));
      continue;
    }

    for (float sample : impl_->capture_out) {
      float clamped = std::max(-1.0f, std::min(1.0f, sample));
      impl_->capture_out_i16.push_back(static_cast<int16_t>(clamped * 32767.0f));
    }
  }
  CompactConsumedSamples(&impl_->capture_pending, &impl_->capture_read_pos, frame_samples * 4);

  const size_t out_bytes = impl_->capture_out_i16.size() * sizeof(int16_t);
  if (out_bytes == 0) {
    return {};
  }

  std::vector<uint8_t> out(out_bytes);
  std::memcpy(out.data(), impl_->capture_out_i16.data(), out_bytes);
  impl_->capture_out_i16.clear();
  return out;
}

}  // namespace xiaoai_plus::dsp
