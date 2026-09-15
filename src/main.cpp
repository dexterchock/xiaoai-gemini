#include <csignal>
#include <atomic>
#include <chrono>
#include <exception>
#include <iostream>
#include <string>
#include <thread>

#include "common/log.hpp"

#include "app/app.hpp"
#include "config/config.hpp"

namespace {
volatile std::sig_atomic_t g_stop_requested = 0;

void SignalHandler(int) {
  g_stop_requested = 1;
}
}  // namespace

int main(int argc, char** argv) {
  std::string config_path;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-c" && i + 1 < argc) {
      config_path = argv[++i];
    }
  }

  try {
    auto cfg = xiaoai_plus::config::load(config_path);
    xiaoai_plus::app::App app(cfg);
    g_stop_requested = 0;
    std::atomic<bool> watcher_done{false};

    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    std::thread signal_watcher([&app, &watcher_done]() {
      while (!watcher_done.load()) {
        if (g_stop_requested) {
          app.Stop();
          return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }
    });

    bool run_ok = false;
    try {
      run_ok = app.Run();
    } catch (...) {
      app.Stop();
      watcher_done.store(true);
      if (signal_watcher.joinable()) {
        signal_watcher.join();
      }
      throw;
    }

    watcher_done.store(true);
    if (signal_watcher.joinable()) {
      signal_watcher.join();
    }
    if (!run_ok) {
      std::cerr << "app stopped with error" << std::endl;
      return 1;
    }
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "startup failed: " << e.what() << std::endl;
    return 1;
  }
}
