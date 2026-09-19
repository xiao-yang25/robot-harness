#include "recovery_owner.hpp"

#include "recovery_protocol.hpp"

#include <cstdint>
#include <limits>
#include <memory>
#include <utility>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

namespace robot_harness::detail::recovery {
namespace {

bool is_socketpair_endpoint(int fd) noexcept {
  if (fd < 0) {
    return false;
  }
  int type = 0;
  socklen_t size = sizeof(type);
  if (::getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &size) != 0 || size != sizeof(type) ||
      type != SOCK_SEQPACKET) {
    return false;
  }
  sockaddr_storage local{};
  size = sizeof(local);
  if (::getsockname(fd, reinterpret_cast<sockaddr*>(&local), &size) != 0 ||
      local.ss_family != AF_UNIX) {
    return false;
  }
  sockaddr_storage peer{};
  size = sizeof(peer);
  return ::getpeername(fd, reinterpret_cast<sockaddr*>(&peer), &size) == 0;
}

Message reply(Kind kind, std::uint64_t session, std::uint64_t request) noexcept {
  Message message;
  message.kind = kind;
  message.session = session;
  message.request = request;
  return message;
}

}  // namespace

struct Owner::Impl {
  explicit Impl(std::string executable, std::uint64_t stop_grace_ms)
      : worker(std::move(executable)), stop_grace_ms(stop_grace_ms) {
  }

  ~Impl() {
    disconnect(last_now);
  }

  bool attach(int candidate, std::uint64_t now) {
    if (fd >= 0 || fault || epoch == std::numeric_limits<std::uint64_t>::max() ||
        (clock_seen && now < last_now) || !is_socketpair_endpoint(candidate) ||
        !ComputeProcess::can_launch(worker)) {
      if (clock_seen && now < last_now) {
        fault = true;
        disconnect(last_now);
      }
      if (epoch == std::numeric_limits<std::uint64_t>::max()) {
        fault = true;
        disconnect(last_now);
      }
      return false;
    }
    const int descriptor_flags = ::fcntl(candidate, F_GETFD);
    if (descriptor_flags < 0 || ((descriptor_flags & FD_CLOEXEC) == 0 &&
                                 ::fcntl(candidate, F_SETFD, descriptor_flags | FD_CLOEXEC) != 0)) {
      return false;
    }

    fd = candidate;
    ++epoch;
    last_now = now;
    clock_seen = true;
    active = false;
    last_request = 0;
    last_operation = 0;
    clear_token = 0;
    clear_token_at = 0;
    pending = reply(Kind::kHello, epoch, 0);
    has_pending = true;
    pending_at = now;
    return true;
  }

  bool clear() const noexcept {
    if (fault || !ComputeProcess::can_launch(worker)) {
      return false;
    }
    if (!process) {
      return true;
    }
    const auto& observed = process->observation();
    return observed.exit_status.has_value() && observed.event_eof && observed.channels_closed &&
           !observed.ownership_lost && !observed.malformed && observed.io_error == 0;
  }

  void disconnect(std::uint64_t now) noexcept {
    if (process && process->observation().launched &&
        !process->observation().exit_status.has_value()) {
      process->request_stop(now);
    }
    if (operation != 0 && !operation_handed_off && !operation_discarded) {
      ++discarded_results;
      operation_discarded = true;
    }
    operation = 0;
    operation_handed_off = false;
    result_ready = false;
    active = false;
    clear_token = 0;
    has_pending = false;
    if (fd >= 0) {
      const int owned = std::exchange(fd, -1);
      // Retrying an interrupted close risks closing an unrelated reused descriptor.
      if (::close(owned) != 0) {
        fault = true;
      }
    }
  }

  void queue(Message message, std::uint64_t now) noexcept {
    pending = message;
    pending_at = now;
    has_pending = true;
  }

  void flush(std::uint64_t now) noexcept {
    if (!has_pending || fd < 0) {
      return;
    }
    if (now - pending_at >= kEvidenceLifetimeMs) {
      disconnect(now);
      return;
    }
    const Io sent = send_message(fd, pending);
    if (sent == Io::kClosed) {
      disconnect(now);
    } else if (sent == Io::kDone) {
      if (pending.kind == Kind::kResult) {
        operation_handed_off = true;
        operation = 0;
        result_ready = false;
      }
      has_pending = false;
    }
  }

  void observe_native(std::uint64_t now) noexcept {
    if (!process) {
      return;
    }
    process->poll(now);
    const auto& observed = process->observation();
    if (observed.ownership_lost || observed.malformed || observed.io_error != 0) {
      fault = true;
      disconnect(now);
    }
    if (!observed.exit_status.has_value() || !observed.event_eof) {
      return;
    }
    if (!process->close_channels()) {
      fault = true;
      disconnect(now);
      return;
    }
    last_observation = process->observation();
    process.reset();
    if (operation == 0 || fd < 0) {
      return;
    }
    result = reply(Kind::kResult, operation_session, operation_request);
    result.operation = operation;
    result.amount = amount;
    if (last_observation.exit_status.has_value() && WIFEXITED(*last_observation.exit_status)) {
      const int code = WEXITSTATUS(*last_observation.exit_status);
      if (code == 0 && last_observation.terminal == ProcessObservation::Terminal::kCompleted &&
          last_observation.terminal_iterations == amount) {
        result.flags = kSucceeded;
        result.value = last_observation.terminal_value;
      } else if (code == 20 && last_observation.stop_requested &&
                 last_observation.terminal == ProcessObservation::Terminal::kCancelled &&
                 last_observation.terminal_iterations <= amount) {
        result.flags = kCancelled;
      }
    }
    if (result.flags == 0) {
      result.flags = kFailed;
    }
    result_ready = true;
  }

