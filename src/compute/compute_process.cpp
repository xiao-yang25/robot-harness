#include "compute_process.hpp"

#include "compute_protocol.hpp"

#include <array>
#include <cerrno>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>
#include <utility>

#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace robot_harness::detail {
namespace {

constexpr int kMinimumPrivateDescriptor = 10;
constexpr std::size_t kMaximumFramesPerPoll = 128;

struct CloseReport {
  int* error = nullptr;
  bool* unknown = nullptr;
};

class Descriptor final {
public:
  Descriptor() noexcept = default;
  Descriptor(int descriptor, CloseReport report) noexcept
      : descriptor_(descriptor), report_(report) {
  }

  ~Descriptor() {
    reset();
  }

  Descriptor(const Descriptor&) = delete;
  Descriptor& operator=(const Descriptor&) = delete;

  Descriptor(Descriptor&& other) noexcept
      : descriptor_(std::exchange(other.descriptor_, -1)), report_(other.report_) {
  }

  Descriptor& operator=(Descriptor&& other) noexcept {
    if (this != &other) {
      reset();
      descriptor_ = std::exchange(other.descriptor_, -1);
      report_ = other.report_;
    }
    return *this;
  }

  int get() const noexcept {
    return descriptor_;
  }

  int release() noexcept {
    return std::exchange(descriptor_, -1);
  }

  void reset() noexcept {
    if (descriptor_ >= 0) {
      // Retrying close after EINTR can close an unrelated, reused descriptor.
      if (::close(std::exchange(descriptor_, -1)) != 0) {
        if (*report_.error == 0)
          *report_.error = errno == 0 ? EIO : errno;
        *report_.unknown = true;
      }
    }
  }

private:
  int descriptor_ = -1;
  CloseReport report_;
};

struct ChannelPair {
  Descriptor first;
  Descriptor second;
};

int executable_error(const std::string& executable) noexcept {
  if (executable.empty() || executable.front() != '/') {
    return EINVAL;
  }

  struct stat status{};
  if (::stat(executable.c_str(), &status) != 0) {
    return errno == 0 ? ENOENT : errno;
  }
  if (!S_ISREG(status.st_mode)) {
    return EACCES;
  }
  if (::access(executable.c_str(), X_OK) != 0) {
    return errno == 0 ? EACCES : errno;
  }

  struct sigaction action{};
  if (::sigaction(SIGCHLD, nullptr, &action) != 0) {
    return errno == 0 ? EINVAL : errno;
  }
  if (action.sa_handler != SIG_DFL || (action.sa_flags & SA_NOCLDWAIT) != 0) {
    return EBUSY;
  }
  return 0;
}

bool set_nonblocking(int descriptor, int* error) noexcept {
  const int flags = ::fcntl(descriptor, F_GETFL);
  if (flags < 0 || ::fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) != 0) {
    *error = errno == 0 ? EIO : errno;
    return false;
  }
  return true;
}

bool duplicate_pair(const int originals[2], ChannelPair* pair, int* error,
                    CloseReport report) noexcept {
  const int first = ::fcntl(originals[0], F_DUPFD_CLOEXEC, kMinimumPrivateDescriptor);
  if (first < 0) {
    *error = errno == 0 ? EMFILE : errno;
    return false;
  }
  Descriptor first_owner(first, report);

  const int second = ::fcntl(originals[1], F_DUPFD_CLOEXEC, kMinimumPrivateDescriptor);
  if (second < 0) {
    *error = errno == 0 ? EMFILE : errno;
    return false;
  }
  Descriptor second_owner(second, report);

  if (!set_nonblocking(first, error) || !set_nonblocking(second, error)) {
    return false;
  }
  pair->first = std::move(first_owner);
  pair->second = std::move(second_owner);
  return true;
}

bool make_control_pair(ChannelPair* pair, int* error, CloseReport report) noexcept {
  int originals[2] = {-1, -1};
  if (::socketpair(AF_UNIX, SOCK_STREAM, 0, originals) != 0) {
    *error = errno == 0 ? EMFILE : errno;
    return false;
  }
  Descriptor first(originals[0], report);
  Descriptor second(originals[1], report);
  if (!duplicate_pair(originals, pair, error, report)) {
    return false;
  }

#if defined(__APPLE__)
  // Suppress SIGPIPE only on the owned parent socket; never alter host process disposition.
  const int enabled = 1;
  if (::setsockopt(pair->second.get(), SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) != 0) {
    *error = errno == 0 ? EIO : errno;
    return false;
  }
#endif
  return true;
}

