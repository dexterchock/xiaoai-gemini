#pragma once

#include <string>

namespace xiaoai_plus::config {

struct RealtimePreset {
  std::string ws_url{
      "wss://generativelanguage.googleapis.com/ws/google.ai.generativelanguage."
      "v1beta.GenerativeService.BidiGenerateContent"};
  std::string model{"gemini-3.1-flash-live-preview"};
  std::string bot_name{"Gemini"};
  std::string system_role{"你是 Gemini 同学，是小爱音箱上的语音助手。"};
  std::string speaking_style{"语气自然、友好、简洁。"};
  std::string voice{"Kore"};
};

struct Realtime {
  std::string api_key;
  RealtimePreset preset;
};

struct AudioPreset {
  std::string input_device{"noop"};
  std::string output_device{"default"};
  int sample_rate{16000};
  int channels{1};
  int bits_per_sample{16};
  int buffer_size{1440};
  int period_size{360};
  float playback_gain{1.0f};
};
using Audio = AudioPreset;

struct Wakeup {
  std::string say_hello{"在"};
  std::string keywords_file{"assets/keywords.txt"};
  std::string tokens_path{"assets/tokens.txt"};
  std::string encoder_path{"assets/encoder.onnx"};
  std::string decoder_path{"assets/decoder.onnx"};
  std::string joiner_path{"assets/joiner.onnx"};
};

struct BudgetPreset {
  int audio_chunk_ms{20};
  int input_queue_frames{64};
  int output_queue_frames{128};
  int reconnect_backoff_min_ms{300};
  int reconnect_backoff_max_ms{4000};
};

struct Config {
  Realtime realtime;
  AudioPreset audio;
  Wakeup wakeup;
  BudgetPreset budget;

  void normalize();
  void validate() const;
};

Config load(const std::string& path);

}  // namespace xiaoai_plus::config
