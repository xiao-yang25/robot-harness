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

void cancellation_before_dispatch() {
  MonotonicTime now = 10;
  SampleExecutionHost host(CompletionMode::kDeferred, [&] { return now; });
  host.initialize();
  const auto prepared = host.prepare({"prepared", {2, 3}});
  require(prepared.status == SampleSubmissionStatus::kPrepared, "request retained before dispatch");
  const auto authority = *prepared.authority;
  auto wrong = authority;
  ++wrong.binding.provider_generation;
  require(host.request_cancel(wrong).status == ControlStatus::kNoActiveOperation,
          "wrong generation cannot cancel prepared work");
  require(host.request_cancel(authority).status == ControlStatus::kApplied, "cancel applied");
  require(host.request_cancel(authority).status == ControlStatus::kAlreadyRequested,
          "repeated cancellation is idempotent");
  require(!host.dispatch_prepared(authority), "discarded request cannot dispatch");
  require(host.worker_submission_count() == 0 && host.native_stop_request_count() == 0 &&
              host.results().empty(),
          "no submission, unnecessary stop or result");
  require(host.submit({"blocked", {4}}).admission_status == AdmissionStatus::kDomainOccupied,
          "revocation alone does not release domain");
  host.process_all_staged_events();
  const auto receipt = *host.receipt(authority);
  require(receipt.dispatch == DispatchStatus::kNotSubmitted &&
              receipt.native_outcome == NativeOutcome::kNotExecuted &&
              receipt.output == OutputDisposition::kNotDelivered &&
              receipt.settlement == SettlementStatus::kSettled &&
              receipt.authority_disposition == AuthorityDisposition::kReleased,
          "discarded request closes with non-submission evidence");
  require(host.request_cancel(authority).status == ControlStatus::kNoActiveOperation,
          "released operation needs no cancellation");
  const auto next = host.submit({"fresh", {4}});
  require(next.status == SampleSubmissionStatus::kSubmitted, "fresh work admitted after closure");
  require(host.request_cancel(authority).status == ControlStatus::kNoActiveOperation &&
              !host.receipt(*next.authority)->cancellation_requested_at,
          "stale cancellation cannot revoke next operation");
  host.complete_deferred();
  host.process_all_staged_events();
  require(host.results().size() == 1 && host.results()[0].sum_of_squares == 16,
          "only fresh request produces an actual result");
}

void accepted_cancellation(CancellationMode mode, StopAcknowledgement acknowledgement,
                           bool fail_naturally = false) {
  MonotonicTime now = 100;
  SampleExecutionHost host(CompletionMode::kDeferred, [&] { return now; });
  host.initialize();
  host.set_cancellation_mode(mode);
  if (fail_naturally) {
    host.fail_next_native_execution();
  }
  const auto authority = *host.submit({"accepted", {2, 3}}).authority;
  ++now;
  require(host.request_cancel(authority).should_request_native_stop, "first cancel requests stop");
  require(!host.request_cancel(authority).should_request_native_stop &&
              host.native_stop_request_count() == 1,
          "native stop attempted at most once");
  auto receipt = *host.receipt(authority);
  require(receipt.stop_acknowledgement == acknowledgement &&
              receipt.native_outcome == NativeOutcome::kPending &&
              receipt.settlement == SettlementStatus::kPending && host.has_pending_native_work(),
          "stop response does not terminate native work or release retained payload");
  require(host.submit({"blocked", {1}}).admission_status == AdmissionStatus::kDomainOccupied,
          "pending native work blocks competing admission");
  ++now;
  require(host.complete_deferred(), "native work eventually completes");
  require(!host.has_pending_native_work(), "adapter releases native payload before event drain");
  while (host.staged_event_count() > 1) {
    host.process_next_staged_event();
  }
  receipt = *host.receipt(authority);
  const auto expected = mode == CancellationMode::kCooperative ? NativeOutcome::kCancelled
                        : fail_naturally                       ? NativeOutcome::kFailed
                                                               : NativeOutcome::kSucceeded;
  require(receipt.native_outcome == expected &&
              receipt.output == OutputDisposition::kNotDelivered &&
              receipt.settlement == SettlementStatus::kPending && host.results().empty(),
          "true terminal fact and output disposal still await cleanup");
  require(host.submit({"still-blocked", {1}}).admission_status == AdmissionStatus::kDomainOccupied,
          "terminal outcome alone cannot release domain");
  host.process_all_staged_events();
  receipt = *host.receipt(authority);
  require(receipt.authority_disposition == AuthorityDisposition::kReleased &&
              receipt.domain_verdict == DomainVerdict::kUnassessed,
          "native termination and settlement close without a task verdict");
}

void staged_result_ordering(bool output_first, bool missing_settlement) {
  MonotonicTime now = 200;
  SampleExecutionHost host(CompletionMode::kDeferred, [&] { return now; });
  host.initialize();
  host.set_settlement_reporting_enabled(!missing_settlement);
  const auto authority = *host.submit({"staged", {3}}).authority;
  host.complete_deferred();
  if (output_first) {
    host.process_next_staged_event();  // Started.
    host.process_next_staged_event();  // Native success.
    host.process_next_staged_event();  // Actual sink acceptance.
  }
  ++now;
  host.request_cancel(authority);
  require(host.submit({"blocked", {4}}).admission_status == AdmissionStatus::kDomainOccupied,
          "cancellation before cleanup retains domain");
  host.shutdown();
  const auto receipt = *host.receipt(authority);
  require(receipt.native_outcome == NativeOutcome::kSucceeded &&
              receipt.output == (output_first ? OutputDisposition::kAccepted
                                              : OutputDisposition::kNotDelivered) &&
              host.results().size() == (output_first ? 1u : 0u),
          "host ordering determines actual output and shutdown preserves it");
  require(receipt.settlement ==
              (missing_settlement ? SettlementStatus::kPending : SettlementStatus::kSettled),
          "shutdown cannot manufacture a missing cleanup observation");
  if (missing_settlement) {
    require(receipt.authority_disposition == AuthorityDisposition::kRevoked,
            "unsettled revoked operation remains occupied after shutdown");
  }
}