bool make_event_pipe(ChannelPair* pair, int* error, CloseReport report) noexcept {
  int originals[2] = {-1, -1};
  if (::pipe(originals) != 0) {
    *error = errno == 0 ? EMFILE : errno;
    return false;
  }
  Descriptor first(originals[0], report);
  Descriptor second(originals[1], report);
  // Fixed frames remain atomic on the pipe. The worker handles its own SIGPIPE locally.
  return duplicate_pair(originals, pair, error, report);
}

bool is_known_kind(compute_protocol::FrameKind kind) noexcept {
  using compute_protocol::FrameKind;
  switch (kind) {
  case FrameKind::kReady:
  case FrameKind::kProgress:
  case FrameKind::kStopAcknowledged:
  case FrameKind::kStopIgnored:
  case FrameKind::kCompleted:
  case FrameKind::kCancelled:
    return true;
  }
  return false;
}

}  // namespace

class ComputeProcess::Impl final {
public:
  Impl(std::string executable, std::uint64_t iterations, std::uint64_t stop_grace_ms)
      : executable_(std::move(executable)), iterations_(iterations), stop_grace_ms_(stop_grace_ms) {
  }

  ~Impl() {
    cleanup_owned_child();
  }

  bool launch() noexcept {
    if (observation_.launched) {
      return true;
    }
    if (observation_.stop_requested) {
      observation_.launch_error = EINVAL;
      return false;
    }
    if (iterations_ == 0 || iterations_ > compute_protocol::kMaximumIterations) {
      observation_.launch_error = EINVAL;
      return false;
    }

    observation_.launch_error = executable_error(executable_);
    if (observation_.launch_error != 0) {
      return false;
    }

    std::array<char, 32> iteration_argument{};
    const auto conversion =
        std::to_chars(iteration_argument.data(),
                      iteration_argument.data() + iteration_argument.size(), iterations_);
    if (conversion.ec != std::errc{}) {
      observation_.launch_error = EINVAL;
      return false;
    }
    *conversion.ptr = '\0';

    int error = 0;
    ChannelPair control;
    ChannelPair events;
    const CloseReport report{&observation_.io_error, &channel_cleanup_unknown_};
    if (!make_control_pair(&control, &error, report) || !make_event_pipe(&events, &error, report) ||
        channel_cleanup_unknown_) {
      observation_.launch_error = error != 0 ? error : observation_.io_error;
      return false;
    }

    posix_spawn_file_actions_t actions;
    error = ::posix_spawn_file_actions_init(&actions);
    if (error != 0) {
      observation_.launch_error = error;
      return false;
    }

    error = ::posix_spawn_file_actions_adddup2(&actions, control.first.get(),
                                               compute_protocol::kControlDescriptor);
    if (error == 0) {
      error = ::posix_spawn_file_actions_adddup2(&actions, events.second.get(),
                                                 compute_protocol::kEventDescriptor);
    }

    pid_t child = -1;
    char* arguments[] = {const_cast<char*>(executable_.c_str()), const_cast<char*>("--iterations"),
                         iteration_argument.data(), nullptr};
    if (error == 0) {
      // Unrelated non-CLOEXEC descriptors remain the embedding host's responsibility.
      error = ::posix_spawn(&child, executable_.c_str(), &actions, nullptr, arguments, environ);
    }
    ::posix_spawn_file_actions_destroy(&actions);
    if (error != 0 || child <= 0) {
      observation_.launch_error = error != 0 ? error : ECHILD;
      return false;
    }

    // Everything after successful spawn is fixed-size, non-throwing ownership transfer.
    owned_pid_ = child;
    control_descriptor_ = control.second.release();
    event_descriptor_ = events.first.release();
    observation_.launched = true;
    observation_.child_pid = static_cast<int>(child);
    observation_.launch_error = 0;
    return true;
  }

  void request_stop(std::uint64_t now_ms) noexcept {
    if (observation_.stop_requested) {
      return;
    }
    observation_.stop_requested = true;
    observation_.stop_requested_at = now_ms;

    if (owned_pid_ <= 0 || observation_.exit_status.has_value() || control_descriptor_ < 0) {
      termination_pending_ = owned_pid_ > 0 && !observation_.exit_status.has_value();
      return;
    }

    const unsigned char command = compute_protocol::kStopCommand;
#if defined(MSG_NOSIGNAL)
    const ssize_t result = ::send(control_descriptor_, &command, sizeof(command), MSG_NOSIGNAL);
#else
    const ssize_t result = ::send(control_descriptor_, &command, sizeof(command), 0);
#endif
    if (result == static_cast<ssize_t>(sizeof(command))) {
      observation_.stop_delivered = true;
      return;
    }

    const int error = result < 0 ? errno : EIO;
    observation_.stop_error = error == 0 ? EIO : error;
    termination_pending_ = true;
  }

