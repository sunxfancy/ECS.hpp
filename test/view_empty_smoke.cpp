#include "ECS.hpp"

#include <cassert>
#include <cstdio>

class Node : public ecs::Entity
{
public:
  ENTITY(Node, ecs::Entity)

  void release() override {}

  struct Velocity
  {
    float dx = 1;
    float dy = 1;
  };

  COMPONENT(Velocity, velocity)
};

int main()
{
  // Regression: constructing View before any CreateEntity must not crash.
  {
    auto view = ecs::View<Node, Node::Velocity>();
    assert(view.begin() == view.end());
    assert(ecs::ComponentManager<Node>::inst().registy != nullptr);
  }

  Node *a = Node::create();
  Node *b = Node::create();
  assert(a != nullptr);
  assert(b != nullptr);

  int count = 0;
  auto view = ecs::View<Node, Node::Velocity>();
  for (auto [v] : view)
  {
    assert(v != nullptr);
    v->dx += 1;
    ++count;
  }
  assert(count == 2);
  assert(a->velocity()->dx == 2);
  assert(b->velocity()->dx == 2);

  std::puts("view_empty_smoke: ok");
  return 0;
}
