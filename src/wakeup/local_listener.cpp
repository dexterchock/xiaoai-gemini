#include "wakeup/local_listener.hpp"

#include "common/log.hpp"

namespace xiaoai_plus::wakeup {

namespace {
const auto kLog = xiaoai_plus::GetLogger("kws");
}  // namespace

LocalListener::LocalListener(Config cfg) : cfg_(std::move(cfg)) {}

void LocalListener::AcceptPcm(const std::vector<uint8_t>& chunk) {
  if (chunk.empty() || !cfg_.gate || !cfg_.trigger || !cfg_.kws_engine) {
    return;
  }

  if (cfg_.gate->step() != Step::kIdle) {
    return;
  }

  auto hit = cfg_.kws_engine->AcceptPcm16(chunk.data(), chunk.size(), cfg_.sample_rate,
                                          cfg_.channels, cfg_.bit_depth);
  if (!hit.has_value()) {
    return;
  }
  kLog->info("kws hit: keyword='{}'", hit->keyword);

  const auto now = std::chrono::steady_clock::now();
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (closed_) {
      return;
    }
    if (last_trigger_.time_since_epoch().count() > 0) {
      auto delta =
          std::chrono::duration_cast<std::chrono::milliseconds>(now - last_trigger_).count();
      if (delta < cfg_.min_trigger_interval_ms) {
        kLog->info("kws skip by interval: delta={}ms min={}ms", delta,
                     cfg_.min_trigger_interval_ms);
        return;
      }
    }
  }

  if (cfg_.trigger->FireFromText(hit->keyword)) {
    kLog->info("kws trigger accepted: keyword='{}'", hit->keyword);
    std::lock_guard<std::mutex> lock(mu_);
    last_trigger_ = now;
  } else {
    kLog->info("kws trigger rejected: keyword='{}'", hit->keyword);
  }
}

void LocalListener::Close() {
  std::lock_guard<std::mutex> lock(mu_);
  closed_ = true;
  if (cfg_.kws_engine) {
    cfg_.kws_engine->Reset();
  }
}

}  // namespace xiaoai_plus::wakeup
