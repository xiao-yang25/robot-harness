#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "robot_harness/authority_gate.hpp"

#include <array>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

namespace rh = robot_harness;
constexpr std::size_t kMaximumTextBytes = 1024;
constexpr const char* kBindingSource = "python-binding";
constexpr const char* kWorkerSource = "python-worker";
constexpr const char* kAdapterSource = "python-adapter";
constexpr const char* kSinkSource = "python-result-sink";
constexpr const char* kNativeSource = "python-episode";
constexpr const char* kSettlementSource = "python-closure";
constexpr const char* kCapabilitySource = "python-capabilities";
constexpr const char* kSettlementScope = "python-episode-lane";

struct GateState {
  GateState(rh::AuthorityGateConfig config, std::string profile, std::uint64_t maximum)
      : binding(config.binding), capability(config.capability_id), profile(std::move(profile)),
        maximum_units(maximum), gate(std::move(config)), owner_thread(PyThread_get_thread_ident()) {
  }

  rh::BindingIdentity binding;
  std::string capability;
  std::string profile;
  std::uint64_t maximum_units;
  rh::AuthorityGate gate;
  unsigned long owner_thread;
  std::uint64_t sequence = 0;
  std::optional<rh::OperationAuthority> authority;

  rh::EvidenceRecord record(const char* source, std::uint64_t now) {
    if (sequence == std::numeric_limits<std::uint64_t>::max()) {
      throw std::overflow_error("evidence sequence exhausted");
    }
    return {source, now, ++sequence};
  }
};

struct GateObject {
  PyObject_HEAD GateState* state;
};

void translate_exception() noexcept {
  try {
    throw;
  } catch (const std::bad_alloc&) {
    PyErr_NoMemory();
  } catch (const std::overflow_error& error) {
    PyErr_SetString(PyExc_OverflowError, error.what());
  } catch (const std::invalid_argument& error) {
    PyErr_SetString(PyExc_ValueError, error.what());
  } catch (const std::exception& error) {
    PyErr_SetString(PyExc_RuntimeError, error.what());
  } catch (...) {
    PyErr_SetString(PyExc_RuntimeError, "unexpected C++ bridge failure");
  }
}

bool unsigned_value(PyObject* value, std::uint64_t& result) {
  if (!PyLong_Check(value) || PyBool_Check(value)) {
    PyErr_SetString(PyExc_TypeError, "expected a nonnegative integer");
    return false;
  }
  const auto converted = PyLong_AsUnsignedLongLong(value);
  if (PyErr_Occurred()) {
    return false;
  }
  if (converted > std::numeric_limits<std::uint64_t>::max()) {
    PyErr_SetString(PyExc_OverflowError, "integer exceeds uint64 range");
    return false;
  }
  result = static_cast<std::uint64_t>(converted);
  return true;
}

bool text_value(PyObject* value, std::string& result, bool allow_empty = false) {
  if (!PyUnicode_Check(value)) {
    PyErr_SetString(PyExc_TypeError, "expected a string");
    return false;
  }
  Py_ssize_t size = 0;
  const char* bytes = PyUnicode_AsUTF8AndSize(value, &size);
  if (!bytes) {
    return false;
  }
  if ((!allow_empty && size == 0) || static_cast<std::size_t>(size) > kMaximumTextBytes) {
    PyErr_SetString(PyExc_ValueError, "string must contain 1 to 1024 UTF-8 bytes");
    return false;
  }
  result.assign(bytes, static_cast<std::size_t>(size));
  if (result.find('\0') != std::string::npos) {
    PyErr_SetString(PyExc_ValueError, "strings must not contain NUL");
    return false;
  }
  return true;
}

bool boolean_value(PyObject* value, bool& result) {
  if (!PyBool_Check(value)) {
    PyErr_SetString(PyExc_TypeError, "expected a bool");
    return false;
  }
  result = value == Py_True;
  return true;
}

