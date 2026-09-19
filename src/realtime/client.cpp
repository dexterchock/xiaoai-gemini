#include "realtime/client.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <random>
#include <system_error>
#include <thread>

#include <nlohmann/json.hpp>
#include "common/log.hpp"

namespace xiaoai_plus::realtime {

namespace {

const auto kLog = xiaoai_plus::GetLogger("realtime");

constexpr const char* kInputMimeType = "audio/pcm;rate=16000";

std::string GenSessionId() {
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  return "sid-" + std::to_string(now);
}

void AppendBase64(const std::vector<uint8_t>& in, std::string* out) {
  static const char kAlphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  if (!out) {
    return;
  }
  const size_t in_size = in.size();
  for (size_t i = 0; i < in_size; i += 3) {
    const uint32_t b0 = in[i];
    const uint32_t b1 = (i + 1 < in_size) ? in[i + 1] : 0;
    const uint32_t b2 = (i + 2 < in_size) ? in[i + 2] : 0;
    const uint32_t triple = (b0 << 16) | (b1 << 8) | b2;
    out->push_back(kAlphabet[(triple >> 18) & 0x3F]);
    out->push_back(kAlphabet[(triple >> 12) & 0x3F]);
    out->push_back((i + 1 < in_size) ? kAlphabet[(triple >> 6) & 0x3F] : '=');
    out->push_back((i + 2 < in_size) ? kAlphabet[triple & 0x3F] : '=');
  }
}

int DecodeBase64Char(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

std::vector<uint8_t> Base64Decode(const std::string& in) {
  std::vector<uint8_t> out;
  out.reserve((in.size() * 3) / 4);
  uint32_t acc = 0;
  int bits = 0;
  for (char c : in) {
    if (c == '=') {
      break;
    }
    const int v = DecodeBase64Char(c);
    if (v < 0) {
      continue;
    }
    acc = (acc << 6) | static_cast<uint32_t>(v);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(static_cast<uint8_t>((acc >> bits) & 0xFF));
    }
  }
  return out;
}

std::string JsonString(const nlohmann::json& j, const char* key) {
  if (!j.is_object()) {
    return std::string();
  }
  auto it = j.find(key);
  if (it == j.end() || !it->is_string()) {
    return std::string();
  }
  return it->get<std::string>();
}

}  // namespace

Client::Client(config::Config cfg, Callbacks callbacks)
    : cfg_(std::move(cfg)), callbacks_(std::move(callbacks)), rng_(std::random_device{}()) {}

Client::~Client() { Stop(); }

bool Client::Start() {
  bool expected = false;
  if (!running_.compare_exchange_strong(expected, true)) {
    return true;
  }

  sender_thread_ = std::thread([this]() {
    while (running_.load()) {
      std::vector<uint8_t> chunk;
      {
        std::unique_lock<std::mutex> lock(audio_mu_);
        audio_cv_.wait(lock, [this]() { return !running_.load() || !audio_queue_.empty(); });
        if (!running_.load()) {
          break;
        }
        chunk = std::move(audio_queue_.front());
        audio_queue_.pop_front();
      }

      bool connected = false;
      {
        std::lock_guard<std::mutex> lock(conn_mu_);
        connected = ws_ && ws_connected_ && !session_id_.empty();
      }
      if (!connected || chunk.empty()) {
        continue;
      }

      try {
        SendRealtimeAudio(chunk);
      } catch (...) {
        kLog->warn("audio send failed");
      }
    }
  });

  return true;
}

void Client::Stop() {
  if (!running_.exchange(false)) {
    return;
  }

  audio_cv_.notify_all();
  conn_cv_.notify_all();
  setup_cv_.notify_all();

  CloseConnection(true);
  if (sender_thread_.joinable()) {
    sender_thread_.join();
  }
}

bool Client::StartSession(std::chrono::milliseconds timeout) {
  {
    std::lock_guard<std::mutex> lock(conn_mu_);
    if (!session_id_.empty()) {
      return true;
    }
  }

  if (!running_.load()) {
    return false;
  }

  if (!EnsureConnection(timeout)) {
    kLog->error("start session failed: ensure connection failed");
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(audio_mu_);
    audio_queue_.clear();
  }

  {
    std::lock_guard<std::mutex> lock(conn_mu_);
    session_id_ = GenSessionId();
  }
  kLog->info("gemini live session ready");
  return true;
}

bool Client::FinishSession(std::chrono::milliseconds) {
  std::string sid;
  {
    std::lock_guard<std::mutex> lock(conn_mu_);
    sid = session_id_;
  }
  if (sid.empty()) {
    return true;
  }
  CloseConnection(false);
  return true;
}

bool Client::EnqueueAudio(const std::vector<uint8_t>& chunk) {
  if (chunk.empty()) {
    return true;
  }
  std::lock_guard<std::mutex> lock(audio_mu_);
  if (static_cast<int>(audio_queue_.size()) >= cfg_.budget.input_queue_frames) {
    return false;
  }
  audio_queue_.emplace_back(chunk);
  audio_cv_.notify_one();
  return true;
}

bool Client::EnqueueAudio(std::vector<uint8_t>&& chunk) {
  if (chunk.empty()) {
    return true;
  }
  std::lock_guard<std::mutex> lock(audio_mu_);
  if (static_cast<int>(audio_queue_.size()) >= cfg_.budget.input_queue_frames) {
    return false;
  }
  audio_queue_.push_back(std::move(chunk));
  audio_cv_.notify_one();
  return true;
}

bool Client::SendSayHello() {
  std::string sid;
  {
    std::lock_guard<std::mutex> lock(conn_mu_);
    sid = session_id_;
  }
  if (sid.empty() || cfg_.wakeup.say_hello.empty()) {
    return true;
  }

  const nlohmann::json msg = {
      {"clientContent",
       {{"turns",
         nlohmann::json::array({{{"role", "user"},
                                 {"parts", nlohmann::json::array(
                                               {{{"text", cfg_.wakeup.say_hello}}})}}})},
        {"turnComplete", true}}}};

  return SendJson(msg);
}

bool Client::EnsureConnection(std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  int attempt = 0;

  while (running_.load()) {
    {
      std::lock_guard<std::mutex> lock(conn_mu_);
      std::lock_guard<std::mutex> setup_lock(setup_mu_);
      if (ws_ && ws_connected_ && setup_done_) {
        return true;
      }
    }

    CloseConnection(false);
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
      return false;
    }

    if (OpenConnection(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now))) {
      return true;
    }

