#ifndef ROBOT_HARNESS_COMPUTE_PROCESS_HPP_
#define ROBOT_HARNESS_COMPUTE_PROCESS_HPP_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace robot_harness::detail {

struct ProcessObservation {
  bool launched = false;
  bool started = false;
  bool malformed = false;
  bool event_eof = false;
  bool channels_closed = false;
  bool stop_requested = false;
  bool stop_delivered = false;
  bool stop_acknowledged = false;
  bool stop_ignored = false;
  bool termination_requested = false;
  bool ownership_lost = false;
  int launch_error = 0;
  int io_error = 0;
  int stop_error = 0;
  int termination_error = 0;
  int child_pid = -1;
  std::optional<int> exit_status;
  std::uint64_t progress = 0;
  std::uint64_t dropped_progress = 0;
  enum class Terminal { kNone, kCompleted, kCancelled } terminal = Terminal::kNone;
  std::uint64_t terminal_iterations = 0;
  std::uint64_t terminal_value = 0;
  std::optional<std::uint64_t> stop_requested_at;
};

class ComputeProcess final {
public:
  ComputeProcess(std::string executable, std::uint64_t iterations, std::uint64_t stop_grace_ms);
  ~ComputeProcess();

  static bool can_launch(const std::string& executable) noexcept;
  bool launch();
  void request_stop(std::uint64_t now_ms);
  void poll(std::uint64_t now_ms);
  bool close_channels();
  const ProcessObservation& observation() const noexcept;

  ComputeProcess(const ComputeProcess&) = delete;
  ComputeProcess& operator=(const ComputeProcess&) = delete;
  ComputeProcess(ComputeProcess&&) = delete;
  ComputeProcess& operator=(ComputeProcess&&) = delete;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace robot_harness::detail

#endif  // ROBOT_HARNESS_COMPUTE_PROCESS_HPP_
