#include "config/config.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace xiaoai_plus::config {

namespace {

std::string Trim(std::string s) {
  auto not_space = [](unsigned char c) { return !std::isspace(c); };
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
  s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
  return s;
}

std::string Unquote(std::string s) {
  s = Trim(std::move(s));
  if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') ||
                        (s.front() == '\'' && s.back() == '\''))) {
    return s.substr(1, s.size() - 2);
  }
  return s;
}

std::unordered_map<std::string, std::unordered_map<std::string, std::string>> ParseIni(
    const std::string& path) {
  std::ifstream in(path);
  if (!in.is_open()) {
    throw std::runtime_error("failed to open config: " + path);
  }

  std::unordered_map<std::string, std::unordered_map<std::string, std::string>> sections;
  std::string section;
  std::string line;
  int lineno = 0;

  while (std::getline(in, line)) {
    ++lineno;
    auto hash_pos = line.find('#');
    auto semicolon_pos = line.find(';');
    size_t comment_pos = std::string::npos;
    if (hash_pos != std::string::npos && semicolon_pos != std::string::npos) {
      comment_pos = std::min(hash_pos, semicolon_pos);
    } else if (hash_pos != std::string::npos) {
      comment_pos = hash_pos;
    } else if (semicolon_pos != std::string::npos) {
      comment_pos = semicolon_pos;
    }
    if (comment_pos != std::string::npos) {
      line = line.substr(0, comment_pos);
    }

    line = Trim(std::move(line));
    if (line.empty()) {
      continue;
    }

    if (line.front() == '[') {
      auto right = line.find(']');
      if (right == std::string::npos) {
        throw std::runtime_error("invalid section header at line " + std::to_string(lineno));
      }
      section = Trim(line.substr(1, right - 1));
      if (section.empty()) {
        throw std::runtime_error("empty section name at line " + std::to_string(lineno));
      }
      continue;
    }

    auto eq = line.find('=');
    if (eq == std::string::npos) {
      throw std::runtime_error("invalid key-value at line " + std::to_string(lineno));
    }

    if (section.empty()) {
      throw std::runtime_error("key-value outside section at line " + std::to_string(lineno));
    }

    std::string key = Trim(line.substr(0, eq));
    std::string value = Unquote(line.substr(eq + 1));
    if (key.empty()) {
      throw std::runtime_error("empty key at line " + std::to_string(lineno));
    }
    sections[section][key] = value;
  }

  return sections;
}

const std::string& GetRequired(const std::unordered_map<std::string, std::string>& kv,
                               const std::string& key, const std::string& err_name) {
  auto it = kv.find(key);
  if (it == kv.end() || it->second.empty()) {
    throw std::runtime_error(err_name + " is required");
  }
  return it->second;
}

void SetIfPresent(const std::unordered_map<std::string, std::string>& kv, const std::string& key,
                  std::string* out) {
  auto it = kv.find(key);
  if (it != kv.end()) {
    *out = it->second;
  }
}

void SetIfPresent(const std::unordered_map<std::string, std::string>& kv, const std::string& key,
                  int* out) {
  auto it = kv.find(key);
  if (it != kv.end()) {
    *out = std::stoi(it->second);
  }
}

void SetIfPresent(const std::unordered_map<std::string, std::string>& kv, const std::string& key,
                  float* out) {
  auto it = kv.find(key);
  if (it != kv.end()) {
    *out = std::stof(it->second);
  }
}

}  // namespace

void Config::normalize() {
  if (budget.reconnect_backoff_min_ms <= 0) {
    budget.reconnect_backoff_min_ms = 300;
  }
  if (budget.reconnect_backoff_max_ms < budget.reconnect_backoff_min_ms) {
    budget.reconnect_backoff_max_ms = budget.reconnect_backoff_min_ms * 4;
  }
}

void Config::validate() const {
  if (realtime.api_key.empty()) {
    throw std::runtime_error("realtime.api_key is required");
  }
}

Config load(const std::string& path) {
  Config cfg;
  if (!path.empty()) {
    auto sections = ParseIni(path);

    if (auto it = sections.find("realtime"); it != sections.end()) {
      const auto& kv = it->second;
      cfg.realtime.api_key = GetRequired(kv, "api_key", "realtime.api_key");
      SetIfPresent(kv, "model", &cfg.realtime.preset.model);
      SetIfPresent(kv, "bot_name", &cfg.realtime.preset.bot_name);
      SetIfPresent(kv, "system_role", &cfg.realtime.preset.system_role);
      SetIfPresent(kv, "speaking_style", &cfg.realtime.preset.speaking_style);
      SetIfPresent(kv, "voice", &cfg.realtime.preset.voice);
      SetIfPresent(kv, "ws_url", &cfg.realtime.preset.ws_url);
    }

    if (auto it = sections.find("wakeup"); it != sections.end()) {
      const auto& kv = it->second;
      SetIfPresent(kv, "say_hello", &cfg.wakeup.say_hello);
    }

    if (auto it = sections.find("audio"); it != sections.end()) {
      const auto& kv = it->second;
      SetIfPresent(kv, "playback_gain", &cfg.audio.playback_gain);
    }
  }

  cfg.normalize();
  cfg.validate();
  return cfg;
}

}  // namespace xiaoai_plus::config