  void poll(std::uint64_t now_ms) noexcept {
    if (!observation_.launched || observation_.ownership_lost || observation_.channels_closed) {
      return;
    }

    drain_events();
    collect_exit();

    if (observation_.malformed || observation_.io_error != 0) {
      termination_pending_ = true;
    }
    if (observation_.stop_requested && !observation_.exit_status.has_value()) {
      if (!observation_.stop_delivered) {
        termination_pending_ = true;
      } else if (observation_.stop_requested_at.has_value() &&
                 now_ms >= *observation_.stop_requested_at &&
                 now_ms - *observation_.stop_requested_at >= stop_grace_ms_) {
        termination_pending_ = true;
      }
    }
    request_termination_once();
  }

  bool close_channels() noexcept {
    if (observation_.channels_closed) {
      return true;
    }
    if (!observation_.exit_status.has_value() || !observation_.event_eof ||
        observation_.ownership_lost || channel_cleanup_unknown_) {
      return false;
    }

    const bool control_closed = close_once(&control_descriptor_);
    const bool event_closed = close_once(&event_descriptor_);
    observation_.channels_closed = control_closed && event_closed;
    return observation_.channels_closed;
  }

  const ProcessObservation& observation() const noexcept {
    return observation_;
  }

private:
  void remember_io_error(int error) noexcept {
    if (observation_.io_error == 0) {
      observation_.io_error = error == 0 ? EIO : error;
    }
  }

  void consume(const compute_protocol::Frame& frame) noexcept {
    using compute_protocol::FrameKind;
    if (observation_.malformed) {
      return;
    }
    if (frame.magic != compute_protocol::kFrameMagic ||
        frame.version != compute_protocol::kFrameVersion || !is_known_kind(frame.kind) ||
        frame.iterations > iterations_ || frame.iterations < last_iterations_ ||
        frame.dropped_progress > iterations_ ||
        frame.dropped_progress < observation_.dropped_progress ||
        observation_.terminal != ProcessObservation::Terminal::kNone) {
      observation_.malformed = true;
      return;
    }

    switch (frame.kind) {
    case FrameKind::kReady:
      if (observation_.started || frame.iterations != 0 || frame.value != 0 ||
          frame.dropped_progress != 0) {
        observation_.malformed = true;
        return;
      }
      observation_.started = true;
      break;
    case FrameKind::kProgress:
      if (!observation_.started || observation_.stop_acknowledged ||
          frame.iterations <= last_iterations_) {
        observation_.malformed = true;
        return;
      }
      observation_.progress = frame.iterations;
      break;
    case FrameKind::kStopAcknowledged:
      if (!observation_.started || !observation_.stop_requested || observation_.stop_acknowledged ||
          observation_.stop_ignored || frame.iterations < observation_.progress) {
        observation_.malformed = true;
        return;
      }
      observation_.stop_acknowledged = true;
      break;
    case FrameKind::kStopIgnored:
      if (!observation_.started || !observation_.stop_requested || observation_.stop_acknowledged ||
          observation_.stop_ignored || frame.iterations < observation_.progress) {
        observation_.malformed = true;
        return;
      }
      observation_.stop_ignored = true;
      break;
    case FrameKind::kCompleted:
      if (!observation_.started || observation_.stop_acknowledged ||
          frame.iterations != iterations_ || frame.iterations < observation_.progress) {
        observation_.malformed = true;
        return;
      }
      observation_.terminal = ProcessObservation::Terminal::kCompleted;
      observation_.terminal_iterations = frame.iterations;
      observation_.terminal_value = frame.value;
      break;
    case FrameKind::kCancelled:
      if (!observation_.started || !observation_.stop_requested ||
          !observation_.stop_acknowledged || frame.iterations < observation_.progress) {
        observation_.malformed = true;
        return;
      }
      observation_.terminal = ProcessObservation::Terminal::kCancelled;
      observation_.terminal_iterations = frame.iterations;
      observation_.terminal_value = frame.value;
      break;
    }
    last_iterations_ = frame.iterations;
    observation_.dropped_progress = frame.dropped_progress;
  }