    ++attempt;
    const auto current = std::chrono::steady_clock::now();
    if (current >= deadline) {
      return false;
    }

    auto backoff = NextBackoff(attempt);
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - current);
    if (backoff > remaining) {
      backoff = remaining;
    }
    if (backoff.count() > 0) {
      kLog->warn("connection attempt {} failed, retrying in {}ms", attempt, backoff.count());
      std::this_thread::sleep_for(backoff);
    }
  }
  return false;
}

bool Client::OpenConnection(std::chrono::milliseconds timeout) {
  std::string ws_url = cfg_.realtime.preset.ws_url;
  const char sep = (ws_url.find('?') != std::string::npos) ? '&' : '?';
  ws_url += sep;
  ws_url += "key=";
  ws_url += cfg_.realtime.api_key;

  const auto query_pos = cfg_.realtime.preset.ws_url.find('?');
  const std::string log_url =
      query_pos == std::string::npos ? cfg_.realtime.preset.ws_url
                                     : cfg_.realtime.preset.ws_url.substr(0, query_pos);
  kLog->info("connecting to {}", log_url);

  auto ws = std::make_unique<ix::WebSocket>();
  ws->setUrl(ws_url);
  ws->disableAutomaticReconnection();

  ix::SocketTLSOptions tls;
  tls.caFile = "NONE";
  ws->setTLSOptions(tls);

  ws->setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) {
    if (msg->type == ix::WebSocketMessageType::Open) {
      std::lock_guard<std::mutex> lock(conn_mu_);
      ws_connected_ = true;
      conn_cv_.notify_all();
      kLog->info("connection ready");
      return;
    }

    if (msg->type == ix::WebSocketMessageType::Close ||
        msg->type == ix::WebSocketMessageType::Error) {
      if (msg->type == ix::WebSocketMessageType::Error) {
        kLog->warn("ws error: {} (retries={}, wait={}ms, http={})",
                   msg->errorInfo.reason, msg->errorInfo.retries,
                   msg->errorInfo.wait_time, msg->errorInfo.http_status);
      } else {
        kLog->warn("ws closed: code={} reason='{}'", msg->closeInfo.code,
                   msg->closeInfo.reason);
      }
      std::string sid;
      {
        std::lock_guard<std::mutex> lock(conn_mu_);
        ws_connected_ = false;
        sid = session_id_;
        conn_cv_.notify_all();
      }
      {
        std::lock_guard<std::mutex> lock(setup_mu_);
        setup_done_ = false;
        setup_cv_.notify_all();
      }
      if (!sid.empty()) {
        HandleSessionClosed("connection_lost");
      }
      return;
    }

    if (msg->type == ix::WebSocketMessageType::Message && !msg->binary) {
      try {
        const auto json_msg = nlohmann::json::parse(msg->str);
        OnServerMessage(json_msg);
      } catch (const nlohmann::json::parse_error& e) {
        kLog->warn("ws json parse failed: {}", e.what());
      } catch (const std::exception& e) {
        kLog->warn("ws message handling failed: {}", e.what());
      }
    }
  });

  {
    std::lock_guard<std::mutex> lock(conn_mu_);
    ws_connected_ = false;
  }

  ws->start();

  {
    std::lock_guard<std::mutex> lock(conn_mu_);
    ws_ = std::move(ws);
  }

  bool opened = false;
  {
    std::unique_lock<std::mutex> lock(conn_mu_);
    const auto ws_open_timeout = std::min(timeout, std::chrono::milliseconds(8000));
    opened = conn_cv_.wait_for(lock, ws_open_timeout, [this] { return ws_connected_; });
  }
  if (!opened) {
    kLog->error("websocket open timeout");
    CloseConnection(false);
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(setup_mu_);
    setup_done_ = false;
  }

  if (!SendJson(BuildSetupMessage())) {
    kLog->error("send setup failed");
    CloseConnection(false);
    return false;
  }

  bool setup_ok = false;
  {
    std::unique_lock<std::mutex> lock(setup_mu_);
    const auto setup_timeout = std::min(timeout, std::chrono::milliseconds(8000));
    setup_ok = setup_cv_.wait_for(lock, setup_timeout, [this] { return setup_done_; });
  }
  if (!setup_ok) {
    kLog->error("setup not acknowledged before timeout");
    CloseConnection(false);
    return false;
  }

  return true;
}

