#pragma once
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace xiaoai_plus {

inline std::shared_ptr<spdlog::logger> GetLogger(const char* name) {
  if (auto logger = spdlog::get(name)) return logger;
  try {
    auto logger = spdlog::stdout_color_mt(name);
    logger->set_pattern("[%H:%M:%S.%e] [%-8n] [%^%-5l%$] %v");
    logger->set_level(spdlog::level::info);
    logger->flush_on(spdlog::level::warn);
    return logger;
  } catch (...) {
    return spdlog::get(name);
  }
}

}  // namespace xiaoai_plus
