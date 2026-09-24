// Reproduce the pinned BT v3 control-root lifecycle after interrupted
// execution.
#include <behaviortree_cpp_v3/bt_factory.h>
#include <stdexcept>

class HeldAction : public BT::StatefulActionNode {
public:
  HeldAction(const std::string &name, const BT::NodeConfiguration &config)
      : StatefulActionNode(name, config) {}
  static BT::PortsList providedPorts() { return {}; }
  BT::NodeStatus onStart() override { return BT::NodeStatus::RUNNING; }
  BT::NodeStatus onRunning() override { return BT::NodeStatus::RUNNING; }
  void onHalted() override { ++halt_count; }
  static unsigned halt_count;
};
unsigned HeldAction::halt_count = 0;

int main() {
  BT::BehaviorTreeFactory factory;
  factory.registerNodeType<HeldAction>("HeldAction");
  auto tree = factory.createTreeFromText(
      "<root main_tree_to_execute=\"Main\"><BehaviorTree ID=\"Main\">"
      "<Sequence><AlwaysSuccess/><HeldAction/></Sequence></BehaviorTree></"
      "root>");
  if (tree.tickRoot() != BT::NodeStatus::RUNNING)
    throw std::runtime_error("fixture did not enter active execution");
  // Exactly the operation exposed by Humble BtActionServer::haltTree().
  tree.rootNode()->halt();
  if (HeldAction::halt_count != 1 ||
      tree.rootNode()->status() != BT::NodeStatus::RUNNING)
    throw std::runtime_error("pinned root-only halt premise changed");
  tree.haltTree();
  for (const auto &node : tree.nodes) {
    if (node->status() != BT::NodeStatus::IDLE)
      throw std::runtime_error("full native tree halt did not reset nodes");
  }
  if (HeldAction::halt_count != 1)
    throw std::runtime_error(
        "full halt repeated already-ended asynchronous work");
}
