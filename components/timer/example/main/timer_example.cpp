#include <chrono>
#include <vector>

#include "logger.hpp"
#include "timer.hpp"

using namespace std::chrono_literals;

extern "C" void app_main(void) {
  espp::Logger logger({.tag = "Timer example", .level = espp::Logger::Verbosity::DEBUG});
  size_t num_seconds_to_run = 3;
  static auto start = std::chrono::high_resolution_clock::now();

  static auto elapsed = []() {
    auto now = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<float>(now - start).count();
  };

  // basic timer example
  {
    logger.info("[{:.3f}] Starting basic timer example", elapsed());
    //! [timer example]
    auto timer_fn = []() {
      static size_t iterations{0};
      fmt::print("[{:.3f}] #iterations = {}\n", elapsed(), iterations);
      iterations++;
      // we don't want to stop, so return false
      return false;
    };
    auto timer = espp::Timer({.name = "Timer 1",
                              .period = 500ms,
                              .callback = timer_fn,
                              .log_level = espp::Logger::Verbosity::DEBUG});
    //! [timer example]
    std::this_thread::sleep_for(num_seconds_to_run * 1s);
  }

  // timer with delay example
  {
    logger.info("[{:.3f}] Starting timer with delay example", elapsed());
    //! [timer delay example]
    auto timer_fn = []() {
      static size_t iterations{0};
      fmt::print("[{:.3f}] #iterations = {}\n", elapsed(), iterations);
      iterations++;
      // we don't want to stop, so return false
      return false;
    };
    auto timer =
        espp::Timer({.name = "Timer 1",
                     .period = 500ms,
                     .delay = 500ms,
                     .callback = timer_fn,
                     .auto_start = false, // don't start the timer automatically, we'll call start()
                     .log_level = espp::Logger::Verbosity::DEBUG});
    timer.start();
    std::this_thread::sleep_for(2s);
    logger.info("[{:.3f}] Cancelling timer for 2 seconds", elapsed());
    timer.cancel();
    std::this_thread::sleep_for(2s);
    timer.start();
    std::this_thread::sleep_for(2s);
    logger.info("[{:.3f}] Cancelling timer for 2 seconds", elapsed());
    timer.cancel();
    std::this_thread::sleep_for(2s);
    timer.start(1s);
    //! [timer delay example]
    std::this_thread::sleep_for(num_seconds_to_run * 1s);
  }

  // oneshot timer example
  {
    logger.info("[{:.3f}] Starting oneshot timer example", elapsed());
    //! [timer oneshot example]
    auto timer_fn = []() {
      static size_t iterations{0};
      fmt::print("[{:.3f}] #iterations = {}\n", elapsed(), iterations);
      iterations++;
      // we don't want to stop, so return false
      return false;
    };
    auto timer = espp::Timer({.name = "Timer 1",
                              .period = 0ms, // one shot timer
                              .delay = 500ms,
                              .callback = timer_fn,
                              .log_level = espp::Logger::Verbosity::DEBUG});
    //! [timer oneshot example]
    std::this_thread::sleep_for(num_seconds_to_run * 1s);
  }

  // timer cancel itself example
  {
    logger.info("[{:.3f}] Starting timer cancel itself example", elapsed());
    //! [timer cancel itself example]
    auto timer_fn = []() {
      static size_t iterations{0};
      fmt::print("[{:.3f}] #iterations = {}\n", elapsed(), iterations);
      iterations++;
      // cancel the timer after 3 iterations
      if (iterations == 3) {
        fmt::print("[{:.3f}] auto-cancelling timer\n", elapsed());
        return true;
      }
      return false;
    };
    auto timer = espp::Timer({.name = "Timer 1",
                              .period = 500ms,
                              .callback = timer_fn,
                              .log_level = espp::Logger::Verbosity::DEBUG});
    //! [timer cancel itself example]
    std::this_thread::sleep_for(num_seconds_to_run * 1s);
  }

  logger.info("Test ran for {:.03f} seconds", elapsed());

  logger.info("Example complete!");

  while (true) {
    std::this_thread::sleep_for(1s);
  }
}
