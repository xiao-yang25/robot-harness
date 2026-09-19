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

void rebind_after_closure_fences_old_results() {
  MonotonicTime now = 100;
  SampleExecutionHost host(CompletionMode::kDeferred, [&] { return now; });
  host.initialize();
  host.set_result_replay_enabled(true);
  host.set_cancellation_mode(CancellationMode::kIgnore);
  const auto a = *host.submit({"old", {3, 4}}).authority;
  ++now;
  const auto withdrawal = host.withdraw_binding(a.binding);
  require(withdrawal.status == BindingControlStatus::kApplied && withdrawal.native_stop_authority &&
              host.native_stop_request_count() == 1,
          "withdrawal requests one stop with original authority");
  require(host.receipt(a)->binding_withdrawn_at == now &&
              !host.receipt(a)->cancellation_requested_at,
          "provider withdrawal is distinct from user cancellation");
  require(host.rebind_provider(a.binding, "new-worker", CompletionMode::kDeferred).status ==
                  BindingControlStatus::kUnresolvedWork &&
              host.has_pending_native_work(),
          "stop acknowledgement cannot release the old owner");
  host.request_cancel(a);
  host.withdraw_binding(a.binding);
  require(host.native_stop_request_count() == 1, "cancel and repeat withdrawal do not stop twice");
  require(host.complete_deferred(), "old provider really produces its result");
  require(host.process_next_staged_event() && host.process_next_staged_event() &&
              host.process_next_staged_event(),
          "native success and revoked output are observed");
  require(host.results().empty() && host.result_permission_denial_count() == 1,
          "actual sink rejects the original revoked result");
  require(host.rebind_provider(a.binding, "new-worker", CompletionMode::kDeferred).status ==
              BindingControlStatus::kUnresolvedWork,
          "pending old settlement prevents handoff");
  host.process_all_staged_events();
  require(host.receipt(a)->settlement == SettlementStatus::kSettled, "old work settled");
  // This armed failure belongs to the retired provider and must not reach its replacement.
  host.fail_next_native_execution();
  ++now;
  require(host.rebind_provider(a.binding, "new-worker", CompletionMode::kDeferred).status ==
              BindingControlStatus::kApplied,
          "same host rebinds after full old closure");
  const auto binding = host.binding_status().active;
  require(binding.provider_id == "new-worker" && binding.effect_domain == a.binding.effect_domain &&
              binding.provider_generation > a.binding.provider_generation,
          "same result domain, fresh provider identity");
  const auto old_receipt = host.receipt(a);
  require(old_receipt && old_receipt->authority_disposition == AuthorityDisposition::kReleased,
          "old receipt remains queryable before next admission");
  require(host.replay_retained_result() && host.process_next_staged_event() &&
              host.receipt(a)->result_reference == old_receipt->result_reference &&
              host.receipt(a)->output == old_receipt->output,
          "old callback cannot rewrite the retained receipt after commit");
  const auto b = *host.submit({"new", {5, 6}}).authority;
  require(b.binding == binding && b.operation_id > a.operation_id &&
              host.request_cancel(a).status == ControlStatus::kNoActiveOperation &&
              !host.dispatch_prepared(a) &&
              host.withdraw_binding(a.binding).status == BindingControlStatus::kStaleIdentity,
          "stale controls cannot affect the new operation");
  require(host.complete_deferred() && host.process_next_staged_event() &&
              host.process_next_staged_event(),
          "new provider computes despite the old provider's armed failure");
  require(host.receipt(b)->native_outcome == NativeOutcome::kSucceeded &&
              host.receipt(b)->output == OutputDisposition::kPending,
          "new operation has live delivery permission before stale arrival");
  const auto denials = host.result_permission_denial_count();
  require(host.replay_retained_result() && host.process_next_staged_event() &&
              host.result_permission_denial_count() == denials + 1 && host.results().empty(),
          "old real callback reaches the actual sink and is denied");
  host.process_all_staged_events();
  require(host.results().size() == 1 && host.results()[0].operation_id == b.operation_id &&
              host.results()[0].squared_values == std::vector<std::int64_t>({25, 36}) &&
              host.results()[0].sum_of_squares == 61 &&
              host.receipt(b)->settlement == SettlementStatus::kSettled,
          "only the replacement's independently known result is delivered");
  host.shutdown();
  require(host.rebind_provider(binding, "closed", CompletionMode::kDeferred).status ==
                  BindingControlStatus::kClosed &&
              !host.replay_retained_result(),
          "shutdown cannot be undone by rebind");
}

