#include "recovery_client.hpp"
#include "recovery_protocol.hpp"
#include <fcntl.h>
#include <limits>
#include <stdexcept>
#include <utility>

namespace robot_harness::detail::recovery {
namespace {
constexpr const char* kOwner = "recovery-owner";
constexpr const char* kSink = "recovery-sink";
constexpr const char* kScope = "supervised-compute-domain";
constexpr const char* kCapability = "compute.periodic-square-sum";
AuthorityGateConfig gate_config(std::uint64_t prior_session, MonotonicTime now) {
  AuthorityGateConfig c;
  c.binding = {kOwner, kScope, 1, prior_session};
  c.capability_id = kCapability;
  c.binding_observer = kOwner;
  c.worker_observer = kOwner;
  c.result_sink_observer = kSink;
  c.native_evidence_source = kOwner;
  c.settlement_evidence_source = kOwner;
  c.settlement_scope = "owned-child-output-and-channels";
  c.readiness_kind = StartupEvidenceKind::kAdapterReady;
  c.started_at = now;
  return c;
}
std::uint64_t expected_sum(std::uint64_t count) {
  const auto r = count % 1000;
  return count / 1000 * 332833500 + (r == 0 ? 0 : (r - 1) * r * (2 * r - 1) / 6);
}
bool empty_payload(const Message& m) {
  return m.operation == 0 && m.amount == 0 && m.value == 0 && m.flags == 0;
}
}  // namespace

struct Client::Impl {
  enum class Exchange { kHello, kIdle, kSnapshot, kActivation, kAcceptance, kResult };
  int fd;
  ClientState state = ClientState::kConnecting;
  Exchange exchange = Exchange::kHello;
  MonotonicTime last_time;
  MonotonicTime issued_at;
  MonotonicTime queued_at = 0;
  MonotonicTime query_at = 0;
  std::uint64_t epoch = 0;
  std::uint64_t nonce = 0;
  std::uint64_t token = 0;
  std::uint64_t sequence = 0;
  std::uint64_t iterations = 0;
  std::unique_ptr<AuthorityGate> gate;
  std::optional<BindingIdentity> candidate;
  std::optional<OperationAuthority> operation;
  std::optional<Message> pending;
  std::optional<std::uint64_t> value;