void Client::CloseConnection(bool) {
  std::unique_ptr<ix::WebSocket> ws;
  {
    std::lock_guard<std::mutex> lock(conn_mu_);
    ws_connected_ = false;
    session_id_.clear();
    ws = std::move(ws_);
    conn_cv_.notify_all();
  }
  {
    std::lock_guard<std::mutex> lock(setup_mu_);
    setup_done_ = false;
    setup_cv_.notify_all();
  }
  {
    std::lock_guard<std::mutex> lock(audio_mu_);
    audio_queue_.clear();
  }
  if (ws) {
    ws->setOnMessageCallback(nullptr);
    try {
      ws->stop();
    } catch (const std::system_error& e) {
      kLog->error("ws stop failed with system_error: {}", e.what());
    } catch (const std::exception& e) {
      kLog->error("ws stop failed: {}", e.what());
    } catch (...) {
      kLog->error("ws stop failed: unknown exception");
    }
  }
}

nlohmann::json Client::BuildSetupMessage() const {
  const auto& preset = cfg_.realtime.preset;

  std::string model = preset.model;
  if (model.rfind("models/", 0) != 0) {
    model = "models/" + model;
  }

  std::string system_text;
  if (!preset.bot_name.empty()) {
    system_text += "Your name is " + preset.bot_name + ".\n";
  }
  if (!preset.system_role.empty()) {
    system_text += preset.system_role;
    system_text += "\n";
  }
  if (!preset.speaking_style.empty()) {
    system_text += preset.speaking_style;
  }

  nlohmann::json generation_config = {
      {"responseModalities", nlohmann::json::array({"AUDIO"})},
  };
  if (!preset.voice.empty()) {
    generation_config["speechConfig"] = {
        {"voiceConfig",
         {{"prebuiltVoiceConfig", {{"voiceName", preset.voice}}}}},
    };
  }

  nlohmann::json setup = {
      {"model", std::move(model)},
      {"generationConfig", std::move(generation_config)},
      {"inputAudioTranscription", nlohmann::json::object()},
  };

  if (preset.google_search) {
    setup["tools"] = nlohmann::json::array({
        {{"googleSearch", nlohmann::json::object()}}
    });
  }

  if (!system_text.empty()) {
    nlohmann::json text_part = nlohmann::json::object();
    text_part["text"] = system_text;
    nlohmann::json system_instruction = nlohmann::json::object();
    system_instruction["parts"] = nlohmann::json::array({text_part});
    setup["systemInstruction"] = system_instruction;
  }

  return {{"setup", std::move(setup)}};
}

