#include "wakeup/gate.hpp"

#include "common/log.hpp"

namespace xiaoai_plus::wakeup {

namespace {
const auto kLog = xiaoai_plus::GetLogger("gate");
}  // namespace

Gate::Gate(std::chrono::milliseconds timeout, Hooks hooks)
    : timeout_(timeout.count() > 0 ? timeout : std::chrono::seconds(60)), hooks_(std::move(hooks)) {
  timeout_thread_ = std::thread([this]() { TimeoutLoop(); });
}

Gate::~Gate() { Close(); }

Step Gate::step() const {
  std::lock_guard<std::mutex> lock(mu_);
  return step_;
}

bool Gate::TryWakeup(const std::string& keyword) {
  const std::string reason = "local_kws: " + keyword;
  std::function<void(const std::string&)> on_arm;
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (step_ != Step::kIdle) {
      kLog->info("gate reject wakeup: busy");
      return false;
    }
    step_ = Step::kActive;
    RefreshTimeoutLocked();
    on_arm = hooks_.on_arm;
  }
  kLog->info("gate to active: {}", reason);
  if (on_arm) {
    on_arm(reason);
  }
  return true;
}

void Gate::Activate(const std::string& reason) {
  bool was_idle = false;
  std::function<void(const std::string&)> on_arm;
  {
    std::lock_guard<std::mutex> lock(mu_);
    was_idle = (step_ == Step::kIdle);
    step_ = Step::kActive;
    RefreshTimeoutLocked();
    on_arm = hooks_.on_arm;
  }
  if (was_idle && on_arm) {
    on_arm(reason);
  }
}

void Gate::Disarm(const std::string& reason) {
  std::function<void(const std::string&)> after;
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (step_ == Step::kIdle) {
      return;
    }
    step_ = Step::kIdle;
    ++timeout_epoch_;
    after = hooks_.after_disarm;
  }
  kLog->info("gate to idle: {}", reason);
  timeout_cv_.notify_all();
  if (after) {
    after(reason);
  }
}

void Gate::RefreshTimeout() {
  std::lock_guard<std::mutex> lock(mu_);
  if (step_ == Step::kActive) {
    RefreshTimeoutLocked();
  }
}

void Gate::RefreshTimeoutLocked() {
  timeout_deadline_ = std::chrono::steady_clock::now() + timeout_;
  ++timeout_epoch_;
  timeout_cv_.notify_all();
}

void Gate::SetAiSpeaking(bool is_speaking) {
  std::lock_guard<std::mutex> lock(mu_);
  if (is_ai_speaking_ == is_speaking) {
    return;
  }
  is_ai_speaking_ = is_speaking;
  if (step_ != Step::kActive) {
    return;
  }

  if (is_ai_speaking_) {
    // Pause timeout while AI is speaking.
    ++timeout_epoch_;
    timeout_cv_.notify_all();
  } else {
    // Resume timeout countdown from now when AI speech ends.
    RefreshTimeoutLocked();
  }
}

void Gate::Close() {
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (closed_) {
      return;
    }
    closed_ = true;
    ++timeout_epoch_;
  }
  timeout_cv_.notify_all();
  if (timeout_thread_.joinable()) {
    timeout_thread_.join();
  }
}

void Gate::TimeoutLoop() {
  std::unique_lock<std::mutex> lock(mu_);
  while (!closed_) {
    if (step_ != Step::kActive) {
      timeout_cv_.wait(lock, [this]() { return closed_ || step_ == Step::kActive; });
      continue;
    }

    if (is_ai_speaking_) {
      const auto epoch = timeout_epoch_;
      timeout_cv_.wait(lock, [this, epoch]() {
        return closed_ || step_ != Step::kActive || timeout_epoch_ != epoch || !is_ai_speaking_;
      });
      continue;
    }

    const auto epoch = timeout_epoch_;
    const auto deadline = timeout_deadline_;
    if (timeout_cv_.wait_until(lock, deadline, [this, epoch]() {
          return closed_ || step_ != Step::kActive || timeout_epoch_ != epoch || is_ai_speaking_;
        })) {
      continue;
    }

    step_ = Step::kIdle;
    ++timeout_epoch_;
    auto after = hooks_.after_disarm;
    lock.unlock();
    kLog->info("gate timeout -> idle");
    if (after) {
      after("timeout");
    }
    lock.lock();
  }
}

}  // namespace xiaoai_plus::wakeup
