#include "../simulation/native_drive/motion_permission.hpp"
#include <array>
#include <iostream>

namespace {
using robot_harness::simulation::MotionPermission;
using robot_harness::simulation::MotionPermitIdentity;
using robot_harness::simulation::write_current;
const MotionPermitIdentity kA{std::string(32, 'a'), 1, 1};
const MotionPermitIdentity kB{std::string(32, 'b'), 2, 2};

void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void deterministic_checks() {
  // Literal traces express the contract independently of runtime scheduling.
  MotionPermission normal(1000);
  require(!normal.can_consume(kA, 99), "boot allowed motion");
  require(!normal.open(kA, 0, 100, 100, false), "opened before zero application");
  require(normal.open(kA, 0, 100, 100, true), "A failed to open");
  require(normal.renew(kA, 200, 250), "current renewal rejected");
  require(normal.deadline_ns() == 1200, "receipt time extended deadline");
  require(normal.open(kA, 0, 100, 300, true), "exact open retry rejected");
  require(normal.deadline_ns() == 1200, "open retry extended deadline");
  require(!normal.renew(kA, 200, 300), "duplicate renewed");
  require(!normal.renew(kB, 350, 350), "wrong identity renewed");
  require(!normal.renew(kA, 401, 400), "future timestamp renewed");
  require(normal.close(kA, 500), "normal close rejected");
  require(!normal.can_consume(kA, 501), "closed A consumed");
  require(!normal.open(kB, 1, 502, 502, false), "B opened before zero application");
  require(normal.open(kB, 1, 503, 503, true), "B failed normal handoff");
  require(!normal.can_consume(kA, 504), "A identity affected B");
  require(normal.can_consume(kB, 505), "current B rejected");

  MotionPermission expiry(1000);
  require(expiry.open(kA, 0, 100, 100, true), "expiry setup failed");
  require(expiry.can_consume(kA, 1099), "premature expiry");
  require(!expiry.renew(kA, 1100, 1100), "renewal won at exact old deadline");
  require(!expiry.can_consume(kA, 1101), "expired command consumed");
  require(expiry.close(kA, 1102), "idempotent fault close rejected");
  require(!expiry.open(kB, 1, 1103, 1103, true), "new generation cleared fault latch");

  MotionPermission queued(1000);
  require(queued.open(kA, 0, 100, 100, true), "queue setup failed");
  require(queued.renew(kA, 200, 1050), "valid delayed renewal rejected");
  require(queued.deadline_ns() == 1200, "queued renewal received a fresh lifetime");
  require(!queued.can_consume(kA, 1200), "delayed renewal extended lifetime");

  MotionPermission stale(1000);
  require(!stale.open(kA, 0, 100, 1100, true), "expired initial grant opened");
  require(!stale.open(kA, 0, std::numeric_limits<std::uint64_t>::max() - 500,
                      std::numeric_limits<std::uint64_t>::max() - 500, true),
          "overflow initial grant opened");

  MotionPermission reversal(1000);
  require(reversal.open(kA, 0, 100, 100, true), "clock setup failed");
  require(!reversal.can_consume(kA, 99), "clock reversal allowed motion");
  MotionPermission fields(1000);
  require(fields.open(kA, 0, 100, 100, true), "identity setup failed");
  std::uint64_t now = 100;
  for (const auto& wrong : {MotionPermitIdentity{std::string(32, 'b'), 1, 1},
                            MotionPermitIdentity{std::string(32, 'a'), 2, 1},
                            MotionPermitIdentity{std::string(32, 'a'), 1, 2}}) {
    ++now;
    require(!fields.renew(wrong, now, now), "one-field mismatch renewed");
    require(!fields.can_consume(wrong, ++now), "one-field mismatch consumed");
    require(!fields.close(wrong, ++now), "one-field mismatch closed current grant");
    require(fields.can_consume(kA, ++now) && fields.deadline_ns() == 1100,
            "rejected identity changed current grant");
  }
  std::cout << "{\"event\":\"contract_checks_passed\",\"groups\":6}" << std::endl;
}

struct Fixture {
  MotionPermission permission{1000};
  MotionPermitIdentity identity{std::string(32, 'a'), 1, 7};
  std::uint64_t now{100};
  std::array<double, 2> targets{0, 0};
  int nonzero_calls{0};
  bool zero_applied{false};
  bool all_zero_writes_succeed{true};

  Fixture() {
    require(permission.open(identity, 0, now, now, true), "fixture open");
  }

  bool write(int wheel) {
    return write_current(
        permission, identity, [this] { return now; },
        [this](std::uint64_t) {
          targets.fill(0);
          // The native path must retain actual zero-write
          // acceptance; closure alone cannot assert application.
          zero_applied = all_zero_writes_succeed;
        },
        [this, wheel] {
          require(now < permission.deadline_ns(), "nonzero native call admitted after expiry");
          targets[wheel] = 0.4;
          ++nonzero_calls;
        });
  }
};

void boundary_checks() {
  // Controlled causal trace of the previous check-before-calculation mechanism;
  // this is not a reproduction of a measured real Gazebo scheduling stall.
  Fixture old_path;
  const bool cached_allowed = old_path.permission.can_consume(old_path.identity, old_path.now);
  old_path.now = 1100;  // Calculation/native-call delay crosses the cutoff.
  require(cached_allowed && old_path.now >= old_path.permission.deadline_ns(),
          "old mechanism must leave a stale admission decision");
  std::cout << "old-check-before-calculation: stale admission reproduced\n";

  Fixture calculation;
  require(calculation.permission.can_consume(calculation.identity, 100),
          "initial check must be valid");
  calculation.now = 1100;
  require(!calculation.write(0) && calculation.nonzero_calls == 0 && calculation.zero_applied &&
              calculation.permission.state() == MotionPermission::State::kFaultClosed,
          "calculation expiry must apply zero, never cached nonzero");

  Fixture between_wheels;
  require(between_wheels.write(0), "first wheel should be admitted while valid");
  between_wheels.now = 1100;
  require(!between_wheels.write(1) && between_wheels.nonzero_calls == 1 &&
              between_wheels.targets[0] == 0 && between_wheels.targets[1] == 0 &&
              between_wheels.zero_applied,
          "between-wheel expiry must zero every wheel and refuse the next call");
  require(!between_wheels.permission.renew(between_wheels.identity, 1101, 1101),
          "expired final write must retain the fault latch");

  Fixture failed_zero;
  failed_zero.now = 1100;
  failed_zero.all_zero_writes_succeed = false;
  require(!failed_zero.write(0) && !failed_zero.zero_applied,
          "failed actual zero write cannot assert application");
  std::cout << "final-write boundary: calculation / between wheels / zero failure passed\n";
}

}  // namespace

int main() {
  deterministic_checks();
  boundary_checks();
}
