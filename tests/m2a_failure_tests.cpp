#include "robot_harness/sample_execution.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
  }
}

void unavailable_before_dispatch_closes_without_submission(bool worker_unavailable) {
  robot_harness::MonotonicTime time = worker_unavailable ? 500 : 600;
  robot_harness::SampleExecutionHost host(robot_harness::CompletionMode::kSynchronous,
                                          [&time] { return time++; });
  require(host.initialize().state == robot_harness::InitializationState::kReady,
          "fixture initializes before availability changes");
  if (worker_unavailable) {
    host.set_worker_ready(false);
  } else {
    host.set_result_sink_ready(false);
  }

  const auto refused = host.submit({"refused-a", {2}});
  require(refused.status == robot_harness::SampleSubmissionStatus::kDispatchBlocked &&
              refused.authority.has_value(),
          "unavailable adapter closes an admitted operation without dispatch");
  require(host.worker_submission_count() == 0 && host.worker_execution_count() == 0 &&
              host.results().empty(),
          "unavailable path has no native attempt, execution or result");
  const auto receipt = host.receipt(*refused.authority);
  require(receipt.has_value() &&
              receipt->dispatch == robot_harness::DispatchStatus::kNotSubmitted &&
              receipt->native_outcome == robot_harness::NativeOutcome::kNotExecuted &&
              receipt->output == robot_harness::OutputDisposition::kNotDelivered &&
              receipt->output_non_delivery_reason ==
                  robot_harness::OutputNonDeliveryReason::kNoOutputProduced &&
              receipt->settlement == robot_harness::SettlementStatus::kSettled &&
              receipt->authority_disposition == robot_harness::AuthorityDisposition::kReleased,
          "correlated non-submission and cleanup fully close the refused operation");

  host.set_worker_ready(true);
  host.set_result_sink_ready(true);
  const auto recovered = host.submit({"recovered-b", {3}});
  require(recovered.status == robot_harness::SampleSubmissionStatus::kSubmitted &&
              host.worker_submission_count() == 1 && host.worker_execution_count() == 1 &&
              host.results().size() == 1 && host.results()[0].sum_of_squares == 9,
          "restored adapter executes only the caller-selected fresh operation");
}

void host_preserves_adapter_sequences_with_a_constant_clock() {
  robot_harness::SampleExecutionHost host(robot_harness::CompletionMode::kSynchronous,
                                          [] { return robot_harness::MonotonicTime{100}; });
  require(host.initialize().state == robot_harness::InitializationState::kReady,
          "equal nondecreasing clock values are supported");
  host.set_worker_ready(false);
  const auto refused = host.submit({"not-submitted", {2}});
  require(refused.authority.has_value(), "refused operation retains a receipt");
  auto receipt = host.receipt(*refused.authority);
  require(receipt.has_value() && receipt->non_submission_evidence.has_value() &&
              receipt->output_non_delivery_evidence.has_value() &&
              receipt->settlement_evidence.has_value(),
          "host records every non-submission closure observation");
  require(receipt->non_submission_evidence->sequence == 1 &&
              receipt->output_non_delivery_evidence->sequence == 2 &&
              receipt->settlement_evidence->sequence == 3,
          "host preserves the adapter's non-submission closure sequence");
  host.set_worker_ready(true);
  host.reject_next_native_submission();
  const auto rejected = host.submit({"rejected", {3}});
  require(rejected.authority.has_value(), "next operation is admitted after ordered closure");
  receipt = host.receipt(*rejected.authority);
  require(receipt->output_non_delivery_evidence.has_value() &&
              receipt->settlement_evidence.has_value() &&
              receipt->output_non_delivery_evidence->sequence == 1 &&
              receipt->settlement_evidence->sequence == 2,
          "rejection uses its own ordered adapter sequence");
  host.fail_next_native_execution();
  const auto failed = host.submit({"failed", {4}});
  require(failed.authority.has_value(), "failed operation starts after rejection closure");
  receipt = host.receipt(*failed.authority);
  require(receipt->output_non_delivery_evidence.has_value() &&
              receipt->settlement_evidence.has_value() &&
              receipt->output_non_delivery_evidence->sequence == 1 &&
              receipt->settlement_evidence->sequence == 2,
          "failure adapter sequence is independent of native terminal sequence");
  require(host.submit({"normal", {5}}).status ==
                  robot_harness::SampleSubmissionStatus::kSubmitted &&
              host.results().size() == 1 && host.results()[0].sum_of_squares == 25,
          "ordered failure closure permits actual fresh work at the same clock time");
}

