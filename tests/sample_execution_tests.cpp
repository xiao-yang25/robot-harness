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

void synchronous_worker_delivers_actual_result() {
  robot_harness::MonotonicTime time = 100;
  robot_harness::SampleExecutionHost host(robot_harness::CompletionMode::kSynchronous,
                                          [&time] { return time++; });
  require(host.initialize().state == robot_harness::InitializationState::kReady,
          "fresh fixture becomes ready");
  const auto submission = host.submit({"sample-a", {2, -3, 4}});
  require(submission.status == robot_harness::SampleSubmissionStatus::kSubmitted,
          "valid sample submitted");
  require(host.worker_submission_count() == 1, "worker observed one actual submission");
  require(host.results().size() == 1, "managed sink accepted one result");
  const auto& result = host.results().front();
  require(result.squared_values == std::vector<std::int64_t>({4, 9, 16}),
          "worker output matches independent expected values");
  require(result.sum_of_squares == 29, "worker aggregate matches independent expected value");
  const auto receipt = host.receipt(*submission.authority);
  require(receipt.has_value(), "receipt is observable");
  require(receipt->native_outcome == robot_harness::NativeOutcome::kSucceeded,
          "receipt has native success");
  require(receipt->output == robot_harness::OutputDisposition::kAccepted,
          "receipt has managed output acceptance");
  require(receipt->settlement == robot_harness::SettlementStatus::kSettled,
          "receipt has fixture settlement");

  const auto second = host.submit({"sample-b", {5, 6}});
  require(second.status == robot_harness::SampleSubmissionStatus::kSubmitted,
          "settlement permits a caller-selected sequential operation");
  require(host.worker_submission_count() == 2, "worker observes sequential submissions");
  require(host.results().size() == 2, "sink observes sequential results");
  require(host.results()[1].squared_values == std::vector<std::int64_t>({25, 36}) &&
              host.results()[1].sum_of_squares == 61,
          "second actual output matches independent expectation");
  require(second.authority->operation_id == submission.authority->operation_id + 1,
          "Core allocates a new sequential operation identity");
}

void readiness_and_argument_checks_prevent_submission() {
  robot_harness::MonotonicTime time = 200;
  robot_harness::SampleExecutionHost host(robot_harness::CompletionMode::kSynchronous,
                                          [&time] { return time++; });
  host.set_worker_ready(false);
  require(host.initialize().state == robot_harness::InitializationState::kRecoveryRequired,
          "not-ready worker keeps fresh host closed");
  require(host.submit({"sample", {1}}).status ==
              robot_harness::SampleSubmissionStatus::kAdmissionBlocked,
          "valid payload is not submitted before readiness");
  require(host.worker_submission_count() == 0, "blocked admission does not reach worker");
  host.set_worker_ready(true);
  host.set_result_sink_ready(false);
  require(host.initialize().state == robot_harness::InitializationState::kRecoveryRequired,
          "unavailable managed sink keeps fresh host closed");
  require(host.submit({"sample", {1}}).status ==
              robot_harness::SampleSubmissionStatus::kAdmissionBlocked,
          "ready worker cannot bypass unavailable sink");
  host.set_result_sink_ready(true);
  require(host.initialize().state == robot_harness::InitializationState::kReady,
          "fresh worker and sink observations complete initialization");
  require(host.submit({"", {1}}).status == robot_harness::SampleSubmissionStatus::kInvalidArgument,
          "empty request reference rejected by adapter");
  require(host.submit({"empty", {}}).status ==
              robot_harness::SampleSubmissionStatus::kInvalidArgument,
          "empty sample rejected by adapter");
  require(host.submit({"out-of-range", {10001}}).status ==
              robot_harness::SampleSubmissionStatus::kInvalidArgument,
          "unbounded arithmetic input rejected by adapter");
  require(host.worker_submission_count() == 0, "invalid inputs never reach worker");
}

