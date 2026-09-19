#include "robot_harness/sample_execution.hpp"

#include <iostream>

int main() {
  using namespace robot_harness;
  MonotonicTime now = 100;
  SampleExecutionHost host(CompletionMode::kDeferred, [&] { return now; });
  if (host.initialize().state != InitializationState::kReady) {
    return 1;
  }
  host.set_cancellation_mode(CancellationMode::kIgnore);
  host.set_result_replay_enabled(true);
  const auto a = host.submit({"A", {2, -3, 4}});
  if (!a.authority || a.status != SampleSubmissionStatus::kSubmitted) {
    return 1;
  }
  const SampleRequest replacement{"B", {5, 6}};
  ++now;
  host.request_cancel(*a.authority);
  const bool blocked =
      host.submit(replacement).admission_status == AdmissionStatus::kDomainOccupied &&
      host.has_pending_native_work() && host.worker_submission_count() == 1;
  std::cout << "B blocked while cancelled A is still pending: " << blocked << '\n';
  host.complete_deferred();
  host.process_all_staged_events();
  const auto closed_a = host.receipt(*a.authority);
  if (!blocked || !closed_a || closed_a->native_outcome != NativeOutcome::kSucceeded ||
      closed_a->settlement != SettlementStatus::kSettled || !host.results().empty()) {
    return 1;
  }
  ++now;
  const auto b = host.submit(replacement);
  if (!b.authority || b.status != SampleSubmissionStatus::kSubmitted) {
    return 1;
  }
  host.complete_deferred();
  host.process_next_staged_event();  // B started.
  host.process_next_staged_event();  // B succeeded; its output is still pending.
  const auto denials = host.result_permission_denial_count();
  if (!host.replay_retained_result()) {
    return 1;
  }
  host.process_next_staged_event();  // A's old result arrives before B's result.
  const bool fenced = host.result_permission_denial_count() == denials + 1 &&
                      host.results().empty() &&
                      host.receipt(*b.authority)->output == OutputDisposition::kPending;
  const bool old_cancel_rejected =
      host.request_cancel(*a.authority).status == ControlStatus::kNoActiveOperation &&
      !host.receipt(*b.authority)->cancellation_requested_at;
  host.process_all_staged_events();
  const bool accepted = host.results().size() == 1 &&
                        host.results()[0].operation_id == b.authority->operation_id &&
                        host.results()[0].sum_of_squares == 61 &&
                        host.receipt(*b.authority)->settlement == SettlementStatus::kSettled;
  std::cout << "A's replay denied at B's delivery boundary: " << fenced << '\n'
            << "A's old cancel cannot affect B: " << old_cancel_rejected << '\n';
  if (accepted) {
    std::cout << "B result: " << host.results()[0].sum_of_squares << '\n';
  }
  host.shutdown();
  return fenced && old_cancel_rejected && accepted && !host.replay_retained_result() ? 0 : 1;
}