  Impl(int supplied, MonotonicTime now) : fd(supplied), last_time(now), issued_at(now) {
    if (fd < 0 || ::fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) {
      throw std::invalid_argument("recovery client requires an owned local session descriptor");
    }
  }
  ~Impl() {
    close();
  }
  void close() noexcept {
    state = ClientState::kClosed;
    if (gate)
      gate->close_admission();
    pending.reset();
    if (fd >= 0)
      ::close(std::exchange(fd, -1));
  }
  bool time(MonotonicTime now) {
    if (state == ClientState::kClosed)
      return false;
    if (now < last_time) {
      close();
      return false;
    }
    last_time = now;
    return true;
  }
  bool fresh(MonotonicTime start, MonotonicTime now) const {
    return now >= start && now - start < kEvidenceLifetimeMs;
  }
  bool queue(Message message, MonotonicTime now) {
    if (pending || nonce == std::numeric_limits<std::uint64_t>::max()) {
      close();
      return false;
    }
    message.session = epoch;
    message.request = ++nonce;
    pending = message;
    queued_at = issued_at = now;
    return true;
  }
  bool evidence(EvidenceDisposition disposition) {
    if (disposition != EvidenceDisposition::kAccepted) {
      close();
      return false;
    }
    return true;
  }
  EvidenceRecord record(const char* source, MonotonicTime now) {
    return {source, now, ++sequence};
  }
  void handle(const Message& m, MonotonicTime now) {
    if (exchange == Exchange::kHello) {
      if (m.kind != Kind::kHello || m.session < 2 || m.request || !empty_payload(m)) {
        close();
        return;
      }
      epoch = m.session;
      auto config = gate_config(epoch - 1, now);
      gate = std::make_unique<AuthorityGate>(config);
      if (gate->withdraw_binding(config.binding, now).status != BindingControlStatus::kApplied) {
        close();
        return;
      }
      state = ClientState::kBlocked;
      exchange = Exchange::kIdle;
      return;
    }
    if (m.session != epoch || m.request != nonce) {
      close();
      return;
    }
    if (exchange == Exchange::kSnapshot) {
      if (m.kind != Kind::kSnapshot || m.operation || m.amount || m.value ||
          (m.flags != 0 && m.flags != kClear) || !fresh(query_at, now)) {
        close();
        return;
      }
      if (m.flags == 0) {
        state = ClientState::kBlocked;
        exchange = Exchange::kIdle;
        return;
      }
      if (query_at > std::numeric_limits<MonotonicTime>::max() - kEvidenceLifetimeMs) {
        close();
        return;
      }
      const auto expires = query_at + kEvidenceLifetimeMs;
      const auto old = gate->binding_status(now).active;
      const BindingReadinessEvidence closure{StartupEvidenceKind::kBindingIdleAndSettled, old,
                                             record(kOwner, now), true, expires};
      const auto decision = gate->begin_rebind(old, kOwner, closure, now);
      if (decision.status != BindingControlStatus::kApplied || !decision.candidate ||
          decision.candidate->provider_generation != epoch) {
        close();
        return;
      }
      candidate = decision.candidate;
      token = m.request;
      Message activate;
      activate.kind = Kind::kActivate;
      activate.value = token;
      if (queue(activate, now))
        exchange = Exchange::kActivation;
      return;
    }
    if (exchange == Exchange::kActivation) {
      if (m.kind != Kind::kActivated || m.value != token || m.operation || m.amount || m.flags ||
          !fresh(query_at, now) || !candidate) {
        close();
        return;
      }
      // Activated re-observes owner prerequisites after candidate preparation.
      const auto expires = query_at + kEvidenceLifetimeMs;
      for (auto kind :
           {StartupEvidenceKind::kBindingIdleAndSettled, StartupEvidenceKind::kAdapterReady,
            StartupEvidenceKind::kResultSinkReady}) {
        if (!evidence(gate->observe_rebind_readiness(
                {kind, *candidate,
                 record(kind == StartupEvidenceKind::kResultSinkReady ? kSink : kOwner, now), true,
                 expires})))
          return;
      }
      if (gate->commit_rebind(*candidate, now).status != BindingControlStatus::kApplied) {
        close();
        return;
      }
      state = ClientState::kReady;
      exchange = Exchange::kIdle;
      return;
    }
    if (!operation || m.operation != operation->operation_id || m.amount != iterations) {
      close();
      return;
    }
    const auto native_id =
        "session-" + std::to_string(epoch) + "-op-" + std::to_string(m.operation);
    if (exchange == Exchange::kAcceptance && m.kind == Kind::kAccepted && !m.flags && !m.value) {
      if (evidence(gate->observe_native(
              {*operation, native_id, NativeEventKind::kAccepted, record(kOwner, now)})))
        exchange = Exchange::kResult;
      return;
    }
    if (m.kind != Kind::kResult ||
        (exchange != Exchange::kResult &&
         !(exchange == Exchange::kAcceptance && m.flags == kNotLaunched)) ||
        m.flags < kSucceeded || m.flags > kNotLaunched ||
        (exchange == Exchange::kResult && m.flags == kNotLaunched) ||
        (m.flags == kSucceeded ? m.value != expected_sum(iterations) : m.value != 0)) {
      close();
      return;
    }
    const auto outcome = m.flags == kSucceeded     ? NativeEventKind::kTerminalSucceeded
                         : m.flags == kCancelled   ? NativeEventKind::kTerminalCancelled
                         : m.flags == kNotLaunched ? NativeEventKind::kRejected
                                                   : NativeEventKind::kTerminalFailed;
    if (!evidence(gate->observe_native({*operation, native_id, outcome, record(kOwner, now)})))
      return;
    if (m.flags == kSucceeded) {
      if (!gate->can_deliver_result(*operation)) {
        close();
        return;
      }
      if (!evidence(gate->observe_output_accepted({*operation, native_id, record(kSink, now)})))
        return;
    } else if (!evidence(gate->observe_output_not_delivered(
                   {*operation, OutputNonDeliveryReason::kNoOutputProduced, "",
                    record(kOwner, now)})))
      return;
    if (!evidence(gate->observe_settlement(
            {*operation, "owned-child-output-and-channels", true, record(kOwner, now)})))
      return;
    if (m.flags == kSucceeded)
      value = m.value;
    exchange = Exchange::kIdle;
  }
};

Client::Client(int fd, MonotonicTime now) try : impl_(std::make_unique<Impl>(fd, now)) {
} catch (...) {
  // The descriptor transfers at entry, including allocation/initialization failure.
  if (fd >= 0)
    ::close(fd);
  throw;
}
Client::~Client() = default;
void Client::close() noexcept {
  impl_->close();
}
ClientState Client::state() const noexcept {
  return impl_->state;
}
std::uint64_t Client::session() const noexcept {
  return impl_->epoch;
}
std::optional<std::uint64_t> Client::result() const noexcept {
  return impl_->value;
}
std::optional<OperationReceipt> Client::receipt() const {
  return impl_->operation ? impl_->gate->receipt(*impl_->operation) : std::nullopt;
}
void Client::poll(MonotonicTime now) try {
  auto& s = *impl_;
  if (!s.time(now))
    return;
  if (s.pending) {
    if (!s.fresh(s.queued_at, now)) {
      s.close();
      return;
    }
    const auto sent = send_message(s.fd, *s.pending);
    if (sent == Io::kClosed) {
      s.close();
      return;
    }
    if (sent == Io::kAgain)
      return;
    s.pending.reset();
  }
  if (s.exchange != Impl::Exchange::kIdle && s.exchange != Impl::Exchange::kResult &&
      !s.fresh(s.issued_at, now)) {
    s.close();
    return;
  }
  Message incoming;
  const auto received = receive_message(s.fd, incoming);
  if (received == Io::kClosed)
    s.close();
  else if (received == Io::kDone)
    s.handle(incoming, now);
} catch (...) {
  impl_->close();
  throw;
}
bool Client::recover(MonotonicTime now) {
  auto& s = *impl_;
  if (!s.time(now) || s.state != ClientState::kBlocked || s.exchange != Impl::Exchange::kIdle)
    return false;
  Message query;
  query.kind = Kind::kQuery;
  if (!s.queue(query, now))
    return false;
  s.query_at = now;
  s.exchange = Impl::Exchange::kSnapshot;
  s.state = ClientState::kRecovering;
  return true;
}
bool Client::submit(std::uint64_t iterations, MonotonicTime now) try {
  auto& s = *impl_;
  if (!s.time(now) || s.state != ClientState::kReady || s.exchange != Impl::Exchange::kIdle ||
      iterations == 0 || iterations > kMaximumIterations)
    return false;
  OperationRequest request;
  request.capability_id = kCapability;
  request.effect_domain = kScope;
  request.opaque_request_reference = "prototype-" + std::to_string(s.nonce + 1);
  request.target_reference = kSink;
  request.requested_at = now;
  auto admitted = s.gate->admit(request);
  if (admitted.status != AdmissionStatus::kAdmitted || !admitted.authority)
    return false;
  s.operation = admitted.authority;
  s.iterations = iterations;
  s.value.reset();
  if (!s.gate->claim_dispatch(*s.operation, now)) {
    s.close();
    return false;
  }
  Message submit;
  submit.kind = Kind::kSubmit;
  submit.operation = s.operation->operation_id;
  submit.amount = iterations;
  if (!s.queue(submit, now))
    return false;
  s.exchange = Impl::Exchange::kAcceptance;
  return true;
} catch (...) {
  impl_->close();
  throw;
}
}  // namespace robot_harness::detail::recovery
