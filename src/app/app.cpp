#include "app/app.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <thread>

#include "common/log.hpp"
#include "wakeup/kws_zipformer.hpp"

namespace xiaoai_plus::app {

namespace {

const auto kLog = xiaoai_plus::GetLogger("app");

constexpr int kWakeupTimeoutSec = 60;
constexpr int kWelcomeResponseTimeoutSec = 8;
constexpr float kKwsThreshold = 0.20f;
constexpr int kMinTriggerIntervalMs = 800;

struct EchoStats {
  double mic_rms{0.0};
  double ref_rms{0.0};
  double corr{0.0};
  double ratio{0.0};
  bool valid_ref{false};
};

EchoStats AnalyzeEcho(const std::vector<uint8_t>& mic_mono,
                      const std::vector<uint8_t>& reverse_ref) {
  EchoStats stats;
  if (mic_mono.empty() || reverse_ref.empty()) {
    return stats;
  }
  const size_t n = std::min(mic_mono.size(), reverse_ref.size()) / sizeof(int16_t);
  if (n == 0) {
    return stats;
  }

  const auto* mic = reinterpret_cast<const int16_t*>(mic_mono.data());
  const auto* ref = reinterpret_cast<const int16_t*>(reverse_ref.data());

  double mic_energy = 0.0;
  double ref_energy = 0.0;
  double cross = 0.0;
  for (size_t i = 0; i < n; ++i) {
    const double m = static_cast<double>(mic[i]);
    const double r = static_cast<double>(ref[i]);
    mic_energy += m * m;
    ref_energy += r * r;
    cross += m * r;
  }
  stats.mic_rms = std::sqrt(mic_energy / static_cast<double>(n));
  stats.ref_rms = std::sqrt(ref_energy / static_cast<double>(n));

  if (ref_energy < 1e7) {
    return stats;
  }
  stats.valid_ref = true;

  stats.corr = std::abs(cross) / std::sqrt((mic_energy + 1.0) * (ref_energy + 1.0));
  stats.ratio = std::sqrt((mic_energy + 1.0) / (ref_energy + 1.0));
  return stats;
}

void ScalePcmS16InPlace(std::vector<uint8_t>* pcm, float gain) {
  if (!pcm || pcm->empty() || pcm->size() % sizeof(int16_t) != 0) {
    return;
  }
  const float safe_gain = std::max(0.0f, gain);
  if (safe_gain >= 0.999f && safe_gain <= 1.001f) {
    return;
  }
  auto* samples = reinterpret_cast<int16_t*>(pcm->data());
  const size_t n = pcm->size() / sizeof(int16_t);
  for (size_t i = 0; i < n; ++i) {
    const float scaled = static_cast<float>(samples[i]) * safe_gain;
    const int32_t v = static_cast<int32_t>(std::round(scaled));
    samples[i] = static_cast<int16_t>(std::max(-32768, std::min(32767, v)));
  }
}

float ComputeUplinkGainWhileAiSpeaking(const EchoStats& stats) {
  if (!stats.valid_ref) {
    return 0.30f;
  }

  const bool echo_dominant =
      (stats.ref_rms > 1200.0 && stats.ratio < 0.55 && stats.corr > 0.06);

  const bool has_near_end_speech = (stats.corr < 0.04 || stats.ratio > 0.70);

  if (echo_dominant) {
    return has_near_end_speech ? 0.34f : 0.006f;
  }

  return has_near_end_speech ? 0.55f : 0.18f;
}

std::string TrimSpace(std::string s) {
  auto not_space = [](unsigned char c) { return !std::isspace(c); };
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
  s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
  return s;
}

std::vector<std::string> LoadWakeupKeywords(const std::string& keywords_file) {
  std::ifstream in(keywords_file);
  if (!in.is_open()) {
    throw std::runtime_error("failed to open wakeup.keywords_file: " + keywords_file);
  }

  std::vector<std::string> keywords;
  std::string line;
  int lineno = 0;
  while (std::getline(in, line)) {
    ++lineno;
    line = TrimSpace(std::move(line));
    if (line.empty() || line.front() == '#' || line.front() == ';') {
      continue;
    }

    const auto at = line.rfind('@');
    if (at == std::string::npos) {
      throw std::runtime_error("invalid wakeup.keywords_file line " + std::to_string(lineno) +
                               ": missing '@' label");
    }
    auto keyword = TrimSpace(line.substr(at + 1));
    if (keyword.empty()) {
      throw std::runtime_error("invalid wakeup.keywords_file line " + std::to_string(lineno) +
                               ": empty keyword label");
    }
    keywords.push_back(std::move(keyword));
  }

  std::sort(keywords.begin(), keywords.end());
  keywords.erase(std::unique(keywords.begin(), keywords.end()), keywords.end());
  if (keywords.empty()) {
    throw std::runtime_error("wakeup.keywords_file has no valid keyword label: " +
                             keywords_file);
  }

  return keywords;
}

}  // namespace

App::App(config::Config cfg) : cfg_(std::move(cfg)) {
  cfg_.audio.input_device = "noop";
  cfg_.audio.output_device = "notify";
  cfg_.audio.sample_rate = 16000;
  cfg_.audio.channels = 4;
  cfg_.audio.bits_per_sample = 32;
  cfg_.audio.buffer_size = 1440;
  cfg_.audio.period_size = 360;

  recorder_ = std::make_unique<audio::ArecordRecorder>(cfg_.audio);

  config::Audio player_audio = cfg_.audio;
  player_audio.sample_rate = 24000;
  player_audio.channels = 1;
  player_audio.bits_per_sample = 16;
  player_ = std::make_unique<audio::AplayPlayer>(player_audio, cfg_.budget.output_queue_frames);

  aec_ = std::make_unique<dsp::AecWebrtc>(cfg_.audio.sample_rate, 1);

  wakeup::Hooks hooks;
  hooks.after_disarm = [this](const std::string&) {
    CancelWelcomeTimer();
    // Resume AirPlay audio when voice conversation ends
    (void)std::system("killall -CONT shairport-sync >/dev/null 2>&1");
  };
  hooks.on_arm = [this](const std::string& reason) {
    // Run OnArm asynchronously so audio capture is not blocked during TLS handshake
    std::thread([this, reason]() { OnArm(reason); }).detach();
  };

  gate_ = std::make_unique<wakeup::Gate>(std::chrono::seconds(kWakeupTimeoutSec), hooks);

  auto wakeup_keywords = LoadWakeupKeywords(cfg_.wakeup.keywords_file);
  kLog->info("loaded {} wakeup keyword labels from '{}'", wakeup_keywords.size(),
             cfg_.wakeup.keywords_file);
  trigger_ = std::make_unique<wakeup::Trigger>(
      std::move(wakeup_keywords),
      [this](const std::string& keyword) {
        return gate_->TryWakeup(keyword);
      });

  auto kws_engine = std::make_shared<wakeup::ZipformerKwsEngine>(cfg_.wakeup);
  wakeup::LocalListener::Config kws_cfg;
  kws_cfg.gate = gate_.get();
  kws_cfg.trigger = trigger_.get();
  kws_cfg.kws_engine = std::move(kws_engine);
  kws_cfg.sample_rate = cfg_.audio.sample_rate;
  kws_cfg.channels = 1;
  kws_cfg.bit_depth = 16;
  kws_cfg.min_trigger_interval_ms = kMinTriggerIntervalMs;
  kws_listener_ = std::make_unique<wakeup::LocalListener>(kws_cfg);

  realtime::Client::Callbacks callbacks;
  callbacks.on_audio = [this](const std::vector<uint8_t>& chunk) { OnAudio(chunk); };
  callbacks.on_set_ai_speaking = [this](bool speaking) { OnSetAiSpeaking(speaking); };
  callbacks.on_asr_final = [this](const std::string& text) { OnAsrFinal(text); };
  callbacks.on_user_activity = [this]() { OnUserActivity(); };
  callbacks.on_session_closed = [this](const std::string& reason) { OnSessionClosed(reason); };
  callbacks.on_chat_ended = [this]() { OnChatEnded(); };
  client_ = std::make_unique<realtime::Client>(cfg_, callbacks);

  player_->SetOnChunkPlayed([this](const std::vector<uint8_t>&) {
    OnPlaybackChunkPlayed();
  });
}

App::~App() { Stop(); }

bool App::Run() {
  kLog->info("app config: dev={}, rate={}, ch={}, bits={}, buf={}, period={}, kws_thresh={}",
           cfg_.audio.input_device, cfg_.audio.sample_rate, cfg_.audio.channels,
           cfg_.audio.bits_per_sample, cfg_.audio.buffer_size, cfg_.audio.period_size,
           kKwsThreshold);

  {
    std::lock_guard<std::mutex> lock(state_mu_);
    lifecycle_ = Lifecycle::kRunning;
  }

  running_.store(true);

  if (!player_->Start()) {
    kLog->error("app start failed: player start");
    running_.store(false);
    return false;
  }
  if (!recorder_->Start([this](const std::vector<uint8_t>& chunk) { OnInputAudio(chunk); })) {
    kLog->error("app start failed: recorder start");
    running_.store(false);
    player_->Close();
    return false;
  }
  if (!client_->Start()) {
    kLog->error("app start failed: realtime client start");
    running_.store(false);
    recorder_->Stop();
    player_->Close();
    return false;
  }
  kLog->info("app components started");

  std::thread([]() {
    const int rc =
        std::system("/usr/sbin/tts_play.sh '谷歌已启动' >/dev/null 2>&1");
    if (rc != 0) {
      kLog->warn("startup tts command failed: rc={}", rc);
    }
  }).detach();

  {
    std::unique_lock<std::mutex> lock(run_mu_);
    run_cv_.wait(lock, [this]() { return !running_.load(); });
  }

  {
    std::lock_guard<std::mutex> lock(state_mu_);
    lifecycle_ = Lifecycle::kStopped;
  }
  return true;
}

void App::Stop() {
  if (stopping_.exchange(true)) {
    return;
  }
  kLog->info("app stopping");
  running_.store(false);
  run_cv_.notify_all();

  {
    std::lock_guard<std::mutex> lock(state_mu_);
    lifecycle_ = Lifecycle::kStopping;
  }

  CancelWelcomeTimer();
  if (gate_) gate_->Close();
  if (kws_listener_) kws_listener_->Close();
  if (client_) client_->Stop();
  if (recorder_) recorder_->Stop();
  if (player_) player_->Close();

  {
    std::lock_guard<std::mutex> lock(state_mu_);
    lifecycle_ = Lifecycle::kStopped;
  }
  kLog->info("app stopped");
}

Lifecycle App::State() const {
  std::lock_guard<std::mutex> lock(state_mu_);
  return lifecycle_;
}

void App::ConvertS32ToS16(const std::vector<uint8_t>& chunk, std::vector<uint8_t>* out) {
  if (!out) {
    return;
  }
  if (chunk.size() % sizeof(int32_t) != 0) {
    out->assign(chunk.begin(), chunk.end());
    return;
  }
  const size_t n = chunk.size() / sizeof(int32_t);
  const auto* in = reinterpret_cast<const int32_t*>(chunk.data());
  out->resize(n * sizeof(int16_t));
  auto* dst = reinterpret_cast<int16_t*>(out->data());
  for (size_t i = 0; i < n; ++i) {
    int32_t s = in[i] >> 8;
    if (s > 32767) s = 32767;
    if (s < -32768) s = -32768;
    dst[i] = static_cast<int16_t>(s);
  }
}

void App::DownmixToMono(const std::vector<uint8_t>& chunk, int channels,
                        std::vector<uint8_t>* out) {
  if (!out) {
    return;
  }
  if (channels <= 1 || chunk.empty()) {
    out->assign(chunk.begin(), chunk.end());
    return;
  }
  const size_t stride = static_cast<size_t>(channels);
  if (chunk.size() % (sizeof(int16_t) * stride) != 0) {
    out->assign(chunk.begin(), chunk.end());
    return;
  }
  const auto* in = reinterpret_cast<const int16_t*>(chunk.data());
  const size_t n_frames = chunk.size() / (sizeof(int16_t) * stride);

  const int mic_channels = (channels == 4) ? 3 : std::max(1, channels);

  out->resize(n_frames * sizeof(int16_t));
  auto* dst = reinterpret_cast<int16_t*>(out->data());

  if (mic_channels < 2) {
    for (size_t i = 0; i < n_frames; ++i) {
      dst[i] = in[i * stride];
    }
    return;
  }

  static constexpr int kMaxLag = 3;

  auto xcorr_lag = [&](int ch_a, int ch_b) -> int {
    int best_lag = 0;
    int64_t best = INT64_MIN;
    for (int lag = -kMaxLag; lag <= kMaxLag; ++lag) {
      const size_t t0 = static_cast<size_t>(std::max(0, -lag));
      const size_t t1 = n_frames - static_cast<size_t>(std::max(0, lag));
      int64_t s = 0;
      for (size_t t = t0; t < t1; ++t) {
        s += static_cast<int64_t>(in[t * stride + ch_a]) *
             static_cast<int64_t>(in[(static_cast<int>(t) + lag) * stride + ch_b]);
      }
      if (s > best) {
        best = s;
        best_lag = lag;
      }
    }
    return best_lag;
  };

  const int lag1 = xcorr_lag(0, 1);
  const int lag2 = (mic_channels >= 3) ? xcorr_lag(0, 2) : 0;

  const int n = static_cast<int>(n_frames);
  for (int t = 0; t < n; ++t) {
    int32_t sum = static_cast<int32_t>(in[t * stride]);
    const int t1 = std::max(0, std::min(n - 1, t + lag1));
    sum += static_cast<int32_t>(in[static_cast<size_t>(t1) * stride + 1]);
    if (mic_channels >= 3) {
      const int t2 = std::max(0, std::min(n - 1, t + lag2));
      sum += static_cast<int32_t>(in[static_cast<size_t>(t2) * stride + 2]);
    }
    dst[t] = static_cast<int16_t>(sum / mic_channels);
  }
}

void App::ExtractChannelS16(const std::vector<uint8_t>& chunk, int channels, int channel,
                            std::vector<uint8_t>* out) {
  if (!out) {
    return;
  }
  if (channels <= 1 || channel < 0 || channel >= channels) {
    out->clear();
    return;
  }
  const size_t stride = static_cast<size_t>(channels);
  if (chunk.size() % (sizeof(int16_t) * stride) != 0) {
    out->clear();
    return;
  }
  const auto* in = reinterpret_cast<const int16_t*>(chunk.data());
  const size_t n_frames = chunk.size() / (sizeof(int16_t) * stride);

  out->resize(n_frames * sizeof(int16_t));
  auto* dst = reinterpret_cast<int16_t*>(out->data());
  for (size_t i = 0; i < n_frames; ++i) {
    dst[i] = in[i * stride + static_cast<size_t>(channel)];
  }
}

void App::OnInputAudio(const std::vector<uint8_t>& chunk) {
  ConvertS32ToS16(chunk, &s16_buf_);
  if (cfg_.audio.channels >= 2) {
    ExtractChannelS16(s16_buf_, cfg_.audio.channels, 0, &capture_mono_buf_);
  } else {
    capture_mono_buf_.clear();
  }
  if (capture_mono_buf_.empty()) {
    DownmixToMono(s16_buf_, cfg_.audio.channels, &capture_mono_buf_);
  }
  DownmixToMono(s16_buf_, cfg_.audio.channels, &kws_mono_buf_);
  if (cfg_.audio.channels >= 4) {
    ExtractChannelS16(s16_buf_, cfg_.audio.channels, 3, &reverse_ref_buf_);
  } else {
    reverse_ref_buf_.clear();
  }
  EchoStats echo_stats;
  if (!reverse_ref_buf_.empty()) {
    echo_stats = AnalyzeEcho(capture_mono_buf_, reverse_ref_buf_);
  }
  if (aec_ && !reverse_ref_buf_.empty()) {
    aec_->AnalyzeReverseStream(reverse_ref_buf_.data(), reverse_ref_buf_.size());
  }
  if (kws_listener_) {
    kws_listener_->AcceptPcm(kws_mono_buf_);
  }
  if (!gate_ || gate_->step() != wakeup::Step::kActive) {
    return;
  }
  if (client_) {
    bool ai_speaking = false;
    {
      std::lock_guard<std::mutex> lock(mu_);
      ai_speaking = is_ai_speaking_ || (pending_playback_chunks_ > 0);
    }

    const std::vector<uint8_t>* api_audio = &capture_mono_buf_;
    std::vector<uint8_t> owned_audio;
    if (aec_) {
      auto processed = aec_->ProcessCaptureStream(capture_mono_buf_.data(), capture_mono_buf_.size());
      if (!processed.empty()) {
        owned_audio = std::move(processed);
        api_audio = &owned_audio;
      }
    }

    if (ai_speaking) {
      const float gain = ComputeUplinkGainWhileAiSpeaking(echo_stats);
      if (gain < 0.999f) {
        if (api_audio == &capture_mono_buf_) {
          owned_audio = capture_mono_buf_;
          api_audio = &owned_audio;
        }
        ScalePcmS16InPlace(&owned_audio, gain);
      }
    }

    if (api_audio == &owned_audio) {
      client_->EnqueueAudio(std::move(owned_audio));
    } else {
      client_->EnqueueAudio(*api_audio);
    }
  }
}

void App::OnAudio(const std::vector<uint8_t>& chunk) {
  if (!player_) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(mu_);
    ++pending_playback_chunks_;
  }

