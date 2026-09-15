#include "robot_harness/sample_execution.hpp"

#include <iostream>

int main() {
  using namespace robot_harness;
  MonotonicTime now = 10;
  SampleExecutionHost host(CompletionMode::kDeferred, [&] { return now; });
  host.initialize();
  host.set_cancellation_mode(CancellationMode::kIgnore);
  const auto submission = host.submit({"deadline-square", {2, 3}, 20});
  if (!submission.authority) {
    return 1;
  }
  const auto authority = *submission.authority;
  now = 20;
  const bool passive_receipt = !host.receipt(authority)->expiry_observed_at;
  host.poll();
  auto receipt = *host.receipt(authority);
  const bool expired_but_running = receipt.expiry_observed_at == 20 &&
                                   receipt.native_outcome == NativeOutcome::kPending &&
                                   host.has_pending_native_work();
  host.request_cancel(authority);
  const bool one_stop = host.native_stop_request_count() == 1 &&
                        host.receipt(authority)->cancellation_requested_at.has_value();
  now = 25;
  host.complete_deferred();
  host.process_all_staged_events();
  receipt = *host.receipt(authority);
  const bool late_result_discarded =
      receipt.native_outcome == NativeOutcome::kSucceeded &&
      receipt.output_non_delivery_reason == OutputNonDeliveryReason::kAuthorityRevoked &&
      receipt.settlement == SettlementStatus::kSettled && host.results().empty();
  std::cout << std::boolalpha << "receipt read is passive: " << passive_receipt << '\n'
            << "poll observes deadline 20; native still running: " << expired_but_running << '\n'
            << "expiry plus cancellation requests one native stop: " << one_stop << '\n'
            << "native finishes at 25; late output discarded; cleanup observed: "
            << late_result_discarded << '\n';
  return passive_receipt && expired_but_running && one_stop && late_result_discarded ? 0 : 1;
}
