#include "zeroerr.hpp"
#include "ECS.hpp"

TEST_CASE("default current is default_table")
{
  REQUIRE(ecs::current() == &ecs::default_table());
}

TEST_CASE("set_current and ScopedTable restore")
{
  ecs::Table a;
  ecs::Table b;
  ecs::set_current(&a);
  REQUIRE(ecs::current() == &a);
  {
    ecs::ScopedTable guard(b);
    REQUIRE(ecs::current() == &b);
  }
  REQUIRE(ecs::current() == &a);
  ecs::set_current(nullptr);
  REQUIRE(ecs::current() == &ecs::default_table());
}

namespace tw
{
class Node : public ecs::Entity
{
public:
  ENTITY(Node, ecs::Entity)
  void release() override {}
  struct Pos
  {
    int x = 0;
  };
  COMPONENT(Pos, pos)
};

class Sprite : public Node
{
public:
  ENTITY(Sprite, Node)
  struct Img
  {
    int w = 0;
  };
  COMPONENT(Img, img)
};
} // namespace tw

TEST_CASE("CreateEntity into explicit tables isolates entities")
{
  ecs::Table a;
  ecs::Table b;
  tw::Node *na = tw::Node::create(a);
  tw::Node *nb = tw::Node::create(b);
  na->pos()->x = 1;
  nb->pos()->x = 2;
  REQUIRE(na->table == &a);
  REQUIRE(nb->table == &b);
  REQUIRE(na->pos()->x == 1);
  REQUIRE(nb->pos()->x == 2);
  REQUIRE(a.getManager<tw::Node>()->registy->size() == 1);
  REQUIRE(b.getManager<tw::Node>()->registy->size() == 1);
}

TEST_CASE("View(table) does not see other table entities")
{
  ecs::Table a;
  ecs::Table b;
  tw::Node::create(a)->pos()->x = 10;
  tw::Node::create(b)->pos()->x = 20;

  int count_a = 0, sum_a = 0;
  for (auto [p] : ecs::View<tw::Node, tw::Node::Pos>(a))
  {
    ++count_a;
    sum_a += p->x;
  }
  REQUIRE(count_a == 1);
  REQUIRE(sum_a == 10);

  int count_b = 0, sum_b = 0;
  for (auto [p] : ecs::View<tw::Node, tw::Node::Pos>(b))
  {
    ++count_b;
    sum_b += p->x;
  }
  REQUIRE(count_b == 1);
  REQUIRE(sum_b == 20);
}

TEST_CASE("no-arg View follows current table")
{
  ecs::Table a;
  ecs::Table b;
  tw::Node::create(a);
  tw::Node::create(b);
  ecs::set_current(&a);
  int n = 0;
  for (auto [p] : ecs::View<tw::Node, tw::Node::Pos>())
  {
    (void)p;
    ++n;
  }
  REQUIRE(n == 1);
  ecs::set_current(nullptr);
}

TEST_CASE("ComponentRef stays on owning table after current switches")
{
  ecs::Table a;
  ecs::Table b;
  tw::Node *na = tw::Node::create(a);
  na->pos()->x = 7;
  ecs::set_current(&b);
  tw::Node::create(b);
  REQUIRE(na->pos()->x == 7);
  na->pos()->x = 8;
  REQUIRE(na->pos()->x == 8);
  ecs::set_current(nullptr);
}

TEST_CASE("same-table View<Node> includes Sprite; other table excluded")
{
  ecs::Table a;
  ecs::Table b;
  tw::Node::create(a);
  tw::Sprite::create(a);
  tw::Sprite::create(b);

  int n = 0;
  for (auto [p] : ecs::View<tw::Node, tw::Node::Pos>(a))
  {
    (void)p;
    ++n;
  }
  REQUIRE(n == 2);
}

TEST_CASE("CreateEntity() uses current table")
{
  ecs::Table a;
  ecs::set_current(&a);
  tw::Node *n = tw::Node::create();
  REQUIRE(n->table == &a);
  ecs::set_current(nullptr);
}

TEST_CASE("explicit CreateEntity(B) while current is A")
{
  ecs::Table a;
  ecs::Table b;
  ecs::set_current(&a);
  tw::Node *n = tw::Node::create(b);
  REQUIRE(n->table == &b);
  int n_a = 0;
  for (auto [p] : ecs::View<tw::Node, tw::Node::Pos>())
  {
    (void)p;
    ++n_a;
  }
  REQUIRE(n_a == 0);
  ecs::set_current(nullptr);
}
