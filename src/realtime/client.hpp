#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <ixwebsocket/IXWebSocket.h>
#include <nlohmann/json.hpp>

#include "config/config.hpp"

namespace xiaoai_plus::realtime {

class Client {
 public:
  struct Callbacks {
    std::function<void(const std::vector<uint8_t>&)> on_audio;
    std::function<void(bool)> on_set_ai_speaking;
    std::function<void(const std::string&)> on_asr_final;
    std::function<void()> on_user_activity;
    std::function<void(const std::string&)> on_session_closed;
    std::function<void()> on_chat_ended;
  };

  explicit Client(config::Config cfg, Callbacks callbacks);
  ~Client();

  bool Start();
  void Stop();

  bool StartSession(std::chrono::milliseconds timeout = std::chrono::seconds(10));
  bool FinishSession(std::chrono::milliseconds timeout = std::chrono::seconds(2));
  bool EnqueueAudio(const std::vector<uint8_t>& chunk);
  bool EnqueueAudio(std::vector<uint8_t>&& chunk);
  bool SendSayHello();

 private:
  bool EnsureConnection(std::chrono::milliseconds timeout);
  bool OpenConnection(std::chrono::milliseconds timeout);
  void CloseConnection(bool send_finish_event);

  void OnServerMessage(const nlohmann::json& msg);
  void HandleServerContent(const nlohmann::json& sc);
  void HandleSetupComplete();
  void StopSpeaking();
  void HandleSessionClosed(const std::string& reason);

  nlohmann::json BuildSetupMessage() const;
  bool SendJson(const nlohmann::json& msg);
  bool SendRealtimeAudio(const std::vector<uint8_t>& chunk);
  std::chrono::milliseconds NextBackoff(int attempt) const;

  config::Config cfg_;
  Callbacks callbacks_;

  mutable std::mutex conn_mu_;
  std::condition_variable conn_cv_;
  std::unique_ptr<ix::WebSocket> ws_;
  bool ws_connected_{false};
  std::string session_id_;

  mutable std::mutex write_mu_;

  mutable std::mutex audio_mu_;
  std::condition_variable audio_cv_;
  std::deque<std::vector<uint8_t>> audio_queue_;

  mutable std::mutex setup_mu_;
  std::condition_variable setup_cv_;
  bool setup_done_{false};

  mutable std::mutex event_mu_;
  bool is_ai_speaking_{false};

  mutable std::mutex rng_mu_;
  mutable std::mt19937 rng_;

  std::atomic<bool> running_{false};
  std::thread sender_thread_;
};

}  // namespace xiaoai_plus::realtime