GateState* owner_state(GateObject* self) {
  if (!self->state) {
    PyErr_SetString(PyExc_RuntimeError, "Gate is not initialized");
    return nullptr;
  }
  if (self->state->owner_thread != PyThread_get_thread_ident()) {
    PyErr_SetString(PyExc_RuntimeError, "Gate must be used on its owner thread");
    return nullptr;
  }
  return self->state;
}

bool arguments(PyObject* args, Py_ssize_t count) {
  if (PyTuple_GET_SIZE(args) != count) {
    PyErr_Format(PyExc_TypeError, "expected %zd positional arguments", count);
    return false;
  }
  return true;
}

bool operation_value(GateState& state, PyObject* value) {
  std::uint64_t operation_id;
  if (!unsigned_value(value, operation_id)) {
    return false;
  }
  if (!state.authority || state.authority->operation_id != operation_id) {
    PyErr_SetString(PyExc_ValueError, "operation_id is not the current operation");
    return false;
  }
  return true;
}

template <typename Function>
PyObject* boundary(GateObject* self, PyObject* args, Py_ssize_t count, Function function) noexcept {
  try {
    auto* state = owner_state(self);
    if (!state || !arguments(args, count)) {
      return nullptr;
    }
    return function(*state);
  } catch (...) {
    translate_exception();
    return nullptr;
  }
}

const char* name(rh::EvidenceDisposition value) {
  switch (value) {
  case rh::EvidenceDisposition::kAccepted:
    return "accepted";
  case rh::EvidenceDisposition::kDuplicate:
    return "duplicate";
  case rh::EvidenceDisposition::kRejected:
    return "rejected";
  }
  throw std::logic_error("unknown evidence disposition");
}

const char* name(rh::AdmissionStatus value) {
  switch (value) {
  case rh::AdmissionStatus::kAdmitted:
    return "admitted";
  case rh::AdmissionStatus::kInvalidRequest:
    return "invalid_request";
  case rh::AdmissionStatus::kExpired:
    return "expired";
  case rh::AdmissionStatus::kRecoveryRequired:
    return "recovery_required";
  case rh::AdmissionStatus::kDomainOccupied:
    return "domain_occupied";
  case rh::AdmissionStatus::kClosed:
    return "closed";
  case rh::AdmissionStatus::kCapabilitiesUnavailable:
    return "capabilities_unavailable";
  }
  throw std::logic_error("unknown admission status");
}

const char* name(rh::ControlStatus value) {
  switch (value) {
  case rh::ControlStatus::kApplied:
    return "applied";
  case rh::ControlStatus::kAlreadyRequested:
    return "already_requested";
  case rh::ControlStatus::kNoActiveOperation:
    return "no_active_operation";
  case rh::ControlStatus::kNotDue:
    return "not_due";
  case rh::ControlStatus::kRejected:
    return "rejected";
  }
  throw std::logic_error("unknown control status");
}

const char* name(rh::DispatchStatus value) {
  switch (value) {
  case rh::DispatchStatus::kPending:
    return "pending";
  case rh::DispatchStatus::kSubmitted:
    return "submitted";
  case rh::DispatchStatus::kNotSubmitted:
    return "not_submitted";
  }
  throw std::logic_error("unknown dispatch status");
}

const char* name(rh::NativeAcceptance value) {
  switch (value) {
  case rh::NativeAcceptance::kPending:
    return "pending";
  case rh::NativeAcceptance::kAccepted:
    return "accepted";
  case rh::NativeAcceptance::kRejected:
    return "rejected";
  }
  throw std::logic_error("unknown native acceptance");
}

const char* name(rh::NativeOutcome value) {
  switch (value) {
  case rh::NativeOutcome::kPending:
    return "pending";
  case rh::NativeOutcome::kNotExecuted:
    return "not_executed";
  case rh::NativeOutcome::kSucceeded:
    return "succeeded";
  case rh::NativeOutcome::kFailed:
    return "failed";
  case rh::NativeOutcome::kCancelled:
    return "cancelled";
  case rh::NativeOutcome::kUnknown:
    return "unknown";
  }
  throw std::logic_error("unknown native outcome");
}