  const float gain = cfg_.audio.playback_gain;
  const bool need_gain = !(gain >= 0.999f && gain <= 1.001f);

  bool played;
  if (need_gain) {
    std::vector<uint8_t> amplified = chunk;
    ScalePcmS16InPlace(&amplified, gain);
    played = player_->Play(amplified);
  } else {
    played = player_->Play(chunk);
  }

  if (!played) {
    {
      std::lock_guard<std::mutex> lock(mu_);
      if (pending_playback_chunks_ > 0) {
        --pending_playback_chunks_;
      }
    }
    kLog->warn("player queue full, drop tts chunk: bytes={}", chunk.size());
    TryFinalizeFarewell();
  }
}

void App::OnPlaybackChunkPlayed() {
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (pending_playback_chunks_ > 0) {
      --pending_playback_chunks_;
    }
  }
  TryFinalizeFarewell();
}

void App::OnSetAiSpeaking(bool is_speaking) {
  {
    std::lock_guard<std::mutex> lock(mu_);
    is_ai_speaking_ = is_speaking;
    if (is_speaking && farewell_pending_) {
      farewell_tts_started_ = true;
    }
  }
  if (gate_) {
    gate_->SetAiSpeaking(is_speaking);
  }
  if (!is_speaking) {
    TryFinalizeFarewell();
  }
}

