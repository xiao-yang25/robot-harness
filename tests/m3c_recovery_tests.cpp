#include "recovery_client.hpp"
#include "recovery_owner.hpp"
#include "recovery_protocol.hpp"
#include "recovery_task_execution.hpp"
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace {
bool fail_next_allocation = false;
}
// Isolated single-threaded injection at descriptor transfer and activation only.
void* operator new(std::size_t size) {
  if (fail_next_allocation) {
    fail_next_allocation = false;
    throw std::bad_alloc();
  }
  if (void* p = std::malloc(size == 0 ? 1 : size))
    return p;
  throw std::bad_alloc();
}
void operator delete(void* p) noexcept {
  std::free(p);
}
void operator delete(void* p, std::size_t) noexcept {
  std::free(p);
}

namespace {
using namespace robot_harness::detail::recovery;
void require(bool value, const char* why) {
  if (!value)
    throw std::runtime_error(why);
}
struct Pair {
  int owner = -1, peer = -1;
  Pair() {
    int fds[2];
    require(::socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, fds) == 0, "socketpair");
    owner = fds[0];
    peer = fds[1];
  }
  ~Pair() {
    if (owner >= 0)
      ::close(owner);
    if (peer >= 0)
      ::close(peer);
  }
  int take_owner() {
    const int fd = owner;
    owner = -1;
    return fd;
  }
  void close_peer() {
    ::close(peer);
    peer = -1;
  }
};
Message receive(int fd) {
  Message m;
  require(receive_message(fd, m) == Io::kDone, "missing expected message");
  return m;
}
void send(int fd, Message m) {
  require(send_message(fd, m) == Io::kDone, "send failed");
}
void client_boundary(const std::string& scenario) {
  Pair channel;
  Client client(channel.take_owner(), 0);
  Message hello;
  hello.session = 2;
  send(channel.peer, hello);
  client.poll(0);
  require(client.state() == ClientState::kBlocked && !client.submit(3, 0), "initial recovery gate");
  require(client.recover(1), "query");
  client.poll(1);
  const auto query = receive(channel.peer);
  require(query.kind == Kind::kQuery, "query kind");
  Message snapshot;
  snapshot.kind = Kind::kSnapshot;
  snapshot.session = 2;
  snapshot.request = query.request;
  snapshot.flags = kClear;
  if (scenario == "wrong-session")
    snapshot.session = 1;
  if (scenario == "conflict")
    snapshot.flags = kClear | 2;
  if (scenario == "wrong-request")
    ++snapshot.request;
  send(channel.peer, snapshot);
  client.poll(2);
  if (scenario == "wrong-session" || scenario == "conflict" || scenario == "wrong-request") {
    require(client.state() == ClientState::kClosed && !client.submit(3, 2),
            "invalid snapshot granted");
    return;
  }
  require(client.state() == ClientState::kRecovering && !client.submit(3, 2),
          "snapshot/partial recovery granted permission");
  client.poll(3);
  const auto activation = receive(channel.peer);
  require(activation.kind == Kind::kActivate && activation.value == query.request,
          "token mismatch");
  if (scenario == "interrupted") {
    channel.close_peer();
    client.poll(4);
  } else {
    Message reply;
    reply.kind = Kind::kActivated;
    reply.session = 2;
    reply.request = activation.request;
    reply.value = query.request;
    send(channel.peer, reply);
    if (scenario == "allocation") {
      bool failed = false;
      fail_next_allocation = true;
      try {
        client.poll(4);
      } catch (const std::bad_alloc&) {
        failed = true;
      }
      fail_next_allocation = false;
      require(failed, "activation allocation was not injected");
    } else
      client.poll(scenario == "expired" ? 1001 : 4);
    if (scenario == "owner-loss" || scenario == "old-output") {
      require(client.state() == ClientState::kReady, "valid owner activation did not commit");
      if (scenario == "owner-loss")
        channel.close_peer();
      else {
        Message old;
        old.kind = Kind::kResult;
        old.session = 1;
        old.flags = kSucceeded;
        old.request = activation.request;
        old.operation = 1;
        old.amount = 3;
        old.value = 5;
        send(channel.peer, old);
      }
      client.poll(5);
    }
  }
  require(client.state() == ClientState::kClosed && !client.result() && !client.submit(3, 1002),
          "failed recovery reopened permission");
}
Message exchange(Owner& owner, int peer, Message m, std::uint64_t time) {
  send(peer, m);
  owner.poll(time);
  owner.poll(time);
  return receive(peer);
}
void owner_boundaries(const std::string& worker) {
  Owner owner(worker);
  for (int scenario = 0; scenario < 4; ++scenario) {
    Pair channel;
    require(owner.attach(channel.owner, 10 + scenario * 2000), "attach");
    channel.owner = -1;
    const auto t = std::uint64_t(10 + scenario * 2000);
    owner.poll(t);
    const auto hello = receive(channel.peer);
    const auto epoch = hello.session;
    require(epoch == std::uint64_t(scenario + 2), "session identity reused");
    Message submit;
    submit.kind = Kind::kSubmit;
    submit.session = epoch;
    submit.request = 3;
    submit.operation = 1;
    submit.amount = 3;
    if (scenario != 0) {
      Message q;
      q.kind = Kind::kQuery;
      q.session = epoch;
      q.request = 1;
      require(exchange(owner, channel.peer, q, t).flags == kClear, "initial empty scope");
      Message activate;
      activate.kind = Kind::kActivate;
      activate.session = epoch;
      activate.request = 2;
      activate.value = scenario == 1 ? 99 : 1;
      if (scenario == 1 || scenario == 2) {
        send(channel.peer, activate);
        owner.poll(t + (scenario == 2 ? 1000 : 1));
      } else {
        require(exchange(owner, channel.peer, activate, t + 1).kind == Kind::kActivated,
                "activate");
        submit.session = epoch - 1;
        send(channel.peer, submit);
        owner.poll(t + 2);
      }
    } else {
      send(channel.peer, submit);
      owner.poll(t + 1);
    }
    require(!owner.connected() && owner.launches() == 0 && owner.clear(),
            "unactivated/expired/wrong-epoch command launched native work");
  }
}
void task_channel_loss() {
  using namespace robot_harness;
  using namespace robot_harness::examples;
  Pair channel;
  auto time = ComputeExecutionHost::now();
  Client client(channel.take_owner(), time);
  Message hello;
  hello.session = 2;
  send(channel.peer, hello);
  client.poll(time);
  require(client.recover(time), "task recovery query");
  client.poll(time);
  auto query = receive(channel.peer);
  Message snapshot;
  snapshot.kind = Kind::kSnapshot;
  snapshot.session = 2;
  snapshot.request = query.request;
  snapshot.flags = kClear;
  send(channel.peer, snapshot);
  client.poll(time);
  client.poll(time);
  auto activation = receive(channel.peer);
  Message activated;
  activated.kind = Kind::kActivated;
  activated.session = 2;
  activated.request = activation.request;
  activated.value = query.request;
  send(channel.peer, activated);
  client.poll(time);
  require(client.state() == ClientState::kReady, "task client not activated");
  RecoveryTaskExecution execution(client);
  FiniteComputeTask task(execution);
  require(task.revise_goal({1, 3}), "task goal");
  task.tick(ComputeExecutionHost::now());
  require(task.operation() && task.admitted_steps() == 1, "task operation not admitted");
  auto wrong = task.operation()->authority;
  ++wrong.admitted_at;
  require(!execution.receipt(wrong), "bridge accepted different authority");
  task.tick(ComputeExecutionHost::now());
  require(receive(channel.peer).kind == Kind::kSubmit, "task submission missing");
  channel.close_peer();
  task.tick(ComputeExecutionHost::now());
  require(task.outcome() == TaskOutcome::kNeedsAttention && task.admitted_steps() == 1 &&
              !task.first_sum() && !task.final_sum() && !task.revise_goal({2, 4}),
          "owner channel loss resumed or completed goal");
  task.tick(ComputeExecutionHost::now());
  require(task.admitted_steps() == 1 && task.outcome() == TaskOutcome::kNeedsAttention,
          "continued observation retried unknown goal");
}
void outbound_backpressure() {
  Pair p;
  int small = 1024;
  require(::setsockopt(p.owner, SOL_SOCKET, SO_SNDBUF, &small, sizeof(small)) == 0, "send buffer");
  Message filler;
  int count = 0;
  while (send_message(p.owner, filler) == Io::kDone && ++count < 10000) {
  }
  require(count > 0 && count < 10000, "could not create bounded backpressure");
  Client client(p.take_owner(), 0);
  Message hello;
  hello.session = 2;
  send(p.peer, hello);
  client.poll(0);
  require(client.recover(1), "backpressure query");
  client.poll(1);
  require(client.state() == ClientState::kRecovering, "unsent query should remain pending");
  client.poll(1001);
  require(client.state() == ClientState::kClosed && !client.submit(3, 1001),
          "send timeout reopened");
}
}  // namespace
int main(int argc, char** argv) {
  try {
    require(argc == 3, "usage: recovery_tests client|owner WORKER");
    if (std::string(argv[1]) == "client") {
      for (const auto* scenario : {"wrong-session", "conflict", "wrong-request", "interrupted",
                                   "expired", "owner-loss", "old-output", "allocation"})
        client_boundary(scenario);
      outbound_backpressure();
      Pair channel;
      const int transferred = channel.take_owner();
      bool failed = false;
      fail_next_allocation = true;
      try {
        Client client(transferred, 0);
      } catch (const std::bad_alloc&) {
        failed = true;
      }
      fail_next_allocation = false;
      require(failed && ::fcntl(transferred, F_GETFD) == -1 && errno == EBADF,
              "constructor failure leaked transferred descriptor");
    } else if (std::string(argv[1]) == "task") {
      task_channel_loss();
    } else {
      require(std::string(argv[1]) == "owner", "mode");
      owner_boundaries(argv[2]);
    }
    std::cout << "recovery boundary checks passed\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
