#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace xiaoai_plus::wakeup {

class Trigger {
 public:
  Trigger(std::vector<std::string> keywords,
          std::function<bool(const std::string&)> on_wake);

  static std::string NormalizeKeyword(const std::string& text);

  bool FireFromText(const std::string& text);

 private:
  static std::unordered_map<std::string, std::string> NormalizeKeywords(
      const std::vector<std::string>& keywords);

  std::unordered_map<std::string, std::string> allowed_;
  std::function<bool(const std::string&)> on_wake_;
};

}  // namespace xiaoai_plus::wakeup