  void receive(std::uint64_t now) {
    Message message;
    const Io received = receive_message(fd, message);
    if (received == Io::kAgain) {
      return;
    }
    if (received == Io::kClosed || message.session != epoch || message.request == 0 ||
        message.request <= last_request) {
      disconnect(now);
      return;
    }

    if (message.kind == Kind::kQuery && !active && message.operation == 0 && message.amount == 0 &&
        message.value == 0 && message.flags == 0) {
      last_request = message.request;
      Message snapshot = reply(Kind::kSnapshot, epoch, message.request);
      if (clear()) {
        snapshot.flags = kClear;
        clear_token = message.request;
        clear_token_at = now;
      } else {
        clear_token = 0;
      }
      queue(snapshot, now);
      return;
    }

    if (message.kind == Kind::kActivate && !active && message.operation == 0 &&
        message.amount == 0 && message.flags == 0 && clear_token != 0 &&
        message.value == clear_token && now - clear_token_at < kEvidenceLifetimeMs && clear()) {
      last_request = message.request;
      active = true;
      clear_token = 0;
      Message activated = reply(Kind::kActivated, epoch, message.request);
      activated.value = message.value;
      queue(activated, now);
      return;
    }

    if (message.kind == Kind::kSubmit && active && operation == 0 &&
        message.operation > last_operation && message.amount > 0 &&
        message.amount <= kMaximumIterations && message.value == 0 && message.flags == 0 &&
        clear()) {
      // Allocation and copying happen before launch, so a failed allocation cannot
      // leave a spawned child without a retained owner.
      std::unique_ptr<ComputeProcess> candidate;
      try {
        candidate = std::make_unique<ComputeProcess>(worker, message.amount, stop_grace_ms);
      } catch (...) {
        fault = true;
        disconnect(now);
        return;
      }
      const bool launched = candidate->launch();
      last_request = message.request;
      last_operation = message.operation;
      last_observation = candidate->observation();
      if (last_observation.ownership_lost || last_observation.malformed ||
          last_observation.io_error != 0) {
        fault = true;
        disconnect(now);
        return;
      }
      if (!launched) {
        Message rejected = reply(Kind::kResult, epoch, message.request);
        rejected.operation = message.operation;
        rejected.amount = message.amount;
        rejected.flags = kNotLaunched;
        queue(rejected, now);
        return;
      }
      operation = message.operation;
      operation_session = epoch;
      operation_request = message.request;
      amount = message.amount;
      operation_handed_off = false;
      operation_discarded = false;
      process = std::move(candidate);
      ++launches;
      Message accepted = reply(Kind::kAccepted, epoch, message.request);
      accepted.operation = operation;
      accepted.amount = amount;
      queue(accepted, now);
      return;
    }

    disconnect(now);
  }

  void poll(std::uint64_t now) {
    if (clock_seen && now < last_now) {
      fault = true;
      disconnect(last_now);
      now = last_now;
    } else {
      last_now = now;
      clock_seen = true;
    }
    observe_native(now);
    if (fd < 0) {
      return;
    }
    if (!has_pending && result_ready) {
      queue(result, now);
    }
    if (has_pending) {
      flush(now);
      if (fd < 0) {
        return;
      }
    }
    if (!has_pending && result_ready) {
      queue(result, now);
    }
    if (!has_pending) {
      receive(now);
    }
  }

  std::string worker;
  std::uint64_t stop_grace_ms;
  std::unique_ptr<ComputeProcess> process;
  ProcessObservation last_observation;
  int fd = -1;
  std::uint64_t epoch = 1;
  std::uint64_t last_now = 0;
  bool clock_seen = false;
  bool fault = false;
  bool active = false;
  std::uint64_t last_request = 0;
  std::uint64_t last_operation = 0;
  std::uint64_t clear_token = 0;
  std::uint64_t clear_token_at = 0;
  Message pending;
  bool has_pending = false;
  std::uint64_t pending_at = 0;
  std::uint64_t operation = 0;
  std::uint64_t operation_session = 0;
  std::uint64_t operation_request = 0;
  std::uint64_t amount = 0;
  bool operation_handed_off = false;
  bool operation_discarded = false;
  Message result;
  bool result_ready = false;
  std::uint64_t launches = 0;
  std::uint64_t discarded_results = 0;
};

Owner::Owner(std::string worker, std::uint64_t stop_grace_ms)
    : impl_(std::make_unique<Impl>(std::move(worker), stop_grace_ms)) {
}

Owner::~Owner() = default;

bool Owner::attach(int fd, std::uint64_t now) {
  return impl_->attach(fd, now);
}

void Owner::poll(std::uint64_t now) {
  impl_->poll(now);
}

bool Owner::connected() const noexcept {
  return impl_->fd >= 0;
}

bool Owner::clear() const noexcept {
  return impl_->clear();
}

bool Owner::faulted() const noexcept {
  return impl_->fault;
}

std::uint64_t Owner::session() const noexcept {
  return impl_->epoch;
}

std::uint64_t Owner::launches() const noexcept {
  return impl_->launches;
}

std::uint64_t Owner::discarded_results() const noexcept {
  return impl_->discarded_results;
}

ProcessObservation Owner::observation() const {
  return impl_->process ? impl_->process->observation() : impl_->last_observation;
}

}  // namespace robot_harness::detail::recovery