void Client::OnServerMessage(const nlohmann::json& msg) {
  if (!msg.is_object()) {
    return;
  }
  if (msg.contains("error")) {
    kLog->error("gemini server error: {}", msg["error"].dump());
    HandleSessionClosed("server_error");
    return;
  }
  if (msg.contains("setupComplete")) {
    HandleSetupComplete();
    return;
  }
  if (msg.contains("serverContent")) {
    HandleServerContent(msg["serverContent"]);
    return;
  }
  if (msg.contains("goAway")) {
    kLog->warn("server requested shutdown (goAway)");
    return;
  }
}

void Client::HandleSetupComplete() {
  {
    std::lock_guard<std::mutex> lock(setup_mu_);
    setup_done_ = true;
    setup_cv_.notify_all();
  }
  kLog->info("gemini setup complete");
}

void Client::HandleServerContent(const nlohmann::json& sc) {
  if (!sc.is_object()) {
    return;
  }

  auto gm_it = sc.find("groundingMetadata");
  if (gm_it != sc.end() && gm_it->is_object()) {
    auto queries_it = gm_it->find("webSearchQueries");
    if (queries_it != gm_it->end() && queries_it->is_array()) {
      for (const auto& q : *queries_it) {
        if (q.is_string()) {
          kLog->info("google search query: '{}'", q.get<std::string>());
        }
      }
    }
  }

  auto turn_it = sc.find("modelTurn");
  if (turn_it != sc.end() && turn_it->is_object()) {
    const auto parts = turn_it->value("parts", nlohmann::json::array());
    for (const auto& part : parts) {
      if (!part.is_object()) {
        continue;
      }
      if (part.contains("text")) {
        const auto text = JsonString(part, "text");
        if (!text.empty()) {
          kLog->info("gemini text: '{}'", text);
        }
      }
      auto data_it = part.find("inlineData");
      if (data_it == part.end()) {
        data_it = part.find("inline_data");
      }
      if (data_it == part.end() || !data_it->is_object()) {
        continue;
      }
      const auto b64 = JsonString(*data_it, "data");
      if (b64.empty()) {
        continue;
      }
      const auto pcm = Base64Decode(b64);
      if (pcm.empty()) {
        continue;
      }

      bool set_speaking = false;
      {
        std::lock_guard<std::mutex> lock(event_mu_);
        if (!is_ai_speaking_) {
          is_ai_speaking_ = true;
          set_speaking = true;
        }
      }
      if (set_speaking && callbacks_.on_set_ai_speaking) {
        kLog->info("gemini tts started");
        callbacks_.on_set_ai_speaking(true);
      }

      if (callbacks_.on_audio) {
        callbacks_.on_audio(pcm);
      }
    }
  }

  auto user_turn_it = sc.find("userTurn");
  if (user_turn_it != sc.end() && user_turn_it->is_object()) {
    const auto parts = user_turn_it->value("parts", nlohmann::json::array());
    for (const auto& part : parts) {
      if (part.is_object() && part.contains("text")) {
        const std::string text = part.value("text", "");
        if (!text.empty()) {
          kLog->info("asr final: '{}'", text);
          if (callbacks_.on_asr_final) callbacks_.on_asr_final(text);
          if (callbacks_.on_user_activity) callbacks_.on_user_activity();
        }
      }
    }
  }

  auto input_it = sc.find("inputTranscription");
  if (input_it == sc.end()) {
    input_it = sc.find("input_transcription");
  }
  if (input_it != sc.end() && input_it->is_object()) {
    const auto text = JsonString(*input_it, "text");
    if (!text.empty()) {
      kLog->info("asr final: '{}'", text);
      if (callbacks_.on_asr_final) {
        callbacks_.on_asr_final(text);
      }
      if (callbacks_.on_user_activity) {
        callbacks_.on_user_activity();
      }
    }
  }

  if (sc.value("interrupted", false)) {
    kLog->info("gemini interrupted");
    if (callbacks_.on_user_activity) {
      callbacks_.on_user_activity();
    }
    StopSpeaking();
  }

  if (sc.value("generationComplete", false) || sc.value("turnComplete", false)) {
    StopSpeaking();
    if (callbacks_.on_chat_ended) {
      callbacks_.on_chat_ended();
    }
  }
}

