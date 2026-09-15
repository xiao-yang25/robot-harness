#include "robot_harness/sample_execution.hpp"

#include <cstdlib>
#include <iostream>

namespace {
using namespace robot_harness;
void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
  }
}

void expired_input_and_prepared_boundary() {
  MonotonicTime now = 10;
  SampleExecutionHost host(CompletionMode::kDeferred, [&] { return now; });
  host.initialize();
  for (const auto deadline : {MonotonicTime{9}, MonotonicTime{10}}) {
    const auto submission = host.submit({"expired", {2}, deadline});
    require(submission.admission_status == AdmissionStatus::kExpired && !submission.authority &&
                host.worker_submission_count() == 0 && host.staged_event_count() == 0,
            "expired admission allocates no operation or native work");
  }
  const auto authority = *host.prepare({"prepared", {3}, 20}).authority;
  now = 20;
  require(!host.dispatch_prepared(authority) && host.worker_submission_count() == 0 &&
              host.native_stop_request_count() == 0,
          "equality denies actual native submission");
  host.process_all_staged_events();
  const auto receipt = *host.receipt(authority);
  require(receipt.expiry_observed_at == 20 && !receipt.cancellation_requested_at &&
              receipt.native_outcome == NativeOutcome::kNotExecuted &&
              receipt.authority_disposition == AuthorityDisposition::kReleased,
          "expired prepared request has truthful non-submission closure");
}

void idle_expiry_and_cancel(bool cancel_first) {
  MonotonicTime now = 10;
  SampleExecutionHost host(CompletionMode::kDeferred, [&] { return now; });
  host.initialize();
  host.set_cancellation_mode(CancellationMode::kIgnore);
  const auto authority = *host.submit({"running", {2, 3}, 20}).authority;
  if (cancel_first) {
    now = 15;
    host.request_cancel(authority);
  }
  now = 20;
  require(!host.receipt(authority)->expiry_observed_at, "receipt reads do not evaluate a timer");
  require(host.poll().status == ControlStatus::kApplied,
          "explicit idle poll records equality expiry");
  require(host.poll().status == ControlStatus::kAlreadyRequested, "repeated expiry is idempotent");
  host.request_cancel(authority);
  auto receipt = *host.receipt(authority);
  require(receipt.expiry_observed_at == 20 && receipt.cancellation_requested_at &&
              host.native_stop_request_count() == 1 && host.has_pending_native_work() &&
              receipt.native_outcome == NativeOutcome::kPending,
          "both reasons survive but expiry does not terminate work or repeat stop");
  require(host.submit({"blocked", {4}}).admission_status == AdmissionStatus::kDomainOccupied,
          "expired active work still blocks admission");
  now = 25;
  host.complete_deferred();
  host.process_all_staged_events();
  receipt = *host.receipt(authority);
  require(receipt.native_outcome == NativeOutcome::kSucceeded && host.results().empty() &&
              receipt.output_non_delivery_reason == OutputNonDeliveryReason::kAuthorityRevoked &&
              receipt.settlement == SettlementStatus::kSettled,
          "late natural success is preserved and output discarded before settlement");
  require(host.poll().status == ControlStatus::kNoActiveOperation,
          "released operation is not expired again");
}

void delivery_and_settlement_ordering(bool deliver_before, bool settle_before) {
  MonotonicTime now = 10;
  SampleExecutionHost host(CompletionMode::kDeferred, [&] { return now; });
  host.initialize();
  const auto authority = *host.submit({"staged", {3}, 20}).authority;
  now = 15;
  host.complete_deferred();
  host.process_next_staged_event();
  host.process_next_staged_event();
  if (deliver_before) {
    host.process_next_staged_event();
  }
  if (settle_before) {
    host.process_next_staged_event();
  }
  now = 20;
  host.process_all_staged_events();
  const auto receipt = *host.receipt(authority);
  require(receipt.native_outcome == NativeOutcome::kSucceeded &&
              host.results().size() == (deliver_before ? 1u : 0u),
          "actual delivery ordering at equality determines sink contents");
  require(receipt.expiry_observed_at.has_value() != settle_before &&
              receipt.settlement == SettlementStatus::kSettled,
          "expiry before pending settlement is retained; prior release is not expired");
  require(host.native_stop_request_count() == 0,
          "known native termination requires no unnecessary stop during result cleanup");
}

void admission_observes_existing_expiry() {
  MonotonicTime now = 10;
  SampleExecutionHost host(CompletionMode::kDeferred, [&] { return now; });
  host.initialize();
  const auto authority = *host.submit({"old", {2}, 20}).authority;
  now = 20;
  require(host.submit({"new", {3}}).admission_status == AdmissionStatus::kDomainOccupied &&
              host.receipt(authority)->expiry_observed_at == 20 &&
              host.native_stop_request_count() == 1,
          "new admission observes old operation expiry without freeing its slot");
  host.shutdown();
  require(host.results().empty() &&
              host.receipt(authority)->native_outcome == NativeOutcome::kCancelled,
          "cooperative expired operation drains without delivery during shutdown");
}