const char* name(rh::OutputDisposition value) {
  switch (value) {
  case rh::OutputDisposition::kPending:
    return "pending";
  case rh::OutputDisposition::kAccepted:
    return "accepted";
  case rh::OutputDisposition::kNotDelivered:
    return "not_delivered";
  }
  throw std::logic_error("unknown output disposition");
}

const char* name(rh::SettlementStatus value) {
  switch (value) {
  case rh::SettlementStatus::kPending:
    return "pending";
  case rh::SettlementStatus::kSettled:
    return "settled";
  }
  throw std::logic_error("unknown settlement status");
}

const char* name(rh::AuthorityDisposition value) {
  switch (value) {
  case rh::AuthorityDisposition::kCurrent:
    return "current";
  case rh::AuthorityDisposition::kRevoked:
    return "revoked";
  case rh::AuthorityDisposition::kReleased:
    return "released";
  case rh::AuthorityDisposition::kBlockedUnknown:
    return "blocked_unknown";
  }
  throw std::logic_error("unknown authority disposition");
}

const char* name(rh::OutputNonDeliveryReason value) {
  switch (value) {
  case rh::OutputNonDeliveryReason::kNoOutputProduced:
    return "no_output";
  case rh::OutputNonDeliveryReason::kSinkRejected:
    return "sink_rejected";
  case rh::OutputNonDeliveryReason::kAuthorityRevoked:
    return "authority_revoked";
  }
  throw std::logic_error("unknown non-delivery reason");
}

struct PythonDeleter {
  void operator()(PyObject* value) const noexcept {
    Py_XDECREF(value);
  }
};
using PythonObject = std::unique_ptr<PyObject, PythonDeleter>;

bool put(PyObject* dict, const char* key, PyObject* value) {
  PythonObject owned(value);
  return owned && PyDict_SetItemString(dict, key, owned.get()) == 0;
}

PyObject* none_value() {
  Py_RETURN_NONE;
}

PyObject* optional_time(const std::optional<rh::MonotonicTime>& value) {
  if (value) {
    return PyLong_FromUnsignedLongLong(*value);
  }
  Py_RETURN_NONE;
}

PyObject* receipt_value(const rh::OperationReceipt& receipt) {
  const auto& authority = receipt.authority;
  const auto& binding = authority.binding;
  PythonObject identity(
      Py_BuildValue("{s:s,s:s,s:K,s:K}", "provider", binding.provider_id.c_str(), "domain",
                    binding.effect_domain.c_str(), "revision",
                    static_cast<unsigned long long>(binding.composition_revision), "generation",
                    static_cast<unsigned long long>(binding.provider_generation)));
  PythonObject authority_dict(PyDict_New());
  PythonObject result(PyDict_New());
  if (!identity || !authority_dict || !result ||
      PyDict_SetItemString(authority_dict.get(), "binding", identity.get()) < 0 ||
      !put(authority_dict.get(), "operation_id",
           PyLong_FromUnsignedLongLong(authority.operation_id)) ||
      !put(authority_dict.get(), "admitted_at",
           PyLong_FromUnsignedLongLong(authority.admitted_at)) ||
      PyDict_SetItemString(result.get(), "authority", authority_dict.get()) < 0) {
    return nullptr;
  }
  const std::array<std::pair<const char*, const char*>, 9> fields = {{
      {"dispatch", name(receipt.dispatch)},
      {"native_acceptance", name(receipt.native_acceptance)},
      {"native_outcome", name(receipt.native_outcome)},
      {"output", name(receipt.output)},
      {"settlement", name(receipt.settlement)},
      {"authority_disposition", name(receipt.authority_disposition)},
      {"domain_verdict", "unassessed"},
      {"native_identity", receipt.native_identity.c_str()},
      {"result_reference", receipt.result_reference.c_str()},
  }};
  for (const auto& field : fields) {
    if (!put(result.get(), field.first, PyUnicode_FromString(field.second))) {
      return nullptr;
    }
  }
  if (!put(result.get(), "native_started", PyBool_FromLong(receipt.native_started)) ||
      !put(result.get(), "deadline", optional_time(receipt.deadline)) ||
      !put(result.get(), "cancellation_requested_at",
           optional_time(receipt.cancellation_requested_at)) ||
      !put(result.get(), "expiry_observed_at", optional_time(receipt.expiry_observed_at))) {
    return nullptr;
  }
  PyObject* reason = receipt.output_non_delivery_reason
                         ? PyUnicode_FromString(name(*receipt.output_non_delivery_reason))
                         : none_value();
  if (!put(result.get(), "output_non_delivery_reason", reason)) {
    return nullptr;
  }
  return result.release();
}