void Client::StopSpeaking() {
  bool was_speaking = false;
  {
    std::lock_guard<std::mutex> lock(event_mu_);
    if (is_ai_speaking_) {
      is_ai_speaking_ = false;
      was_speaking = true;
    }
  }
  if (was_speaking && callbacks_.on_set_ai_speaking) {
    kLog->info("gemini tts ended");
    callbacks_.on_set_ai_speaking(false);
  }
}

void Client::HandleSessionClosed(const std::string& reason) {
  {
    std::lock_guard<std::mutex> lock(conn_mu_);
    session_id_.clear();
  }
  {
    std::lock_guard<std::mutex> lock(event_mu_);
    is_ai_speaking_ = false;
  }
  if (callbacks_.on_set_ai_speaking) {
    callbacks_.on_set_ai_speaking(false);
  }
  if (callbacks_.on_session_closed) {
    callbacks_.on_session_closed(reason);
  }
}

bool Client::SendJson(const nlohmann::json& msg) {
  std::string text;
  try {
    text = msg.dump();
  } catch (...) {
    kLog->error("json dump failed");
    return false;
  }
  std::lock_guard<std::mutex> lock(write_mu_);
  std::lock_guard<std::mutex> conn_lock(conn_mu_);
  if (!ws_ || !ws_connected_) {
    return false;
  }
  const auto res = ws_->sendText(text);
  return res.success;
}

bool Client::SendRealtimeAudio(const std::vector<uint8_t>& chunk) {
  if (chunk.empty()) {
    return true;
  }

  std::string text;
  text.reserve(80 + ((chunk.size() + 2) / 3) * 4);
  text.append(R"({"realtimeInput":{"audio":{"mimeType":")");
  text.append(kInputMimeType);
  text.append(R"(","data":")");
  AppendBase64(chunk, &text);
  text.append(R"("}}})");

  std::lock_guard<std::mutex> lock(write_mu_);
  std::lock_guard<std::mutex> conn_lock(conn_mu_);
  if (!ws_ || !ws_connected_) {
    return false;
  }
  const auto res = ws_->sendText(text);
  return res.success;
}

std::chrono::milliseconds Client::NextBackoff(int attempt) const {
  int min_ms = cfg_.budget.reconnect_backoff_min_ms;
  int max_ms = cfg_.budget.reconnect_backoff_max_ms;
  if (min_ms <= 0) {
    min_ms = 300;
  }
  if (max_ms < min_ms) {
    max_ms = min_ms * 4;
  }

  int upper = min_ms << std::max(0, attempt - 1);
  upper = std::min(upper, max_ms);
  if (upper <= min_ms) {
    return std::chrono::milliseconds(min_ms);
  }

  std::lock_guard<std::mutex> lock(rng_mu_);
  std::uniform_int_distribution<int> dist(min_ms, upper);
  return std::chrono::milliseconds(dist(rng_));
}

}  // namespace xiaoai_plus::realtime