void boundary_clock_crossing(bool dispatch_boundary, bool settlement_boundary) {
  MonotonicTime now = 10;
  int reads_until_deadline = 0;
  SampleExecutionHost host(CompletionMode::kDeferred, [&] {
    if (reads_until_deadline > 0 && --reads_until_deadline == 0) {
      now = 20;
    }
    return now;
  });
  host.initialize();
  const auto authority = *host.prepare({"boundary", {3}, 20}).authority;
  if (!dispatch_boundary) {
    host.dispatch_prepared(authority);
    host.complete_deferred();
    host.process_next_staged_event();
    host.process_next_staged_event();
    if (settlement_boundary) {
      host.process_next_staged_event();
    }
  }
  now = 19;
  reads_until_deadline = 2;  // Entry poll precedes the actual boundary's time observation.
  if (dispatch_boundary) {
    require(!host.dispatch_prepared(authority) && host.worker_submission_count() == 0,
            "deadline crossed after entry poll still denies actual submission");
  } else {
    host.process_next_staged_event();
    require(host.results().size() == (settlement_boundary ? 1u : 0u),
            "sink checks its actual acceptance boundary after event-entry poll");
  }
  host.process_all_staged_events();
  require(host.receipt(authority)->expiry_observed_at == 20 &&
              host.receipt(authority)->settlement == SettlementStatus::kSettled,
          "boundary expiry is retained through truthful cleanup");
}

void core_expiry_identity_and_backstops() {
  AuthorityGateConfig config{{"provider", "domain", 1, 1},
                             "square",
                             "binding",
                             "worker",
                             "sink",
                             "worker",
                             "adapter",
                             "scope",
                             10};
  AuthorityGate gate(config);
  gate.observe_startup(
      {StartupEvidenceKind::kBindingIdleAndSettled, config.binding, {"binding", 10, 1}, true});
  gate.observe_startup(
      {StartupEvidenceKind::kWorkerReady, config.binding, {"worker", 10, 1}, true});
  gate.observe_startup(
      {StartupEvidenceKind::kResultSinkReady, config.binding, {"sink", 10, 1}, true});
  const auto authority = *gate.admit({"square", "domain", "request", "sink", 11, 20}).authority;
  auto wrong = authority;
  ++wrong.operation_id;
  require(gate.observe_time(wrong, 20).status == ControlStatus::kNoActiveOperation &&
              !gate.receipt(authority)->expiry_observed_at,
          "unknown operation cannot expire current");
  require(gate.observe_time(authority, 10).status == ControlStatus::kRejected,
          "observation before admission rejected");
  require(gate.observe_time(authority, 19).status == ControlStatus::kNotDue,
          "future deadline does not revoke permission");
  require(!gate.claim_dispatch(authority, 20),
          "Core dispatch backstop denies equality without poll");
  // No dispatch happened; test another valid boundary time in this passive Core API.
  require(gate.claim_dispatch(authority, 19), "pre-deadline dispatch can claim authority");
  gate.observe_native({authority, "native", NativeEventKind::kAccepted, {"worker", 19, 1}});
  gate.observe_native(
      {authority, "native", NativeEventKind::kTerminalSucceeded, {"worker", 19, 2}});
  require(gate.observe_output_accepted({authority, "result", {"sink", 20, 1}}) ==
              EvidenceDisposition::kRejected,
          "Core rejects late delivery evidence even without poll");
  require(gate.observe_time(authority, 20).status == ControlStatus::kApplied,
          "host time observation records expiry");
  require(gate.observe_time(authority, 19).status == ControlStatus::kRejected,
          "cannot backdate observations after expiry");
  require(gate.observe_output_not_delivered(
              {authority, OutputNonDeliveryReason::kAuthorityRevoked, "result", {"sink", 19, 1}}) ==
              EvidenceDisposition::kRejected,
          "revoked disposal cannot precede revocation");
  require(gate.observe_output_not_delivered(
              {authority, OutputNonDeliveryReason::kAuthorityRevoked, "result", {"sink", 20, 1}}) ==
              EvidenceDisposition::kAccepted,
          "equality permits truthful disposal, never late acceptance");
  require(gate.observe_settlement({authority, "scope", true, {"adapter", 20, 1}}) ==
              EvidenceDisposition::kAccepted,
          "expired operation can close with actual evidence");
}

void synchronous_crossing_and_no_deadline() {
  MonotonicTime now = 0;
  SampleExecutionHost host(CompletionMode::kSynchronous, [&] { return now++; });
  host.initialize();
  // The callback clock advances across the deadline while synchronous native work runs.
  const auto authority = *host.submit({"crossing", {2, 3}, now + 5}).authority;
  const auto receipt = *host.receipt(authority);
  require(host.worker_execution_count() == 1 &&
              receipt.native_outcome == NativeOutcome::kSucceeded && receipt.expiry_observed_at &&
              host.results().empty() && receipt.settlement == SettlementStatus::kSettled,
          "synchronous computation is not preempted but cannot deliver across deadline");
  const auto next = *host.submit({"no-deadline", {4}}).authority;
  require(!host.receipt(next)->deadline && !host.receipt(next)->expiry_observed_at &&
              host.results().size() == 1 && host.results()[0].sum_of_squares == 16,
          "no deadline preserves ordinary execution");
}
}  // namespace

int main() {
  expired_input_and_prepared_boundary();
  idle_expiry_and_cancel(false);
  idle_expiry_and_cancel(true);
  delivery_and_settlement_ordering(false, false);
  delivery_and_settlement_ordering(true, false);
  delivery_and_settlement_ordering(true, true);
  admission_observes_existing_expiry();
  synchronous_crossing_and_no_deadline();
  boundary_clock_crossing(true, false);
  boundary_clock_crossing(false, false);
  boundary_clock_crossing(false, true);
  core_expiry_identity_and_backstops();
  std::cout << "M2c deadline checks passed\n";
}
