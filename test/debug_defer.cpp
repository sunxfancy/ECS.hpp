#include "ECS.hpp"
#include <iostream>

namespace dc
{
class Node : public ecs::Entity
{
public:
  ENTITY(Node, ecs::Entity)
  void release() override { ecs::DestroyEntity(this); }
  struct Pos { int x = 0; };
  COMPONENT(Pos, pos)
};
}

int main()
{
  ecs::Table table;
  dc::Node::create(table);
  std::cout << "created\n";
  int n = 0;
  for (auto [p] : ecs::View<dc::Node, dc::Node::Pos>(table))
  {
    (void)p;
    ++n;
  }
  std::cout << "count=" << n << "\n";
  return 0;
}