int gate_init(GateObject* self, PyObject* args, PyObject* kwargs) noexcept {
  try {
    if (self->state) {
      PyErr_SetString(PyExc_RuntimeError, "Gate cannot be reinitialized");
      return -1;
    }
    PyObject* provider;
    PyObject* domain;
    PyObject* capability;
    PyObject* profile;
    PyObject* maximum;
    PyObject* now;
    static const char* keywords[] = {"provider",      "domain", "capability", "profile",
                                     "maximum_units", "now",    nullptr};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OOOOOO:Gate", const_cast<char**>(keywords),
                                     &provider, &domain, &capability, &profile, &maximum, &now)) {
      return -1;
    }
    rh::AuthorityGateConfig config;
    std::string profile_text;
    std::uint64_t maximum_units;
    if (!text_value(provider, config.binding.provider_id) ||
        !text_value(domain, config.binding.effect_domain) ||
        !text_value(capability, config.capability_id) || !text_value(profile, profile_text) ||
        !unsigned_value(maximum, maximum_units) || !unsigned_value(now, config.started_at)) {
      return -1;
    }
    config.binding.composition_revision = 1;
    config.binding.provider_generation = 1;
    config.binding_observer = kBindingSource;
    config.worker_observer = kWorkerSource;
    config.result_sink_observer = kSinkSource;
    config.native_evidence_source = kNativeSource;
    config.settlement_evidence_source = kSettlementSource;
    config.settlement_scope = kSettlementScope;
    config.execution_capability_observer = kCapabilitySource;
    self->state = new GateState(std::move(config), std::move(profile_text), maximum_units);
    return 0;
  } catch (...) {
    translate_exception();
    return -1;
  }
}

void gate_dealloc(GateObject* self) noexcept {
  delete self->state;
  auto* type = Py_TYPE(self);
  type->tp_free(reinterpret_cast<PyObject*>(self));
  Py_DECREF(type);
}

PyObject* gate_startup(GateObject* self, PyObject* args) {
  return boundary(self, args, 3, [&](GateState& state) -> PyObject* {
    std::string kind;
    bool observed;
    std::uint64_t now;
    if (!text_value(PyTuple_GET_ITEM(args, 0), kind) ||
        !boolean_value(PyTuple_GET_ITEM(args, 1), observed) ||
        !unsigned_value(PyTuple_GET_ITEM(args, 2), now)) {
      return nullptr;
    }
    rh::StartupEvidenceKind evidence_kind;
    const char* source;
    if (kind == "idle") {
      evidence_kind = rh::StartupEvidenceKind::kBindingIdleAndSettled;
      source = kBindingSource;
    } else if (kind == "worker") {
      evidence_kind = rh::StartupEvidenceKind::kWorkerReady;
      source = kWorkerSource;
    } else if (kind == "sink") {
      evidence_kind = rh::StartupEvidenceKind::kResultSinkReady;
      source = kSinkSource;
    } else if (kind == "adapter") {
      evidence_kind = rh::StartupEvidenceKind::kAdapterReady;
      source = kAdapterSource;
    } else {
      PyErr_SetString(PyExc_ValueError, "unknown startup kind");
      return nullptr;
    }
    return PyUnicode_FromString(name(state.gate.observe_startup(
        {evidence_kind, state.binding, state.record(source, now), observed})));
  });
}