  void drain_events() noexcept {
    if (event_descriptor_ < 0 || observation_.event_eof) {
      return;
    }

    std::size_t frames = 0;
    while (frames < kMaximumFramesPerPoll) {
      const ssize_t count = ::read(event_descriptor_, frame_buffer_.data() + buffered_frame_bytes_,
                                   frame_buffer_.size() - buffered_frame_bytes_);
      if (count > 0) {
        buffered_frame_bytes_ += static_cast<std::size_t>(count);
        if (buffered_frame_bytes_ == frame_buffer_.size()) {
          compute_protocol::Frame frame{};
          std::memcpy(&frame, frame_buffer_.data(), sizeof(frame));
          buffered_frame_bytes_ = 0;
          ++frames;
          consume(frame);
        }
        continue;
      }
      if (count == 0) {
        observation_.event_eof = true;
        if (buffered_frame_bytes_ != 0 ||
            observation_.terminal == ProcessObservation::Terminal::kNone) {
          observation_.malformed = true;
        }
        return;
      }
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return;
      }
      if (errno == EINTR) {
        return;
      }
      remember_io_error(errno);
      return;
    }
  }

  void collect_exit() noexcept {
    if (owned_pid_ <= 0 || observation_.exit_status.has_value()) {
      return;
    }

    int status = 0;
    const pid_t result = ::waitpid(owned_pid_, &status, WNOHANG);
    if (result == owned_pid_) {
      owned_pid_ = -1;
      observation_.exit_status = status;
      return;
    }
    if (result == 0 || (result < 0 && errno == EINTR)) {
      return;
    }
    if (result < 0 && errno == ECHILD) {
      owned_pid_ = -1;
      observation_.ownership_lost = true;
      return;
    }
    if (result < 0) {
      remember_io_error(errno);
      termination_pending_ = true;
    }
  }

  void request_termination_once() noexcept {
    if (!termination_pending_ || termination_attempted_ || owned_pid_ <= 0 ||
        observation_.exit_status.has_value() || observation_.ownership_lost) {
      return;
    }
    termination_attempted_ = true;
    observation_.termination_requested = true;
    if (::kill(owned_pid_, SIGKILL) != 0) {
      observation_.termination_error = errno == 0 ? EIO : errno;
    }
  }

  bool close_once(int* descriptor) noexcept {
    if (*descriptor < 0) {
      return true;
    }
    const int target = std::exchange(*descriptor, -1);
    if (::close(target) == 0) {
      return true;
    }
    remember_io_error(errno);
    channel_cleanup_unknown_ = true;
    return false;
  }

  void cleanup_owned_child() noexcept {
    if (observation_.ownership_lost) {
      std::fputs("robot_harness: compute child ownership lost\n", stderr);
      std::terminate();
    }

    if (owned_pid_ > 0) {
      if (::kill(owned_pid_, SIGKILL) != 0 && errno != ESRCH) {
        std::fputs("robot_harness: compute child cleanup signal failed\n", stderr);
        std::terminate();
      }

      int status = 0;
      pid_t result = -1;
      do {
        result = ::waitpid(owned_pid_, &status, 0);
      } while (result < 0 && errno == EINTR);
      if (result != owned_pid_) {
        std::fputs("robot_harness: compute child cleanup reap failed\n", stderr);
        std::terminate();
      }
      owned_pid_ = -1;
      observation_.exit_status = status;
    }

    if (control_descriptor_ >= 0 && ::close(std::exchange(control_descriptor_, -1)) != 0) {
      channel_cleanup_unknown_ = true;
    }
    if (event_descriptor_ >= 0 && ::close(std::exchange(event_descriptor_, -1)) != 0) {
      channel_cleanup_unknown_ = true;
    }
    if (channel_cleanup_unknown_) {
      std::fputs("robot_harness: compute channel cleanup outcome unknown\n", stderr);
      std::terminate();
    }
  }

  std::string executable_;
  const std::uint64_t iterations_;
  const std::uint64_t stop_grace_ms_;
  ProcessObservation observation_;
  pid_t owned_pid_ = -1;
  int control_descriptor_ = -1;
  int event_descriptor_ = -1;
  std::array<unsigned char, sizeof(compute_protocol::Frame)> frame_buffer_{};
  std::size_t buffered_frame_bytes_ = 0;
  std::uint64_t last_iterations_ = 0;
  bool termination_pending_ = false;
  bool termination_attempted_ = false;
  bool channel_cleanup_unknown_ = false;
};

ComputeProcess::ComputeProcess(std::string executable, std::uint64_t iterations,
                               std::uint64_t stop_grace_ms)
    : impl_(std::make_unique<Impl>(std::move(executable), iterations, stop_grace_ms)) {
}

ComputeProcess::~ComputeProcess() = default;

bool ComputeProcess::can_launch(const std::string& executable) noexcept {
  return executable_error(executable) == 0;
}

bool ComputeProcess::launch() {
  return impl_->launch();
}

void ComputeProcess::request_stop(std::uint64_t now_ms) {
  impl_->request_stop(now_ms);
}

void ComputeProcess::poll(std::uint64_t now_ms) {
  impl_->poll(now_ms);
}

bool ComputeProcess::close_channels() {
  return impl_->close_channels();
}

const ProcessObservation& ComputeProcess::observation() const noexcept {
  return impl_->observation();
}

}  // namespace robot_harness::detail
