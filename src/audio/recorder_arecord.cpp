#include "audio/recorder.hpp"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <system_error>

#include <sys/wait.h>
#include <unistd.h>

#include "common/log.hpp"

namespace xiaoai_plus::audio {

namespace {
const auto kLog = xiaoai_plus::GetLogger("recorder");
}  // namespace

ArecordRecorder::ArecordRecorder(config::Audio cfg) : cfg_(std::move(cfg)) {}

ArecordRecorder::~ArecordRecorder() { Stop(); }

bool ArecordRecorder::Start(std::function<void(const std::vector<uint8_t>&)> on_chunk) {
  if (running_.exchange(true)) {
    return true;
  }

  const std::string format = "S" + std::to_string(cfg_.bits_per_sample) + "_LE";
  const std::string sample_rate = std::to_string(cfg_.sample_rate);
  const std::string channels = std::to_string(cfg_.channels);
  const std::string buffer_size = std::to_string(cfg_.buffer_size);
  const std::string period_size = std::to_string(cfg_.period_size);
  const int bytes_per_sample = std::max(1, cfg_.bits_per_sample / 8);
  const int bytes_per_frame = bytes_per_sample * std::max(1, cfg_.channels);
  const int target_chunk = std::max(1, cfg_.buffer_size) * bytes_per_frame;

  kLog->info(
      cfg_.input_device, cfg_.sample_rate, cfg_.channels, cfg_.bits_per_sample, cfg_.buffer_size,
      cfg_.period_size, target_chunk);

  int pipefd[2] = {-1, -1};
  if (pipe(pipefd) != 0) {
    kLog->error("recorder pipe create failed: {}", std::strerror(errno));
    running_.store(false);
    return false;
  }

  pid_t pid = fork();
  if (pid < 0) {
    kLog->error("recorder fork failed: {}", std::strerror(errno));
    close(pipefd[0]);
    close(pipefd[1]);
    running_.store(false);
    return false;
  }

  if (pid == 0) {
    dup2(pipefd[1], STDOUT_FILENO);
    close(pipefd[0]);
    close(pipefd[1]);

    execlp("arecord", "arecord", "--quiet", "-t", "raw", "-D", cfg_.input_device.c_str(),
           "-f", format.c_str(), "-r", sample_rate.c_str(), "-c", channels.c_str(),
           "--buffer-size", buffer_size.c_str(), "--period-size", period_size.c_str(),
           static_cast<char*>(nullptr));
    _exit(127);
  }

  close(pipefd[1]);
  pipe_fd_ = pipefd[0];
  child_pid_ = static_cast<int>(pid);

  try {
    thread_ = std::thread([this, on_chunk = std::move(on_chunk)]() mutable { ReadLoop(on_chunk); });
  } catch (const std::system_error& e) {
    kLog->error("recorder thread creation failed: {}", e.what());
    kill(static_cast<pid_t>(child_pid_), SIGTERM);
    close(pipe_fd_);
    int status = 0;
    (void)waitpid(static_cast<pid_t>(child_pid_), &status, 0);
    pipe_fd_ = -1;
    child_pid_ = -1;
    running_.store(false);
    return false;
  }
  return true;
}

void ArecordRecorder::ReadLoop(std::function<void(const std::vector<uint8_t>&)> on_chunk) {
  const int bytes_per_sample = std::max(1, cfg_.bits_per_sample / 8);
  const int bytes_per_frame = bytes_per_sample * std::max(1, cfg_.channels);
  const size_t target_size =
      static_cast<size_t>(std::max(1, cfg_.buffer_size)) * static_cast<size_t>(bytes_per_frame);
  const size_t read_size =
      static_cast<size_t>(std::max(1, cfg_.period_size)) * static_cast<size_t>(bytes_per_frame);

  std::vector<uint8_t> buf(read_size);
  std::vector<uint8_t> acc;
  acc.reserve(target_size * 2);
  std::vector<uint8_t> chunk(target_size);
  size_t acc_read_pos = 0;
  while (running_.load()) {
    const int fd = pipe_fd_;
    if (fd < 0) {
      break;
    }

    ssize_t n = read(fd, buf.data(), read_size);
    if (n > 0) {
      acc.insert(acc.end(), buf.begin(), buf.begin() + static_cast<long>(n));
      while (acc.size() - acc_read_pos >= target_size) {
        std::memcpy(chunk.data(), acc.data() + acc_read_pos, target_size);
        on_chunk(chunk);
        acc_read_pos += target_size;
      }
      if (acc_read_pos == acc.size()) {
        acc.clear();
        acc_read_pos = 0;
      } else if (acc_read_pos >= target_size * 4) {
        acc.erase(acc.begin(), acc.begin() + static_cast<long>(acc_read_pos));
        acc_read_pos = 0;
      }
      continue;
    }

    if (n == 0) {
      break;
    }
    if (errno == EINTR) {
      continue;
    }
    if (!running_.load()) {
      break;
    }
    if (errno != EBADF) {
      kLog->error("recorder read error: {}", std::strerror(errno));
    }
    break;
  }

}

void ArecordRecorder::Stop() {
  if (!running_.exchange(false)) {
    return;
  }

  if (child_pid_ > 0) {
    kill(static_cast<pid_t>(child_pid_), SIGTERM);
  }

  if (pipe_fd_ >= 0) {
    close(pipe_fd_);
    pipe_fd_ = -1;
  }

  if (thread_.joinable()) {
    thread_.join();
  }

  if (child_pid_ > 0) {
    int status = 0;
    (void)waitpid(static_cast<pid_t>(child_pid_), &status, 0);
    child_pid_ = -1;
  }
  kLog->info("recorder stopped");
}

}  // namespace xiaoai_plus::audio
