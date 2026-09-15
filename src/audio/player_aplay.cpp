#include "audio/player.hpp"

#include <chrono>
#include <signal.h>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <system_error>
#include <unistd.h>
#include <vector>

#include "common/log.hpp"

namespace xiaoai_plus::audio {

namespace {
const auto kLog = xiaoai_plus::GetLogger("player");
}  // namespace

AplayPlayer::AplayPlayer(config::Audio cfg, int queue_frames)
    : cfg_(std::move(cfg)), queue_frames_(queue_frames > 0 ? queue_frames : 128) {}

AplayPlayer::~AplayPlayer() { Close(); }

bool AplayPlayer::Start() {
  if (running_.exchange(true)) {
    return true;
  }

  try {
    thread_ = std::thread([this]() { WriteLoop(); });
  } catch (const std::system_error& e) {
    kLog->error("player thread creation failed: {}", e.what());
    running_.store(false);
    return false;
  }
  kLog->info("player started");
  return true;
}

bool AplayPlayer::Play(const std::vector<uint8_t>& chunk) {
  if (chunk.empty()) {
    return true;
  }

  std::lock_guard<std::mutex> lock(mu_);
  if (static_cast<int>(queue_.size()) >= queue_frames_) {
    return false;
  }
  queue_.push_back(chunk);
  cv_.notify_one();
  return true;
}

void AplayPlayer::Interrupt() {
  {
    std::lock_guard<std::mutex> lock(mu_);
    queue_.clear();
  }
  std::lock_guard<std::mutex> pipe_lock(pipe_mu_);
  ClosePipeLocked(true);
}

void AplayPlayer::SetOnChunkPlayed(std::function<void(const std::vector<uint8_t>&)> cb) {
  std::lock_guard<std::mutex> lock(cb_mu_);
  on_chunk_played_ = std::move(cb);
}

bool AplayPlayer::OpenPipeLocked() {
  if (aplay_fd_ >= 0) {
    return true;
  }

  std::string fmt = "S" + std::to_string(cfg_.bits_per_sample) + "_LE";
  std::string rate = std::to_string(cfg_.sample_rate);
  std::string channels = std::to_string(cfg_.channels);
  std::string buffer_size = std::to_string(cfg_.buffer_size);
  std::string period_size = std::to_string(cfg_.period_size);

  std::vector<char*> argv = {
      const_cast<char*>("aplay"),
      const_cast<char*>("--quiet"),
      const_cast<char*>("-t"),           const_cast<char*>("raw"),
      const_cast<char*>("-D"),           const_cast<char*>(cfg_.output_device.c_str()),
      const_cast<char*>("-f"),           const_cast<char*>(fmt.c_str()),
      const_cast<char*>("-r"),           const_cast<char*>(rate.c_str()),
      const_cast<char*>("-c"),           const_cast<char*>(channels.c_str()),
      const_cast<char*>("--buffer-size"), const_cast<char*>(buffer_size.c_str()),
      const_cast<char*>("--period-size"), const_cast<char*>(period_size.c_str()),
      const_cast<char*>("-"),
      nullptr,
  };

  int pipefd[2];
  if (pipe(pipefd) != 0) {
    kLog->error("player pipe() failed");
    return false;
  }

  pid_t pid = fork();
  if (pid < 0) {
    close(pipefd[0]);
    close(pipefd[1]);
    kLog->error("player fork() failed");
    return false;
  }

  if (pid == 0) {
    // child: wire read-end to stdin, exec aplay
    close(pipefd[1]);
    if (dup2(pipefd[0], STDIN_FILENO) < 0) { _exit(1); }
    close(pipefd[0]);
    execvp("aplay", argv.data());
    _exit(1);
  }

  // parent
  close(pipefd[0]);
  aplay_pid_ = pid;
  aplay_fd_ = pipefd[1];
  return true;
}

void AplayPlayer::ClosePipeLocked(bool kill_now) {
  const pid_t pid = aplay_pid_;
  const int fd = aplay_fd_;
  aplay_pid_ = -1;
  aplay_fd_ = -1;

  if (pid > 0 && kill_now) {
    kill(pid, SIGKILL);
  }
  if (fd >= 0) {
    close(fd);
  }
  if (pid > 0) {
    waitpid(pid, nullptr, 0);
  }
}

void AplayPlayer::WriteLoop() {
  const auto idle_timeout = std::chrono::milliseconds(1200);
  while (running_.load()) {
    std::vector<uint8_t> chunk;
    bool timed_out = false;
    {
      std::unique_lock<std::mutex> lock(mu_);
      if (!cv_.wait_for(lock, idle_timeout, [this]() { return !running_.load() || !queue_.empty(); })) {
        timed_out = true;
      }
      if (!running_.load()) {
        break;
      }
      if (timed_out && queue_.empty()) {
        lock.unlock();
        std::lock_guard<std::mutex> pipe_lock(pipe_mu_);
        ClosePipeLocked(false);
        continue;
      }
      chunk = std::move(queue_.front());
      queue_.pop_front();
    }

    if (chunk.empty()) {
      continue;
    }

    bool write_ok = false;
    {
      std::lock_guard<std::mutex> pipe_lock(pipe_mu_);
      if (!OpenPipeLocked()) {
        continue;
      }
      const ssize_t written = write(aplay_fd_, chunk.data(), chunk.size());
      if (written < 0 || static_cast<size_t>(written) < chunk.size()) {
        kLog->warn("player write failed (written={}/{})",
                     written < 0 ? 0 : written, chunk.size());
        ClosePipeLocked(false);
      } else {
        write_ok = true;
      }
    }
    if (!write_ok) {
      continue;
    }

    std::function<void(const std::vector<uint8_t>&)> cb;
    {
      std::lock_guard<std::mutex> lock(cb_mu_);
      cb = on_chunk_played_;
    }
    if (cb) {
      cb(chunk);
    }
  }
}

void AplayPlayer::Close() {
  if (!running_.exchange(false)) {
    return;
  }

  cv_.notify_all();

  if (thread_.joinable()) {
    thread_.join();
  }

  {
    std::lock_guard<std::mutex> pipe_lock(pipe_mu_);
    ClosePipeLocked(true);
  }

  std::lock_guard<std::mutex> lock(mu_);
  queue_.clear();
  kLog->info("player stopped");
}

}  // namespace xiaoai_plus::audio