void native_rejection_is_staged_before_cleanup() {
  robot_harness::MonotonicTime time = 700;
  robot_harness::SampleExecutionHost host(robot_harness::CompletionMode::kDeferred,
                                          [&time] { return time++; });
  require(host.initialize().state == robot_harness::InitializationState::kReady,
          "native-rejection fixture initializes");
  host.reject_next_native_submission();
  const auto rejected = host.submit({"rejected-a", {4}});
  require(rejected.status == robot_harness::SampleSubmissionStatus::kSubmitted &&
              rejected.authority.has_value(),
          "native rejection follows an actual submission attempt");
  require(host.worker_submission_count() == 1 && host.worker_execution_count() == 0 &&
              host.results().empty(),
          "rejected attempt does not execute or produce a result");
  auto receipt = host.receipt(*rejected.authority);
  require(receipt->dispatch == robot_harness::DispatchStatus::kSubmitted &&
              receipt->native_acceptance == robot_harness::NativeAcceptance::kRejected &&
              receipt->native_outcome == robot_harness::NativeOutcome::kNotExecuted &&
              receipt->output == robot_harness::OutputDisposition::kPending &&
              receipt->settlement == robot_harness::SettlementStatus::kPending,
          "receipt distinguishes attempted, rejected and unexecuted work");
  require(host.staged_event_count() == 2,
          "no-output and settlement observations remain staged after rejection");
  require(host.submit({"blocked-b", {5}}).status ==
              robot_harness::SampleSubmissionStatus::kAdmissionBlocked,
          "native rejection alone does not permit a conflicting operation");
  require(host.process_next_staged_event(), "adapter resolves that no output was produced");
  require(host.submit({"still-blocked-b", {5}}).status ==
              robot_harness::SampleSubmissionStatus::kAdmissionBlocked,
          "resolved output without settlement still blocks");
  require(host.process_next_staged_event(), "adapter reports completed cleanup");
  receipt = host.receipt(*rejected.authority);
  require(receipt->authority_disposition == robot_harness::AuthorityDisposition::kReleased,
          "native rejection releases only after full cleanup");

  const auto recovered = host.submit({"recovered-b", {5}});
  require(recovered.status == robot_harness::SampleSubmissionStatus::kSubmitted &&
              host.worker_submission_count() == 2,
          "fresh operation is submitted without retrying the rejected request");
  require(host.complete_deferred(), "fresh operation executes");
  host.process_all_staged_events();
  require(host.worker_execution_count() == 1 && host.results().size() == 1 &&
              host.results()[0].sum_of_squares == 25,
          "only the fresh operation executes and reaches the sink");
}

void accepted_native_failure_waits_for_output_resolution_and_settlement() {
  robot_harness::MonotonicTime time = 800;
  robot_harness::SampleExecutionHost host(robot_harness::CompletionMode::kDeferred,
                                          [&time] { return time++; });
  require(host.initialize().state == robot_harness::InitializationState::kReady,
          "native-failure fixture initializes");
  host.fail_next_native_execution();
  const auto failed = host.submit({"failed-a", {6, -2}});
  require(failed.status == robot_harness::SampleSubmissionStatus::kSubmitted &&
              failed.authority.has_value() && host.worker_submission_count() == 1,
          "failure control reaches an accepted native submission");
  require(host.complete_deferred(), "fixture drives the accepted work through failure");
  require(host.worker_execution_count() == 1 && host.results().empty() &&
              host.staged_event_count() == 4,
          "real execution fails without fabricating a result");
  require(host.process_next_staged_event(), "native start is observed");
  require(host.process_next_staged_event(), "native terminal failure is observed");
  auto receipt = host.receipt(*failed.authority);
  require(receipt->native_started &&
              receipt->native_outcome == robot_harness::NativeOutcome::kFailed &&
              !receipt->observed_native_success && receipt->observed_native_failure &&
              receipt->output == robot_harness::OutputDisposition::kPending,
          "terminal failure is truthful and leaves output unresolved");
  require(host.submit({"blocked-b", {7}}).status ==
              robot_harness::SampleSubmissionStatus::kAdmissionBlocked,
          "terminal failure alone blocks the next caller request");
  require(host.process_next_staged_event(), "adapter reports no output produced");
  require(host.submit({"still-blocked-b", {7}}).status ==
              robot_harness::SampleSubmissionStatus::kAdmissionBlocked,
          "output resolution still requires settlement");
  require(host.process_next_staged_event(), "adapter reports cleanup settlement");

  const auto recovered = host.submit({"recovered-b", {7}});
  require(recovered.status == robot_harness::SampleSubmissionStatus::kSubmitted,
          "settled failure permits a fresh request");
  require(host.complete_deferred(), "fresh request completes normally");
  host.process_all_staged_events();
  require(host.worker_submission_count() == 2 && host.worker_execution_count() == 2 &&
              host.results().size() == 1 && host.results()[0].sum_of_squares == 49,
          "failure is followed by one normal execution and result");
}

