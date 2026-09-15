#include "robot_harness/sample_execution.hpp"

#include <iostream>

int main() {
  using namespace robot_harness;
  MonotonicTime now = 10;
  SampleExecutionHost host(CompletionMode::kDeferred, [&] { return now; });
  host.initialize();
  host.set_cancellation_mode(CancellationMode::kIgnore);
  const auto submitted = host.submit({"square-2-and-3", {2, 3}});
  if (!submitted.authority) {
    return 1;
  }
  const auto authority = *submitted.authority;
  ++now;
  host.request_cancel(authority);
  auto receipt = *host.receipt(authority);
  const bool acknowledged_but_running =
      receipt.stop_acknowledgement == StopAcknowledgement::kAcknowledged &&
      receipt.native_outcome == NativeOutcome::kPending && host.has_pending_native_work();
  std::cout << std::boolalpha
            << "cancel acknowledged; native still pending: " << acknowledged_but_running << '\n';
  const bool blocked =
      host.submit({"too-early", {4}}).admission_status == AdmissionStatus::kDomainOccupied;
  std::cout << "new request blocked until cleanup: " << blocked << '\n';
  ++now;
  host.complete_deferred();
  host.process_all_staged_events();
  receipt = *host.receipt(authority);
  const bool success_without_delivery =
      receipt.native_outcome == NativeOutcome::kSucceeded &&
      receipt.output_non_delivery_reason == OutputNonDeliveryReason::kAuthorityRevoked &&
      receipt.settlement == SettlementStatus::kSettled && host.results().empty();
  std::cout << "native succeeded; revoked result discarded; cleanup observed: "
            << success_without_delivery << '\n';
  const auto fresh = host.submit({"fresh-square-4", {4}});
  host.complete_deferred();
  host.process_all_staged_events();
  const bool fresh_result = fresh.status == SampleSubmissionStatus::kSubmitted &&
                            host.results().size() == 1 && host.results()[0].sum_of_squares == 16;
  std::cout << "fresh operation produces 16 after release: " << fresh_result << '\n';
  return acknowledged_but_running && blocked && success_without_delivery && fresh_result ? 0 : 1;
}
