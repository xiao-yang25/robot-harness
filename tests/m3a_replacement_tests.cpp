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

void replay_is_denied(SampleExecutionHost& host) {
  const auto denials = host.result_permission_denial_count();
  const auto results = host.results().size();
  const auto events = host.staged_event_count();
  require(host.replay_retained_result(), "a real generated result is retained");
  require(host.staged_event_count() == events + 1, "replay is staged, not silently discarded");
  require(host.process_next_staged_event(), "replayed callback reaches the normal event path");
  require(host.result_permission_denial_count() == denials + 1 &&
              host.results().size() == results && host.staged_event_count() == events,
          "actual sink denies replay without accepting a result");
}

void replacement_with_late_result() {
  MonotonicTime now = 100;
  SampleExecutionHost host(CompletionMode::kDeferred, [&] { return now; });
  require(host.initialize().state == InitializationState::kReady, "fresh fixture ready");
  host.set_result_replay_enabled(true);
  require(!host.replay_retained_result(), "replay cannot invent a result");
  host.set_cancellation_mode(CancellationMode::kIgnore);
  const auto a = *host.submit({"A", {2, -3, 4}}).authority;
  ++now;
  require(host.request_cancel(a).status == ControlStatus::kApplied, "A cancellation applied");
  require(host.has_pending_native_work(), "acknowledgement does not finish A");
  require(host.submit({"B", {5, 6}}).admission_status == AdmissionStatus::kDomainOccupied &&
              host.worker_submission_count() == 1,
          "B cannot dispatch while A still has native work");
  ++now;
  require(host.complete_deferred(), "ignored cancellation lets A actually compute");
  require(host.process_next_staged_event(), "A started");
  require(host.process_next_staged_event(), "A native success");
  require(host.submit({"B", {5, 6}}).admission_status == AdmissionStatus::kDomainOccupied,
          "A native success alone cannot admit B");
  require(host.process_next_staged_event(), "A original result disposed");
  require(host.results().empty() && host.result_permission_denial_count() == 1 &&
              host.receipt(a)->output == OutputDisposition::kNotDelivered,
          "A succeeded but the original revoked result was denied");
  require(host.submit({"B", {5, 6}}).admission_status == AdmissionStatus::kDomainOccupied &&
              host.worker_submission_count() == 1,
          "output disposition without settlement still blocks B");
  require(host.process_next_staged_event() &&
              host.receipt(a)->settlement == SettlementStatus::kSettled,
          "original work and output are resolved before replacement");
  ++now;
  const auto prepared = host.prepare({"B", {5, 6}});
  require(prepared.status == SampleSubmissionStatus::kPrepared && prepared.authority,
          "caller can now prepare B");
  const auto b = *prepared.authority;
  require(b.operation_id != a.operation_id && b.binding == a.binding,
          "new operation authority within the same binding");
  replay_is_denied(host);
  require(host.receipt(b)->dispatch == DispatchStatus::kPending &&
              host.receipt(b)->output == OutputDisposition::kPending,
          "old result cannot mutate prepared B");
  require(!host.dispatch_prepared(a) &&
              host.request_cancel(a).status == ControlStatus::kNoActiveOperation &&
              host.native_stop_request_count() == 1,
          "old dispatch and cancel cannot reach B's native work");
  require(host.dispatch_prepared(b) && host.worker_submission_count() == 2,
          "B dispatch remains usable");
  replay_is_denied(host);
  require(host.receipt(b)->native_outcome == NativeOutcome::kPending &&
              !host.receipt(b)->cancellation_requested_at,
          "old callback and control do not corrupt running B");
  ++now;
  require(host.complete_deferred(), "B actually computes");
  require(host.process_next_staged_event(), "B started");
  require(host.process_next_staged_event(), "B native success");
  require(host.receipt(b)->native_outcome == NativeOutcome::kSucceeded &&
              host.receipt(b)->output == OutputDisposition::kPending,
          "B is authorized to deliver before stale A arrives");
  replay_is_denied(host);
  require(host.receipt(b)->native_outcome == NativeOutcome::kSucceeded &&
              host.receipt(b)->output == OutputDisposition::kPending &&
              host.receipt(b)->result_reference.empty(),
          "A result cannot consume B's live output permission");
  host.process_all_staged_events();
  require(host.results().size() == 1 && host.results()[0].operation_id == b.operation_id &&
              host.results()[0].request_reference == "B" &&
              host.results()[0].squared_values == std::vector<std::int64_t>({25, 36}) &&
              host.results()[0].sum_of_squares == 61 &&
              host.receipt(b)->settlement == SettlementStatus::kSettled,
          "only B's independently known result is accepted and settled");
  replay_is_denied(host);
  require(host.receipt(b)->output == OutputDisposition::kAccepted &&
              host.receipt(b)->native_outcome == NativeOutcome::kSucceeded,
          "late A cannot rewrite settled B");
  const auto denials = host.result_permission_denial_count();
  require(host.replay_retained_result(), "stage stale A before shutdown");
  host.shutdown();
  require(host.is_shutdown() && host.staged_event_count() == 0 &&
              host.result_permission_denial_count() == denials + 1 && host.results().size() == 1 &&
              !host.replay_retained_result(),
          "shutdown drains stale callback through fencing and clears replay storage");
}

void missing_settlement_blocks_replacement() {
  MonotonicTime now = 200;
  SampleExecutionHost host(CompletionMode::kDeferred, [&] { return now; });
  host.initialize();
  host.set_result_replay_enabled(true);
  host.set_cancellation_mode(CancellationMode::kIgnore);
  host.set_settlement_reporting_enabled(false);
  const auto a = *host.submit({"A", {3}}).authority;
  host.request_cancel(a);
  host.complete_deferred();
  host.process_all_staged_events();
  replay_is_denied(host);
  now += 10000;
  host.poll();
  require(!host.has_pending_native_work() &&
              host.receipt(a)->settlement == SettlementStatus::kPending &&
              host.submit({"B", {4}}).admission_status == AdmissionStatus::kDomainOccupied &&
              host.worker_submission_count() == 1,
          "elapsed time and replay do not manufacture missing settlement");
  host.shutdown();
  require(host.receipt(a)->settlement == SettlementStatus::kPending &&
              !host.replay_retained_result(),
          "shutdown releases replay copy without claiming unknown work settled");
}

void replay_controls_and_duplicate_delivery() {
  MonotonicTime now = 300;
  SampleExecutionHost host(CompletionMode::kSynchronous, [&] { return now; });
  host.initialize();
  host.submit({"default", {2}});
  require(!host.replay_retained_result(), "replay retention is opt-in");
  host.set_result_replay_enabled(true);
  const auto authority = *host.submit({"captured", {3}}).authority;
  replay_is_denied(host);
  require(host.results().size() == 2 && host.results().back().sum_of_squares == 9 &&
              host.receipt(authority)->output == OutputDisposition::kAccepted,
          "replay of an accepted result cannot deliver twice");
  host.set_result_replay_enabled(false);
  require(!host.replay_retained_result(), "disabling clears retained result");
  host.shutdown();
  host.set_result_replay_enabled(true);
  require(!host.replay_retained_result(), "enabling replay cannot reopen a closed host");
}
}  // namespace

int main() {
  replacement_with_late_result();
  missing_settlement_blocks_replacement();
  replay_controls_and_duplicate_delivery();
  std::cout << "M3a replacement checks passed\n";
}