void native_success_survives_actual_sink_rejection() {
  robot_harness::MonotonicTime time = 900;
  robot_harness::SampleExecutionHost host(robot_harness::CompletionMode::kDeferred,
                                          [&time] { return time++; });
  require(host.initialize().state == robot_harness::InitializationState::kReady,
          "sink-rejection fixture initializes");
  const auto undelivered = host.submit({"undelivered-a", {3, 4}});
  require(undelivered.status == robot_harness::SampleSubmissionStatus::kSubmitted &&
              undelivered.authority.has_value(),
          "operation is submitted before sink availability changes");
  require(host.complete_deferred(), "native computation produces a staged result");
  require(host.process_next_staged_event(), "native start is observed");
  require(host.process_next_staged_event(), "native success is observed");
  host.set_result_sink_ready(false);
  require(host.process_next_staged_event(), "actual managed sink rejects and discards the result");
  auto receipt = host.receipt(*undelivered.authority);
  require(receipt->native_outcome == robot_harness::NativeOutcome::kSucceeded &&
              receipt->observed_native_success &&
              receipt->output == robot_harness::OutputDisposition::kNotDelivered &&
              receipt->output_non_delivery_reason ==
                  robot_harness::OutputNonDeliveryReason::kSinkRejected &&
              receipt->settlement == robot_harness::SettlementStatus::kPending &&
              host.results().empty(),
          "native success and sink delivery failure remain separate facts");
  host.set_result_sink_ready(true);
  require(host.submit({"blocked-b", {8}}).status ==
              robot_harness::SampleSubmissionStatus::kAdmissionBlocked,
          "restoring the sink cannot bypass pending cleanup");
  require(host.process_next_staged_event(), "discarded output is followed by settlement");
  receipt = host.receipt(*undelivered.authority);
  require(receipt->authority_disposition == robot_harness::AuthorityDisposition::kReleased,
          "sink rejection closes after actual disposal and settlement");

  const auto recovered = host.submit({"recovered-b", {8}});
  require(recovered.status == robot_harness::SampleSubmissionStatus::kSubmitted,
          "caller submits fresh work after sink-rejection closure");
  require(host.complete_deferred(), "fresh operation completes");
  host.process_all_staged_events();
  require(host.worker_submission_count() == 2 && host.worker_execution_count() == 2 &&
              host.results().size() == 1 && host.results()[0].sum_of_squares == 64,
          "old output is not retried and only the fresh result is delivered");
}

void normal_operation_can_be_followed_by_a_controlled_failure() {
  robot_harness::MonotonicTime time = 1000;
  robot_harness::SampleExecutionHost host(robot_harness::CompletionMode::kSynchronous,
                                          [&time] { return time++; });
  require(host.initialize().state == robot_harness::InitializationState::kReady,
          "mixed-path fixture initializes");
  require(host.submit({"normal-a", {2}}).status ==
              robot_harness::SampleSubmissionStatus::kSubmitted,
          "normal operation succeeds before failure");
  host.fail_next_native_execution();
  const auto failed = host.submit({"failed-b", {3}});
  require(failed.status == robot_harness::SampleSubmissionStatus::kSubmitted &&
              failed.authority.has_value(),
          "next accepted operation reaches the controlled failure");
  const auto receipt = host.receipt(*failed.authority);
  require(receipt->native_outcome == robot_harness::NativeOutcome::kFailed &&
              receipt->output == robot_harness::OutputDisposition::kNotDelivered &&
              receipt->settlement == robot_harness::SettlementStatus::kSettled,
          "synchronous failure closes without changing the prior result");
  require(host.results().size() == 1 && host.results()[0].sum_of_squares == 4 &&
              host.worker_submission_count() == 2 && host.worker_execution_count() == 2,
          "normal result remains and failed execution adds no result");
}

void shutdown_drains_a_pending_controlled_failure() {
  robot_harness::MonotonicTime time = 1100;
  robot_harness::SampleExecutionHost host(robot_harness::CompletionMode::kDeferred,
                                          [&time] { return time++; });
  require(host.initialize().state == robot_harness::InitializationState::kReady,
          "failure-shutdown fixture initializes");
  host.fail_next_native_execution();
  const auto failed = host.submit({"shutdown-failure", {9}});
  require(failed.status == robot_harness::SampleSubmissionStatus::kSubmitted &&
              failed.authority.has_value() && host.has_pending_native_work(),
          "shutdown begins with an accepted pending operation");
  host.shutdown();
  require(host.is_shutdown() && !host.has_pending_native_work() && host.staged_event_count() == 0 &&
              host.results().empty(),
          "shutdown drains failed native work and callbacks without a sink result");
  const auto receipt = host.receipt(*failed.authority);
  require(receipt.has_value() && receipt->native_outcome == robot_harness::NativeOutcome::kFailed &&
              receipt->output == robot_harness::OutputDisposition::kNotDelivered &&
              receipt->output_non_delivery_reason ==
                  robot_harness::OutputNonDeliveryReason::kNoOutputProduced &&
              receipt->settlement == robot_harness::SettlementStatus::kSettled,
          "shutdown preserves failure, no-output and settlement facts");
  require(host.submit({"after-shutdown", {1}}).admission_status ==
              robot_harness::AdmissionStatus::kClosed,
          "failure drain does not reopen admission after shutdown");
}

}  // namespace

int main() {
  unavailable_before_dispatch_closes_without_submission(true);
  unavailable_before_dispatch_closes_without_submission(false);
  host_preserves_adapter_sequences_with_a_constant_clock();
  native_rejection_is_staged_before_cleanup();
  accepted_native_failure_waits_for_output_resolution_and_settlement();
  native_success_survives_actual_sink_rejection();
  normal_operation_can_be_followed_by_a_controlled_failure();
  shutdown_drains_a_pending_controlled_failure();
  return 0;
}