void App::OnAsrFinal(const std::string& text) {
  if (gate_ && gate_->step() == wakeup::Step::kActive) {
    gate_->RefreshTimeout();
  }

  std::string lower_text = text;
  std::transform(lower_text.begin(), lower_text.end(), lower_text.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

  static const std::vector<std::string> kSignoffs = {
      "bye", "goodbye", "that's all", "that's it", "quit", "exit", "stop", "再见", "拜拜", "退下", "退出"
  };

  for (const auto& kw : kSignoffs) {
    if (lower_text.find(kw) != std::string::npos) {
      kLog->info("signoff matched: '{}'", kw);
      std::lock_guard<std::mutex> lock(mu_);
      BeginFarewellStateLocked();
      break;
    }
  }
}

void App::OnUserActivity() {
  CancelWelcomeTimer();
  if (gate_ && gate_->step() == wakeup::Step::kActive) {
    gate_->RefreshTimeout();
  }

  bool should_interrupt = false;
  {
    std::lock_guard<std::mutex> lock(mu_);
    should_interrupt = is_ai_speaking_ || pending_playback_chunks_ > 0;
    if (should_interrupt) {
      is_ai_speaking_ = false;
      ResetFarewellStateLocked();
    }
  }
  if (!should_interrupt) {
    return;
  }

  kLog->info("barge-in detected: interrupt current playback");
  if (gate_) {
    gate_->SetAiSpeaking(false);
  }
  InterruptPlayback();
}

void App::OnSessionClosed(const std::string& reason) {
  if (stopping_.load()) {
    return;
  }
  kLog->info("session closed: {}", reason);
  CancelWelcomeTimer();
  {
    std::lock_guard<std::mutex> lock(mu_);
    ResetFarewellStateLocked();
  }
  if (gate_) {
    gate_->Disarm(reason);
  }
}

void App::OnChatEnded() {
  if (stopping_.load()) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (!farewell_pending_) {
      return;
    }
    farewell_chat_ended_ = true;
  }
  TryFinalizeFarewell();
}