PyObject* gate_capabilities(GateObject* self, PyObject* args) {
  return boundary(self, args, 3, [&](GateState& state) -> PyObject* {
    bool available;
    std::uint64_t now;
    std::uint64_t valid_until;
    if (!boolean_value(PyTuple_GET_ITEM(args, 0), available) ||
        !unsigned_value(PyTuple_GET_ITEM(args, 1), now) ||
        !unsigned_value(PyTuple_GET_ITEM(args, 2), valid_until)) {
      return nullptr;
    }
    return PyUnicode_FromString(name(state.gate.observe_execution_capabilities(
        {state.binding, state.capability, state.profile, state.maximum_units, available,
         state.record(kCapabilitySource, now), valid_until})));
  });
}

PyObject* gate_admit(GateObject* self, PyObject* args) {
  return boundary(self, args, 4, [&](GateState& state) -> PyObject* {
    rh::OperationRequest request;
    request.capability_id = state.capability;
    request.effect_domain = state.binding.effect_domain;
    std::uint64_t units;
    if (!text_value(PyTuple_GET_ITEM(args, 0), request.opaque_request_reference) ||
        !unsigned_value(PyTuple_GET_ITEM(args, 1), units) ||
        !unsigned_value(PyTuple_GET_ITEM(args, 3), request.requested_at)) {
      return nullptr;
    }
    if (PyTuple_GET_ITEM(args, 2) != Py_None) {
      std::uint64_t deadline;
      if (!unsigned_value(PyTuple_GET_ITEM(args, 2), deadline)) {
        return nullptr;
      }
      request.deadline = deadline;
    }
    request.target_reference = request.opaque_request_reference;
    request.execution_requirements = rh::ExecutionRequirements{state.profile, units};
    const auto decision = state.gate.admit(request);
    if (decision.authority) {
      state.authority = decision.authority;
    }
    PythonObject result(PyDict_New());
    if (!result) {
      return nullptr;
    }
    PyObject* operation_id = decision.authority
                                 ? PyLong_FromUnsignedLongLong(decision.authority->operation_id)
                                 : none_value();
    if (!put(result.get(), "operation_id", operation_id) ||
        !put(result.get(), "status", PyUnicode_FromString(name(decision.status)))) {
      return nullptr;
    }
    return result.release();
  });
}

// These methods retain the GIL. The host owns serialization and supplies actual facts.
template <typename Function>
PyObject* operation_time(GateObject* self, PyObject* args, Function function) {
  return boundary(self, args, 2, [&](GateState& state) -> PyObject* {
    std::uint64_t now;
    if (!operation_value(state, PyTuple_GET_ITEM(args, 0)) ||
        !unsigned_value(PyTuple_GET_ITEM(args, 1), now)) {
      return nullptr;
    }
    return function(state, now);
  });
}

PyObject* gate_dispatch(GateObject* self, PyObject* args) {
  return operation_time(self, args, [](GateState& state, std::uint64_t now) {
    return PyBool_FromLong(state.gate.claim_dispatch(*state.authority, now));
  });
}

PyObject* gate_tick(GateObject* self, PyObject* args) {
  return operation_time(self, args, [](GateState& state, std::uint64_t now) {
    return PyUnicode_FromString(name(state.gate.observe_time(*state.authority, now).status));
  });
}

PyObject* gate_cancel(GateObject* self, PyObject* args) {
  return operation_time(self, args, [](GateState& state, std::uint64_t now) {
    return PyUnicode_FromString(name(state.gate.request_cancel(*state.authority, now).status));
  });
}

