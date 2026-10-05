#ifndef ROBOT_HARNESS_NAV2_NATIVE_LEAF_EVIDENCE_HPP_
#define ROBOT_HARNESS_NAV2_NATIVE_LEAF_EVIDENCE_HPP_
#include <stdexcept>
#include <string>

namespace robot_harness_nav2 {
struct LeafRecord {
  std::string action, scope, uuid, terminal;
  bool entered = false;
  bool ambiguous = false;

  void enter() {
    entered = true;
  }
  void associate(const std::string& identity, const std::string& outcome) {
    if (!entered || identity.size() != 32 || identity == std::string(32, '0') ||
        identity.find_first_not_of("0123456789abcdef") != std::string::npos ||
        (!uuid.empty() && uuid != identity) || (!terminal.empty() && terminal != outcome)) {
      ambiguous = true;
      return;
    }
    uuid = identity;
    terminal = outcome;
  }
  std::string kind() const {
    if (ambiguous)
      return "unknown";
    if (!entered)
      return "not_submitted";
    return uuid.empty() || terminal.empty() ? "unknown" : "terminal";
  }
  std::string json() const {
    return "{\"action\":\"" + action + "\",\"scope_id\":\"" + scope + "\",\"kind\":\"" + kind() +
           "\",\"child_uuid\":\"" + uuid + "\",\"terminal\":\"" + terminal + "\"}";
  }
};

// Borrowed by the native navigator only after its BT worker has joined.
class LeafEvidence {
public:
  virtual ~LeafEvidence() = default;
  virtual const LeafRecord& leaf_record() const = 0;
};
}  // namespace robot_harness_nav2

#endif
