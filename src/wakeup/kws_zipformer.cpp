#include "wakeup/kws_zipformer.hpp"

#include <stdexcept>

#include <sherpa-onnx/c-api/cxx-api.h>

#include "common/log.hpp"

namespace xiaoai_plus::wakeup {

namespace {

constexpr float kKwsThreshold = 0.20f;

const auto kLog = xiaoai_plus::GetLogger("kws");

}  // namespace

struct ZipformerKwsEngine::Impl {
  sherpa_onnx::cxx::KeywordSpotter spotter;
  sherpa_onnx::cxx::OnlineStream stream;
  std::vector<float> samples;

  explicit Impl(const config::Wakeup& cfg)
      : spotter([&]() {
          sherpa_onnx::cxx::KeywordSpotterConfig c;
          c.model_config.model_type = "zipformer2";
          c.model_config.tokens = cfg.tokens_path;
          c.model_config.transducer.encoder = cfg.encoder_path;
          c.model_config.transducer.decoder = cfg.decoder_path;
          c.model_config.transducer.joiner = cfg.joiner_path;
          c.model_config.num_threads = 2;
          c.model_config.provider = "cpu";
          c.keywords_threshold = kKwsThreshold;
          c.keywords_score = 3.0f;
          c.num_trailing_blanks = 0;
          c.max_active_paths = 8;
          c.keywords_file = cfg.keywords_file;
          auto s = sherpa_onnx::cxx::KeywordSpotter::Create(c);
          if (s.Get() == nullptr) {
            kLog->error("kws model load failed: spotter is null "
                        "(check model files exist and memory/disk is sufficient)");
            throw std::runtime_error(
                "kws model load failed: "
                "check model files exist and memory/disk is sufficient");
          }
          return s;
        }()),
        stream(spotter.CreateStream()) {
    if (stream.Get() == nullptr) {
      kLog->error("kws model load failed: CreateStream returned null");
      throw std::runtime_error("kws model load failed: CreateStream returned null");
    }
  }

  std::optional<KwsHit> Accept(const uint8_t* pcm, size_t size_bytes, int sample_rate,
                               int channels, int bits_per_sample) {
    if (!pcm || size_bytes < 2 || bits_per_sample != 16 || channels != 1 || sample_rate <= 0) {
      return std::nullopt;
    }

    const size_t n = size_bytes / sizeof(int16_t);
    if (n == 0) {
      return std::nullopt;
    }
    const auto* in = reinterpret_cast<const int16_t*>(pcm);

    samples.resize(n);
    for (size_t i = 0; i < n; ++i) {
      samples[i] = static_cast<float>(in[i]) / 32768.0f;
    }

    stream.AcceptWaveform(sample_rate, samples.data(), static_cast<int32_t>(n));

    while (spotter.IsReady(&stream)) {
      spotter.Decode(&stream);
      auto r = spotter.GetResult(&stream);
      if (!r.keyword.empty()) {
        spotter.Reset(&stream);
        return KwsHit{.keyword = r.keyword};
      }
    }

    return std::nullopt;
  }

  void Reset() { stream = spotter.CreateStream(); }
};

ZipformerKwsEngine::ZipformerKwsEngine(config::Wakeup cfg)
    : cfg_(std::move(cfg)), impl_(std::make_unique<Impl>(cfg_)) {}

ZipformerKwsEngine::~ZipformerKwsEngine() = default;

std::optional<KwsHit> ZipformerKwsEngine::AcceptPcm16(const uint8_t* pcm, size_t size_bytes,
                                                       int sample_rate, int channels,
                                                       int bits_per_sample) {
  if (!impl_) {
    return std::nullopt;
  }
  return impl_->Accept(pcm, size_bytes, sample_rate, channels, bits_per_sample);
}

void ZipformerKwsEngine::Reset() {
  if (impl_) {
    impl_->Reset();
  }
}

}  // namespace xiaoai_plus::wakeup