void App::OnArm(const std::string& reason) {
  kLog->info("gate armed: {}", reason);
  InterruptPlayback();

  {
    std::lock_guard<std::mutex> lock(mu_);
    ResetFarewellStateLocked();
  }

  if (!client_ || !gate_) {
    kLog->error("on_arm skipped: client/gate missing");
    return;
  }
  if (!client_->StartSession(std::chrono::seconds(12))) {
    kLog->error("start session failed");
    gate_->Disarm("session_start_failed");
    return;
  }
  if (gate_->step() != wakeup::Step::kActive) {
    kLog->info("on_arm cancelled: gate no longer active");
    client_->FinishSession(std::chrono::seconds(2));
    return;
  }
  if (!cfg_.wakeup.say_hello.empty()) {
    if (!client_->SendSayHello()) {
      kLog->error("send say_hello failed");
      client_->FinishSession(std::chrono::seconds(2));
      gate_->Disarm("say_hello_failed");
      return;
    }
    StartWelcomeTimer();
  }
}

void App::StartWelcomeTimer() {
  CancelWelcomeTimer();
  const auto epoch = ++welcome_epoch_;
  {
    std::lock_guard<std::mutex> lock(welcome_mu_);
    welcome_cancelled_ = false;
  }

  std::thread timer([this, epoch]() {
    std::unique_lock<std::mutex> lock(welcome_mu_);
    const auto timeout = std::chrono::seconds(kWelcomeResponseTimeoutSec);
    const bool cancelled = welcome_cv_.wait_for(lock, timeout, [this, epoch]() {
      return welcome_cancelled_ || !running_.load() || welcome_epoch_.load() != epoch;
    });
    if (cancelled) {
      return;
    }
    lock.unlock();
    FinishSessionAndDisarm("welcome_timeout");
  });
  std::lock_guard<std::mutex> lock(welcome_thread_mu_);
  welcome_thread_ = std::move(timer);
}