void withdrawal_before_dispatch_and_missing_settlement() {
  MonotonicTime now = 200;
  SampleExecutionHost host(CompletionMode::kDeferred, [&] { return now; });
  host.initialize();
  const auto a = *host.prepare({"never-submit", {2}}).authority;
  require(host.withdraw_binding(a.binding).status == BindingControlStatus::kApplied &&
              !host.dispatch_prepared(a) && host.worker_submission_count() == 0 &&
              host.native_stop_request_count() == 0,
          "prepared withdrawal neither submits nor invents a native stop");
  host.process_all_staged_events();
  require(host.receipt(a)->dispatch == DispatchStatus::kNotSubmitted &&
              host.receipt(a)->settlement == SettlementStatus::kSettled,
          "prepared work closes through existing non-submission evidence");
  require(host.rebind_provider(a.binding, "", CompletionMode::kDeferred).status ==
                  BindingControlStatus::kInvalidProvider &&
              host.binding_status().phase == BindingPhase::kWithdrawn,
          "invalid provider setup cannot reopen admission");
  require(host.rebind_provider(a.binding, "second", CompletionMode::kDeferred).status ==
              BindingControlStatus::kApplied,
          "fresh provider usable after proven non-submission");
  host.set_settlement_reporting_enabled(false);
  const auto b = *host.submit({"missing-cleanup", {4}}).authority;
  host.withdraw_binding(b.binding);
  host.complete_deferred();
  host.process_all_staged_events();
  now += 10000;
  require(!host.has_pending_native_work() &&
              host.rebind_provider(b.binding, "third", CompletionMode::kDeferred).status ==
                  BindingControlStatus::kUnresolvedWork,
          "native completion and elapsed time cannot replace missing closure evidence");
}

void dependency_loss_requires_explicit_rebind() {
  for (const bool worker_loss : {true, false}) {
    MonotonicTime now = 300;
    SampleExecutionHost host(CompletionMode::kDeferred, [&] { return now++; });
    host.initialize();
    const auto old = host.binding_status().active;
    if (worker_loss) {
      host.set_worker_ready(false);
    } else {
      host.set_result_sink_ready(false);
    }
    require(host.initialize().state == InitializationState::kRecoveryRequired &&
                host.binding_status().phase == BindingPhase::kWithdrawn,
            "initialization cannot downgrade observed dependency loss to startup waiting");
    require(host.submit({"blocked", {1}}).status == SampleSubmissionStatus::kAdmissionBlocked &&
                host.worker_submission_count() == 0,
            "required dependency loss prevents prepare and native submission");
    host.set_worker_ready(true);
    host.set_result_sink_ready(true);
    host.initialize();
    require(host.submit({"still-blocked", {1}}).status == SampleSubmissionStatus::kAdmissionBlocked,
            "restoration and initial readiness API cannot reopen a withdrawn binding");
    require(host.rebind_provider(old, "restored", CompletionMode::kSynchronous).status ==
                BindingControlStatus::kApplied,
            "no previous operation still needs explicit quiescent handoff");
    const auto result = host.submit({"restored", {7}});
    require(result.status == SampleSubmissionStatus::kSubmitted && host.results().size() == 1 &&
                host.results()[0].sum_of_squares == 49,
            "replacement completion mode and result are used");
  }
}
}  // namespace

int main() {
  rebind_after_closure_fences_old_results();
  withdrawal_before_dispatch_and_missing_settlement();
  dependency_loss_requires_explicit_rebind();
  std::cout << "M3b sample rebind checks passed\n";
}