void delayed_cleanup_does_not_depend_on_control_delivery(bool late_ack) {
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
  const auto authority = *gate.admit({"square", "domain", "request", "sink", 11}).authority;
  gate.claim_dispatch(authority, 12);
  gate.observe_native({authority, "native", NativeEventKind::kAccepted, {"worker", 13, 1}});
  if (late_ack) {
    gate.request_cancel(authority, 14);
  }
  gate.observe_native(
      {authority, "native", NativeEventKind::kTerminalCancelled, {"worker", 15, 2}});
  gate.observe_output_not_delivered(
      {authority, OutputNonDeliveryReason::kNoOutputProduced, {}, {"adapter", 16, 1}});
  if (late_ack) {
    require(gate.observe_stop_acknowledgement(
                {authority, StopAcknowledgement::kAcknowledged, {"native-stop", 18, 1}}) ==
                EvidenceDisposition::kAccepted,
            "late independent stop response is recorded");
  } else {
    gate.request_cancel(authority, 18);
  }
  require(gate.observe_settlement({authority, "scope", true, {"adapter", 17, 2}}) ==
              EvidenceDisposition::kAccepted,
          "already completed cleanup is not causally ordered after a later control observation");
  require(gate.admit({"square", "domain", "backdated", "sink", 17}).status ==
              AdmissionStatus::kInvalidRequest,
          "new admission still respects latest control observation time");
  require(gate.admit({"square", "domain", "fresh", "sink", 19}).status ==
              AdmissionStatus::kAdmitted,
          "valid delayed cleanup releases domain without a synthetic replacement report");
}

void core_control_evidence_and_conflict() {
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
  const auto authority = *gate.admit({"square", "domain", "request", "sink", 11}).authority;
  gate.claim_dispatch(authority, 12);
  gate.observe_native({authority, "native", NativeEventKind::kAccepted, {"worker", 13, 1}});
  require(gate.request_cancel(authority, 12).status == ControlStatus::kRejected &&
              !gate.receipt(authority)->cancellation_requested_at,
          "backdated control rejected");
  require(gate.observe_stop_acknowledgement(
              {authority, StopAcknowledgement::kAcknowledged, {"native-stop", 14, 1}}) ==
              EvidenceDisposition::kRejected,
          "unsolicited acknowledgement rejected");
  gate.request_cancel(authority, 14);
  for (const auto record : {EvidenceRecord{"wrong", 14, 1}, EvidenceRecord{"native-stop", 13, 1},
                            EvidenceRecord{"native-stop", 14, 0}}) {
    require(gate.observe_stop_acknowledgement({authority, StopAcknowledgement::kAcknowledged,
                                               record}) == EvidenceDisposition::kRejected,
            "wrong source, time or sequence cannot acknowledge");
  }
  const StopAcknowledgementEvidence ack{
      authority, StopAcknowledgement::kAcknowledged, {"native-stop", 14, 1}};
  require(gate.observe_stop_acknowledgement(ack) == EvidenceDisposition::kAccepted &&
              gate.observe_stop_acknowledgement(ack) == EvidenceDisposition::kDuplicate,
          "matching acknowledgement is idempotent");
  require(!gate.can_deliver_result(authority), "revoked authority cannot deliver");
  gate.observe_native(
      {authority, "native", NativeEventKind::kTerminalCancelled, {"worker", 15, 2}});
  require(gate.observe_native(
              {authority, "native", NativeEventKind::kTerminalSucceeded, {"worker", 16, 2}}) ==
              EvidenceDisposition::kRejected,
          "conflicting same-sequence terminal is invalid evidence");
  gate.observe_native(
      {authority, "native", NativeEventKind::kTerminalSucceeded, {"worker", 16, 3}});
  require(gate.receipt(authority)->native_outcome == NativeOutcome::kUnknown &&
              gate.receipt(authority)->authority_disposition ==
                  AuthorityDisposition::kBlockedUnknown,
          "distinct valid conflicting terminal reports remain unknown and blocked");
}
}  // namespace

int main() {
  cancellation_before_dispatch();
  accepted_cancellation(CancellationMode::kCooperative, StopAcknowledgement::kAcknowledged);
  accepted_cancellation(CancellationMode::kIgnore, StopAcknowledgement::kAcknowledged);
  accepted_cancellation(CancellationMode::kRefuse, StopAcknowledgement::kRefused);
  accepted_cancellation(CancellationMode::kUnavailable, StopAcknowledgement::kUnavailable);
  accepted_cancellation(CancellationMode::kNoAcknowledgement, StopAcknowledgement::kPending);
  accepted_cancellation(CancellationMode::kIgnore, StopAcknowledgement::kAcknowledged, true);
  staged_result_ordering(false, false);
  staged_result_ordering(true, false);
  staged_result_ordering(false, true);
  staged_result_ordering(true, true);
  core_control_evidence_and_conflict();
  delayed_cleanup_does_not_depend_on_control_delivery(true);
  delayed_cleanup_does_not_depend_on_control_delivery(false);
  std::cout << "M2b cancellation checks passed\n";
}