PyObject* gate_non_submission(GateObject* self, PyObject* args) {
  return operation_time(self, args, [](GateState& state, std::uint64_t now) {
    return PyUnicode_FromString(name(state.gate.observe_non_submission(
        {*state.authority, state.record(kSettlementSource, now)})));
  });
}

PyObject* gate_settle(GateObject* self, PyObject* args) {
  return operation_time(self, args, [](GateState& state, std::uint64_t now) {
    return PyUnicode_FromString(name(state.gate.observe_settlement(
        {*state.authority, kSettlementScope, true, state.record(kSettlementSource, now)})));
  });
}

PyObject* gate_receipt(GateObject* self, PyObject* args) {
  return boundary(self, args, 1, [&](GateState& state) -> PyObject* {
    if (!operation_value(state, PyTuple_GET_ITEM(args, 0))) {
      return nullptr;
    }
    const auto receipt = state.gate.receipt(*state.authority);
    if (!receipt) {
      Py_RETURN_NONE;
    }
    return receipt_value(*receipt);
  });
}

PyObject* gate_can_deliver(GateObject* self, PyObject* args) {
  return boundary(self, args, 1, [&](GateState& state) -> PyObject* {
    if (!operation_value(state, PyTuple_GET_ITEM(args, 0))) {
      return nullptr;
    }
    return PyBool_FromLong(state.gate.can_deliver_result(*state.authority));
  });
}

PyObject* gate_supports(GateObject* self, PyObject* args) {
  return boundary(self, args, 2, [&](GateState& state) -> PyObject* {
    std::uint64_t units;
    std::uint64_t now;
    if (!unsigned_value(PyTuple_GET_ITEM(args, 0), units) ||
        !unsigned_value(PyTuple_GET_ITEM(args, 1), now)) {
      return nullptr;
    }
    return PyBool_FromLong(state.gate.supports_execution({state.profile, units}, now));
  });
}

PyObject* gate_native(GateObject* self, PyObject* args) {
  return boundary(self, args, 3, [&](GateState& state) -> PyObject* {
    std::string kind;
    std::uint64_t now;
    if (!operation_value(state, PyTuple_GET_ITEM(args, 0)) ||
        !text_value(PyTuple_GET_ITEM(args, 1), kind) ||
        !unsigned_value(PyTuple_GET_ITEM(args, 2), now)) {
      return nullptr;
    }
    rh::NativeEventKind event;
    if (kind == "accepted") {
      event = rh::NativeEventKind::kAccepted;
    } else if (kind == "started") {
      event = rh::NativeEventKind::kStarted;
    } else if (kind == "succeeded") {
      event = rh::NativeEventKind::kTerminalSucceeded;
    } else if (kind == "failed") {
      event = rh::NativeEventKind::kTerminalFailed;
    } else if (kind == "cancelled") {
      event = rh::NativeEventKind::kTerminalCancelled;
    } else if (kind == "rejected") {
      event = rh::NativeEventKind::kRejected;
    } else {
      PyErr_SetString(PyExc_ValueError, "unknown native event kind");
      return nullptr;
    }
    return PyUnicode_FromString(name(state.gate.observe_native(
        {*state.authority, "episode-" + std::to_string(state.authority->operation_id), event,
         state.record(kNativeSource, now)})));
  });
}

