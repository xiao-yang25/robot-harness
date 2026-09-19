#include "recovery_client.hpp"
#include "recovery_owner.hpp"
#include "recovery_protocol.hpp"
#include "recovery_task_execution.hpp"
#include "robot_harness/compute_execution.hpp"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <iostream>
#include <spawn.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

extern char** environ;
namespace {
using namespace robot_harness;
using namespace robot_harness::examples;
using namespace robot_harness::detail::recovery;
constexpr int kSessionFd = 64;
constexpr int kReportFd = 65;
struct Report {
  std::uint64_t code;
  std::uint64_t session;
  std::uint64_t value;
};
void require(bool condition, const char* message) {
  if (!condition)
    throw std::runtime_error(message);
}
MonotonicTime now() {
  return ComputeExecutionHost::now();
}
void pause_tick() {
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
}
void report(std::uint64_t code, const Client& client, std::uint64_t value = 0) {
  const Report r{code, client.session(), value};
  require(::send(kReportFd, &r, sizeof(r), MSG_NOSIGNAL) == sizeof(r), "host report failed");
}
int task_host_main(const std::string& mode) {
  Client client(kSessionFd, now());
  RecoveryTaskExecution execution(client);
  auto task = std::make_unique<FiniteComputeTask>(execution);
  const bool restarted = mode.rfind("task-restarted", 0) == 0;
  if (restarted)
    require(
        task->note_interrupted_goal({1, mode == "task-restarted-large" ? kMaximumIterations : 3}),
        "interrupted intent rejected");
  else
    require(task->revise_goal({1, mode == "task-large" ? kMaximumIterations : 3}), "goal");
  bool blocked_reported = false, ready_reported = false, held_between = false;
  bool new_requested = false;
  const auto end = now() + 15000;
  while (now() < end) {
    const auto before = client.state();
    if (held_between)
      client.poll(now());
    else
      task->tick(now());
    require(client.state() != ClientState::kClosed, "task owner connection lost");
    if (before == ClientState::kRecovering && client.state() == ClientState::kBlocked &&
        !blocked_reported) {
      require(task->admitted_steps() == 0, "restart admitted before native closure");
      report(1, client);
      blocked_reported = true;
    }
    if (client.state() == ClientState::kBlocked)
      client.recover(now());
    if (restarted && !new_requested) {
      require(task->outcome() == TaskOutcome::kNeedsAttention && !task->operation() &&
                  !task->first_sum() && !task->final_sum() && task->admitted_steps() == 0 &&
                  !task->revise_goal({2, 4}),
              "old task silently resumed");
      if (client.state() == ClientState::kReady && !ready_reported) {
        report(4, client);
        ready_reported = true;
      }
      Report command{};
      const auto count = ::recv(kReportFd, &command, sizeof(command), MSG_DONTWAIT | MSG_TRUNC);
      if (count > 0) {
        require(count == sizeof(command) && ready_reported && command.code == 5 &&
                    command.session == client.session() && command.value == 0,
                "invalid new-goal command");
        // Retire an unknown task, not a recovered success; a distinct request owns the new task.
        task = std::make_unique<FiniteComputeTask>(execution);
        require(task->revise_goal({2, 4}), "explicit fresh goal rejected");
        new_requested = true;
      } else
        require(count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR),
                "caller command channel lost");
    }
    if (mode == "task-between" && !held_between && task->step() == 2) {
      require(task->first_sum() == 5 && task->admitted_steps() == 1,
              "between-step cut was not after first acceptance and cleanup");
      held_between = true;
      report(3, client, 5);
    }
    if (task->outcome() == TaskOutcome::kSucceeded) {
      const auto expected_first = restarted ? 14 : 5;
      const auto expected_final = restarted ? 91 : 55;
      require(task->first_sum() == expected_first && task->final_sum() == expected_final &&
                  task->admitted_steps() == 2 && task->operation() &&
                  execution.receipt(task->operation()->authority)->domain_verdict ==
                      DomainVerdict::kUnassessed,
              "two-step application result mismatch");
      report(6, client, *task->final_sum());
      task->begin_shutdown();
      return 0;
    }
    pause_tick();
  }
  throw std::runtime_error("task demonstration timed out");
}
int host_main(const std::string& mode) {
  if (mode.rfind("task-", 0) == 0)
    return task_host_main(mode);
  Client client(kSessionFd, now());
  const auto end = now() + 10000;
  bool submitted = false;
  bool blocked_reported = false;
  while (now() < end) {
    const auto before = client.state();
    client.poll(now());
    require(client.state() != ClientState::kClosed, "owner connection/recovery failed");
    if (before == ClientState::kRecovering && client.state() == ClientState::kBlocked &&
        !blocked_reported) {
      require(!client.submit(3, now()), "blocked recovery admitted work");
      report(1, client);
      blocked_reported = true;
    }
    if (client.state() == ClientState::kBlocked)
      client.recover(now());
    if (client.state() == ClientState::kReady && !submitted) {
      require(!client.result(), "new Host inherited an old task result");
      require(client.submit(mode == "large" ? kMaximumIterations : 3, now()), "submit failed");
      submitted = true;
    }
    if (submitted && client.result()) {
      const auto receipt = client.receipt();
      require(receipt && receipt->authority_disposition == AuthorityDisposition::kReleased &&
                  receipt->native_outcome == NativeOutcome::kSucceeded &&
                  receipt->domain_verdict == DomainVerdict::kUnassessed,
              "result without released native ownership");
      if (mode != "large")
        require(*client.result() == 5, "independent three-iteration sum");
      report(2, client, *client.result());
      return 0;
    }
    pause_tick();
  }
  throw std::runtime_error("host demonstration timed out");
}
struct Child {
  pid_t pid = -1;
  int reports = -1;
  bool reaped = false;
  ~Child() {
    if (reports >= 0)
      ::close(reports);
    if (pid > 0 && !reaped) {
      ::kill(pid, SIGKILL);
      int status = 0;
      while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
      }
    }
  }
};
// Keep every untransferred endpoint owned across partial setup and spawn failure.
struct HostChannels {
  int session[2] = {-1, -1};
  int reports[2] = {-1, -1};
  int sources[2] = {-1, -1};
  HostChannels() = default;
  HostChannels(const HostChannels&) = delete;
  HostChannels& operator=(const HostChannels&) = delete;
  ~HostChannels() {
    for (int fd : {session[0], session[1], reports[0], reports[1], sources[0], sources[1]}) {
      if (fd >= 0)
        ::close(fd);
    }
  }
};
void launch_host(Owner& owner, const std::string& self, const char* mode, Child& child) {
  HostChannels channels;
  require(::socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, channels.session) == 0,
          "session socketpair failed");
  require(::socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, channels.reports) == 0,
          "report socketpair failed");
  // Neither source may overlap a child destination: file actions execute in order.
  // CLOEXEC closes these temporary copies in the exec'ed Host after dup2 installs
  // its two live endpoints. The parent's copies are closed by HostChannels.
  channels.sources[0] = ::fcntl(channels.session[1], F_DUPFD_CLOEXEC, kReportFd + 1);
  require(channels.sources[0] >= 0, "session source duplication failed");
  channels.sources[1] = ::fcntl(channels.reports[1], F_DUPFD_CLOEXEC, kReportFd + 1);
  require(channels.sources[1] >= 0, "report source duplication failed");
  require(owner.attach(channels.session[0], now()), "owner refused session installation");
  channels.session[0] = -1;  // Owner consumed this endpoint.
  posix_spawn_file_actions_t actions;
  int error = ::posix_spawn_file_actions_init(&actions);
  pid_t spawned = -1;
  if (error == 0) {
    error = ::posix_spawn_file_actions_adddup2(&actions, channels.sources[0], kSessionFd);
    if (!error)
      error = ::posix_spawn_file_actions_adddup2(&actions, channels.sources[1], kReportFd);
    char* args[] = {const_cast<char*>(self.c_str()), const_cast<char*>("--host"),
                    const_cast<char*>(mode), nullptr};
    if (!error)
      error = ::posix_spawn(&spawned, self.c_str(), &actions, nullptr, args, environ);
    ::posix_spawn_file_actions_destroy(&actions);
  }
  require(error == 0, "Host process launch failed");
  child.pid = spawned;
  child.reports = channels.reports[0];
  channels.reports[0] = -1;  // Child now owns the parent's report endpoint.
}
bool read_report(Child& child, Report& r) {
  const auto count = ::recv(child.reports, &r, sizeof(r), MSG_DONTWAIT | MSG_TRUNC);
  if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
    return false;
  if (count == 0)
    return false;
  require(count == sizeof(r), "invalid Host report");
  return true;
}
template <class Check> void drive(Owner& owner, Check check) {
  const auto end = now() + 10000;
  while (now() < end) {
    owner.poll(now());
    require(!owner.faulted(), "owner lost native evidence");
    if (check())
      return;
    pause_tick();
  }
  throw std::runtime_error("owner demonstration timed out");
}
int wait_child(Owner& owner, Child& child) {
  int status = 0;
  drive(owner, [&] {
    const auto found = ::waitpid(child.pid, &status, WNOHANG);
    require(found >= 0, "lost Host wait relationship");
    if (found != child.pid)
      return false;
    child.reaped = true;
    return true;
  });
  return status;
}
int task_demonstration(const std::string& mode, const std::string& self,
                       const std::string& worker) {
  Owner owner(worker, 60000);
  Child first;
  launch_host(owner, self, mode == "task-handoff" ? "task-large" : mode.c_str(), first);
  Report response{};
  if (mode == "task-normal") {
    drive(owner, [&] { return read_report(first, response) && response.code == 6; });
    const auto status = wait_child(owner, first);
    require(WIFEXITED(status) && WEXITSTATUS(status) == 0 && response.value == 55,
            "normal two-step task failed");
    drive(owner, [&] { return !owner.connected() && owner.clear(); });
    require(owner.launches() == 2, "normal task must launch exactly two steps");
    std::cout << "task-normal first=5 final=55 launches=2 cleanup=1\n";
    return 0;
  }
  int old_pid = -1;
  if (mode == "task-between") {
    drive(owner, [&] { return read_report(first, response); });
    require(response.code == 3 && response.value == 5 && owner.clear() && owner.launches() == 1,
            "between-step cut not observed");
  } else {
    drive(owner, [&] {
      const auto o = owner.observation();
      return o.started && !o.exit_status;
    });
    old_pid = owner.observation().child_pid;
    require(::kill(old_pid, SIGSTOP) == 0, "suspend old task worker");
    int stopped = 0;
    pid_t found = 0;
    const auto until = now() + 2000;
    while (!found && now() < until) {
      found = ::waitpid(old_pid, &stopped, WUNTRACED | WNOHANG);
      require(found >= 0, "observe stopped task worker");
      if (!found)
        pause_tick();
    }
    require(found == old_pid && WIFSTOPPED(stopped), "old task worker not held alive");
  }
  require(::kill(first.pid, SIGKILL) == 0, "kill task Host");
  const auto died = wait_child(owner, first);
  require(WIFSIGNALED(died) && WTERMSIG(died) == SIGKILL, "task Host crash not observed");
  drive(owner, [&] { return !owner.connected(); });
  Child replacement;
  launch_host(owner, self, mode == "task-handoff" ? "task-restarted-large" : "task-restarted",
              replacement);
  if (old_pid > 0) {
    drive(owner, [&] { return read_report(replacement, response); });
    require(response.code == 1 && !owner.clear() && owner.launches() == 1,
            "new task Host bypassed old worker");
    require(::kill(old_pid, SIGCONT) == 0, "resume old task worker");
  }
  drive(owner, [&] { return read_report(replacement, response); });
  require(response.code == 4 && owner.clear() && owner.launches() == 1,
          "recovered permission replayed the unknown goal or its dependent step");
  const Report explicit_request{5, response.session, 0};
  require(::send(replacement.reports, &explicit_request, sizeof(explicit_request), MSG_NOSIGNAL) ==
              sizeof(explicit_request),
          "send explicit fresh goal");
  drive(owner, [&] { return read_report(replacement, response) && response.code == 6; });
  const auto status = wait_child(owner, replacement);
  require(WIFEXITED(status) && WEXITSTATUS(status) == 0 && response.value == 91,
          "fresh two-step goal failed");
  drive(owner, [&] { return !owner.connected() && owner.clear(); });
  require(owner.launches() == 3, "old task replayed or fresh goal missing a step");
  std::cout << mode
            << " old_goal=unknown auto_replay=0 new_first=14 new_final=91 launches=3 cleanup=1\n";
  return 0;
}
int demonstration(const std::string& mode, const std::string& self, const std::string& worker) {
  Owner owner(worker, 60000);  // Test-held worker is resumed explicitly; no stop latency claim.
  Child first;
  launch_host(owner, self, mode == "handoff" ? "large" : "three", first);
  if (mode == "normal") {
    Report done{};
    drive(owner, [&] { return read_report(first, done) && done.code == 2; });
    const auto status = wait_child(owner, first);
    require(WIFEXITED(status) && WEXITSTATUS(status) == 0 && done.value == 5, "normal task failed");
    drive(owner, [&] { return !owner.connected() && owner.clear(); });
    require(owner.launches() == 1, "unexpected native launch");
    std::cout << "normal result=5 launches=1 cleanup=1\n";
    return 0;
  }
  drive(owner, [&] {
    const auto o = owner.observation();
    return o.started && !o.exit_status &&
           (mode == "handoff" || o.terminal == detail::ProcessObservation::Terminal::kCompleted);
  });
  const auto old = owner.observation();
  require(::kill(old.child_pid, SIGSTOP) == 0, "could not suspend owned worker");
  int stopped = 0;
  const auto stop_end = now() + 2000;
  pid_t stopped_pid = 0;
  while (!stopped_pid && now() < stop_end) {
    stopped_pid = ::waitpid(old.child_pid, &stopped, WUNTRACED | WNOHANG);
    require(stopped_pid >= 0, "lost worker stop observation");
    if (!stopped_pid)
      pause_tick();
  }
  require(stopped_pid == old.child_pid && WIFSTOPPED(stopped), "worker not held alive at cut");
  require(::kill(first.pid, SIGKILL) == 0, "could not kill owned Host");
  const auto died = wait_child(owner, first);
  require(WIFSIGNALED(died) && WTERMSIG(died) == SIGKILL, "Host crash not observed");
  drive(owner, [&] { return !owner.connected(); });
  require(!owner.clear(), "fenced Host hid unresolved native ownership");
  const auto old_session = owner.session();
  Child replacement;
  launch_host(owner, self, "three", replacement);
  Report response{};
  drive(owner, [&] { return read_report(replacement, response); });
  require(response.code == 1 && response.session > old_session && owner.launches() == 1 &&
              !owner.clear(),
          "replacement did not block on old work");
  require(::kill(old.child_pid, SIGCONT) == 0, "could not resume owned old worker");
  drive(owner, [&] { return read_report(replacement, response) && response.code == 2; });
  const auto status = wait_child(owner, replacement);
  require(WIFEXITED(status) && WEXITSTATUS(status) == 0 && response.value == 5,
          "recovered explicit task failed");
  drive(owner, [&] { return !owner.connected() && owner.clear(); });
  require(owner.launches() == 2 && owner.discarded_results() == 1,
          "old work replayed or old output obligation was lost");
  std::cout
      << mode << " old_session=" << old_session << " new_session=" << response.session
      << " blocked_before_cleanup=1 explicit_new_result=5 launches=2 old_discarded=1 cleanup=1\n";
  return 0;
}
}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc == 3 && std::string(argv[1]) == "--host")
      return host_main(argv[2]);
    require(argc == 3, "usage: recovery_execution normal|handoff|terminal /absolute/worker");
    const std::string mode(argv[1]);
    if (mode == "task-normal" || mode == "task-handoff" || mode == "task-between")
      return task_demonstration(mode, argv[0], argv[2]);
    require(mode == "normal" || mode == "handoff" || mode == "terminal", "invalid mode");
    return demonstration(mode, argv[0], argv[2]);
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