void App::CancelWelcomeTimer() {
  ++welcome_epoch_;
  {
    std::lock_guard<std::mutex> lock(welcome_mu_);
    welcome_cancelled_ = true;
  }
  welcome_cv_.notify_all();
  std::thread timer_to_join;
  {
    std::lock_guard<std::mutex> lock(welcome_thread_mu_);
    if (welcome_thread_.joinable() &&
        welcome_thread_.get_id() != std::this_thread::get_id()) {
      timer_to_join = std::move(welcome_thread_);
    }
  }
  if (timer_to_join.joinable()) {
    timer_to_join.join();
  }
}

void App::FinishSessionAndDisarm(const std::string& reason) {
  kLog->info("finish session and disarm: {}", reason);
  CancelWelcomeTimer();
  InterruptPlayback();
  {
    std::lock_guard<std::mutex> lock(mu_);
    ResetFarewellStateLocked();
  }
  if (client_) {
    client_->FinishSession(std::chrono::seconds(2));
  }
  if (gate_) {
    gate_->Disarm(reason);
  }
}

void App::TryFinalizeFarewell() {
  bool should_close = false;
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (farewell_pending_ && farewell_chat_ended_ && farewell_tts_started_ &&
        !is_ai_speaking_ &&
        pending_playback_chunks_ == 0) {
      should_close = true;
      ResetFarewellStateLocked();
    }
  }

  if (!should_close) {
    return;
  }

  kLog->info("farewell close: playback drained, closing session");
  CancelWelcomeTimer();
  if (client_) client_->FinishSession(std::chrono::seconds(4));
  if (gate_) gate_->Disarm("bye");
}

void App::ResetFarewellStateLocked() {
  farewell_pending_ = false;
  farewell_chat_ended_ = false;
  farewell_tts_started_ = false;
}

void App::BeginFarewellStateLocked() {
  farewell_pending_ = true;
  farewell_chat_ended_ = false;
  farewell_tts_started_ = false;
}

void App::InterruptPlayback() {
  {
    std::lock_guard<std::mutex> lock(mu_);
    pending_playback_chunks_ = 0;
  }
  if (player_) {
    player_->Interrupt();
  }
  const int rc = std::system("mphelper pause");
  if (rc != 0) {
    kLog->warn("mphelper pause failed: rc={}", rc);
  }
  // Pause AirPlay so music stops playing over the voice assistant
  (void)std::system("killall -STOP shairport-sync >/dev/null 2>&1");
}

}  // namespace xiaoai_plus::app