PyObject* gate_output(GateObject* self, PyObject* args) {
  return boundary(self, args, 4, [&](GateState& state) -> PyObject* {
    std::string disposition;
    std::string reference;
    std::uint64_t now;
    if (!operation_value(state, PyTuple_GET_ITEM(args, 0)) ||
        !text_value(PyTuple_GET_ITEM(args, 1), disposition) ||
        !text_value(PyTuple_GET_ITEM(args, 2), reference, true) ||
        !unsigned_value(PyTuple_GET_ITEM(args, 3), now)) {
      return nullptr;
    }
    if (disposition == "accepted") {
      return PyUnicode_FromString(name(state.gate.observe_output_accepted(
          {*state.authority, reference, state.record(kSinkSource, now)})));
    }
    rh::OutputNonDeliveryReason reason;
    const char* source = kSinkSource;
    if (disposition == "no_output") {
      reason = rh::OutputNonDeliveryReason::kNoOutputProduced;
      source = kSettlementSource;
    } else if (disposition == "authority_revoked") {
      reason = rh::OutputNonDeliveryReason::kAuthorityRevoked;
    } else if (disposition == "sink_rejected") {
      reason = rh::OutputNonDeliveryReason::kSinkRejected;
    } else {
      PyErr_SetString(PyExc_ValueError, "unknown output disposition");
      return nullptr;
    }
    return PyUnicode_FromString(name(state.gate.observe_output_not_delivered(
        {*state.authority, reason, reference, state.record(source, now)})));
  });
}

PyObject* gate_close(GateObject* self, PyObject* args) {
  return boundary(self, args, 0, [](GateState& state) -> PyObject* {
    state.gate.close_admission();
    Py_RETURN_NONE;
  });
}

PyMethodDef gate_methods[] = {
    {"startup", reinterpret_cast<PyCFunction>(gate_startup), METH_VARARGS, nullptr},
    {"capabilities", reinterpret_cast<PyCFunction>(gate_capabilities), METH_VARARGS, nullptr},
    {"admit", reinterpret_cast<PyCFunction>(gate_admit), METH_VARARGS, nullptr},
    {"dispatch", reinterpret_cast<PyCFunction>(gate_dispatch), METH_VARARGS, nullptr},
    {"tick", reinterpret_cast<PyCFunction>(gate_tick), METH_VARARGS, nullptr},
    {"cancel", reinterpret_cast<PyCFunction>(gate_cancel), METH_VARARGS, nullptr},
    {"non_submission", reinterpret_cast<PyCFunction>(gate_non_submission), METH_VARARGS, nullptr},
    {"receipt", reinterpret_cast<PyCFunction>(gate_receipt), METH_VARARGS, nullptr},
    {"supports", reinterpret_cast<PyCFunction>(gate_supports), METH_VARARGS, nullptr},
    {"native", reinterpret_cast<PyCFunction>(gate_native), METH_VARARGS, nullptr},
    {"output", reinterpret_cast<PyCFunction>(gate_output), METH_VARARGS, nullptr},
    {"settle", reinterpret_cast<PyCFunction>(gate_settle), METH_VARARGS, nullptr},
    {"close", reinterpret_cast<PyCFunction>(gate_close), METH_VARARGS, nullptr},
    {"can_deliver", reinterpret_cast<PyCFunction>(gate_can_deliver), METH_VARARGS, nullptr},
    {nullptr, nullptr, 0, nullptr},
};

PyType_Slot gate_slots[] = {
    {Py_tp_new, reinterpret_cast<void*>(PyType_GenericNew)},
    {Py_tp_init, reinterpret_cast<void*>(gate_init)},
    {Py_tp_dealloc, reinterpret_cast<void*>(gate_dealloc)},
    {Py_tp_methods, gate_methods},
    {0, nullptr},
};
PyType_Spec gate_spec = {"robot_harness._core.Gate", sizeof(GateObject), 0, Py_TPFLAGS_DEFAULT,
                         gate_slots};
PyModuleDef module_definition = {
    PyModuleDef_HEAD_INIT, "_core", nullptr, -1, nullptr, nullptr, nullptr, nullptr, nullptr};

}  // namespace

PyMODINIT_FUNC PyInit__core() {
  try {
    PythonObject module(PyModule_Create(&module_definition));
    PythonObject gate_type(PyType_FromSpec(&gate_spec));
    if (!module || !gate_type || PyModule_AddObject(module.get(), "Gate", gate_type.get()) < 0) {
      return nullptr;
    }
    gate_type.release();
    return module.release();
  } catch (...) {
    translate_exception();
    return nullptr;
  }
}