void deferred_events_expose_pending_delivery_and_settlement() {
  robot_harness::MonotonicTime time = 300;
  robot_harness::SampleExecutionHost host(robot_harness::CompletionMode::kDeferred,
                                          [&time] { return time++; });
  require(host.initialize().state == robot_harness::InitializationState::kReady,
          "deferred fixture becomes ready");
  const auto first = host.submit({"sample-a", {3, 7}});
  require(first.status == robot_harness::SampleSubmissionStatus::kSubmitted,
          "deferred sample submitted");
  auto receipt = host.receipt(*first.authority);
  require(receipt->native_acceptance == robot_harness::NativeAcceptance::kAccepted,
          "native acceptance is visible before completion");
  require(!receipt->native_started &&
              receipt->native_outcome == robot_harness::NativeOutcome::kPending,
          "acceptance is not relabeled as execution start or completion");
  require(host.has_pending_native_work(), "worker retains deferred payload lifetime");
  require(host.results().empty(), "sink has no result before worker completion");

  require(host.complete_deferred(), "host explicitly drives deferred worker completion");
  require(!host.has_pending_native_work(), "native payload use ends after completion callback");
  require(host.staged_event_count() == 4,
          "completion callbacks remain staged for host serialization");
  require(host.process_next_staged_event(), "host applies observed start");
  require(host.process_next_staged_event(), "host applies native terminal");
  receipt = host.receipt(*first.authority);
  require(receipt->native_started &&
              receipt->native_outcome == robot_harness::NativeOutcome::kSucceeded,
          "actual computation start and terminal are distinct observations");
  require(receipt->output == robot_harness::OutputDisposition::kPending &&
              receipt->settlement == robot_harness::SettlementStatus::kPending,
          "native completion does not manufacture delivery or settlement");
  require(host.results().empty(), "terminal evidence alone cannot reach managed sink");
  require(host.submit({"sample-b", {4}}).status ==
              robot_harness::SampleSubmissionStatus::kAdmissionBlocked,
          "pending delivery blocks a conflicting submission");
  require(host.worker_submission_count() == 1, "blocked operation is not queued or dispatched");

  require(host.process_next_staged_event(),
          "host applies result-ready callback at managed boundary");
  receipt = host.receipt(*first.authority);
  require(host.results().size() == 1 && host.results()[0].sum_of_squares == 58,
          "managed sink receives independently expected deferred output");
  require(receipt->output == robot_harness::OutputDisposition::kAccepted &&
              receipt->settlement == robot_harness::SettlementStatus::kPending,
          "output acceptance remains separate from cleanup settlement");
  require(host.submit({"sample-b", {4}}).status ==
              robot_harness::SampleSubmissionStatus::kAdmissionBlocked,
          "pending settlement keeps the effect domain occupied");
  require(host.process_next_staged_event(), "host applies declared settlement");
  receipt = host.receipt(*first.authority);
  require(receipt->settlement == robot_harness::SettlementStatus::kSettled &&
              receipt->authority_disposition == robot_harness::AuthorityDisposition::kReleased,
          "settlement releases the normal fixture operation");

  const auto second = host.submit({"sample-b", {4}});
  require(second.status == robot_harness::SampleSubmissionStatus::kSubmitted,
          "caller can choose the next operation after release");
  require(host.complete_deferred(), "second deferred sample completes");
  host.process_all_staged_events();
  require(host.results().size() == 2 && host.results()[1].sum_of_squares == 16,
          "second deferred output is actual and independently expected");
}

void shutdown_drains_callbacks_before_releasing_targets() {
  robot_harness::MonotonicTime time = 400;
  robot_harness::SampleExecutionHost host(robot_harness::CompletionMode::kDeferred,
                                          [&time] { return time++; });
  require(host.initialize().state == robot_harness::InitializationState::kReady,
          "shutdown fixture becomes ready");
  const auto submission = host.submit({"shutdown-sample", {-2, 8}});
  require(host.has_pending_native_work(), "shutdown begins with retained native work");
  host.shutdown();
  require(host.is_shutdown() && !host.has_pending_native_work() && host.staged_event_count() == 0,
          "shutdown closes admission and drains native callbacks");
  require(host.results().size() == 1 && host.results()[0].sum_of_squares == 68,
          "shutdown keeps managed sink alive through pending result delivery");
  const auto receipt = host.receipt(*submission.authority);
  require(receipt.has_value() &&
              receipt->native_outcome == robot_harness::NativeOutcome::kSucceeded &&
              receipt->output == robot_harness::OutputDisposition::kAccepted &&
              receipt->settlement == robot_harness::SettlementStatus::kSettled,
          "shutdown drains all correctness-bearing evidence");
  require(host.submit({"after-shutdown", {1}}).admission_status ==
              robot_harness::AdmissionStatus::kClosed,
          "shutdown prevents new admission");
}

}  // namespace

int main() {
  synchronous_worker_delivers_actual_result();
  readiness_and_argument_checks_prevent_submission();
  deferred_events_expose_pending_delivery_and_settlement();
  shutdown_drains_callbacks_before_releasing_targets();
  return 0;
}
