#include "zeroerr.hpp"
#include "ECS.hpp"

namespace dc
{
class Node : public ecs::Entity
{
public:
  ENTITY(Node, ecs::Entity)
  void release() override { ecs::DestroyEntity(this); }

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
} // namespace dc

static int count_nodes(ecs::Table &table)
{
  int n = 0;
  for (auto [p] : ecs::View<dc::Node, dc::Node::Pos>(table))
  {
    (void)p;
    ++n;
  }
  return n;
}

static int sum_pos(ecs::Table &table)
{
  int s = 0;
  for (auto [p] : ecs::View<dc::Node, dc::Node::Pos>(table))
    s += p->x;
  return s;
}

TEST_CASE("ScopedDefer create invisible until commit")
{
  ecs::Table table;
  dc::Node::create(table);
  REQUIRE(count_nodes(table) == 1);

  dc::Node *staged = nullptr;
  {
    ecs::ScopedDefer guard(table);
    staged = dc::Node::create(table);
    staged->pos()->x = 42;
    REQUIRE(count_nodes(table) == 1);
    REQUIRE(staged->pos()->x == 42);
  }
  REQUIRE(count_nodes(table) == 2);

  int seen = 0;
  for (auto [p] : ecs::View<dc::Node, dc::Node::Pos>(table))
  {
    if (p->x == 42)
      ++seen;
  }
  REQUIRE(seen == 1);
}

TEST_CASE("CreateEntity during View not seen until View ends")
{
  ecs::Table table;
  dc::Node::create(table);
  dc::Node::create(table);

  int visited = 0;
  {
    ecs::View<dc::Node, dc::Node::Pos> view(table);
    for (auto it = view.begin(); it != view.end(); ++it)
    {
      auto [p] = *it;
      (void)p;
      if (visited == 0)
        dc::Node::create(table)->pos()->x = 99;
      ++visited;
    }
    REQUIRE(visited == 2);
  }
  REQUIRE(count_nodes(table) == 3);
}

TEST_CASE("DestroyEntity + generation invalidates handle")
{
  ecs::Table table;
  dc::Node *e = dc::Node::create(table);
  e->pos()->x = 7;
  auto h = ecs::handle_of(e);
  REQUIRE(ecs::try_get(h) != nullptr);

  ecs::DestroyEntity(e);
  REQUIRE(ecs::try_get(h) == nullptr);

  dc::Node *reuse = dc::Node::create(table);
  REQUIRE(reuse->id == h.id);
  REQUIRE(reuse->generation != h.generation);
  auto h2 = ecs::handle_of(reuse);
  REQUIRE(ecs::try_get(h) == nullptr);
  REQUIRE(ecs::try_get(h2) == reuse);
}

TEST_CASE("Destroy during View skips tombstone; gone after commit")
{
  ecs::Table table;
  dc::Node *a = dc::Node::create(table);
  dc::Node *b = dc::Node::create(table);
  a->pos()->x = 1;
  b->pos()->x = 2;

  int visited = 0;
  int sum = 0;
  {
    ecs::View<dc::Node, dc::Node::Pos> view(table);
    ecs::DestroyEntity(a);
    for (auto it = view.begin(); it != view.end(); ++it)
    {
      auto [p] = *it;
      sum += p->x;
      ++visited;
    }
  }
  REQUIRE(visited == 1);
  REQUIRE(sum == 2);
  REQUIRE(count_nodes(table) == 1);
}

TEST_CASE("nested defer commits only at outermost end")
{
  ecs::Table table;
  {
    ecs::ScopedDefer outer(table);
    {
      ecs::ScopedDefer inner(table);
      dc::Node::create(table);
      REQUIRE(count_nodes(table) == 0);
    }
    REQUIRE(count_nodes(table) == 0);
    dc::Node::create(table);
    REQUIRE(count_nodes(table) == 0);
  }
  REQUIRE(count_nodes(table) == 2);
}

TEST_CASE("deferred create then destroy never publishes")
{
  ecs::Table table;
  {
    ecs::ScopedDefer guard(table);
    dc::Node *e = dc::Node::create(table);
    e->pos()->x = 5;
    ecs::DestroyEntity(e);
  }
  REQUIRE(count_nodes(table) == 0);
}

TEST_CASE("manual commit while defer stays open")
{
  ecs::Table table;
  ecs::begin_defer(table);
  dc::Node::create(table);
  REQUIRE(count_nodes(table) == 0);
  ecs::commit(table);
  REQUIRE(count_nodes(table) == 1);
  dc::Node::create(table);
  REQUIRE(count_nodes(table) == 1);
  ecs::end_defer(table);
  REQUIRE(count_nodes(table) == 2);
}

TEST_CASE("DestroyEntity(nullptr) is no-op")
{
  ecs::DestroyEntity(nullptr);
  REQUIRE(true);
}

TEST_CASE("double DestroyEntity is no-op")
{
  ecs::Table table;
  dc::Node *e = dc::Node::create(table);
  auto h = ecs::handle_of(e);
  ecs::DestroyEntity(e);
  ecs::DestroyEntity(e);
  REQUIRE(ecs::try_get(h) == nullptr);
  REQUIRE(count_nodes(table) == 0);
}

TEST_CASE("Entity::release destroys entity")
{
  ecs::Table table;
  dc::Node *e = dc::Node::create(table);
  auto h = ecs::handle_of(e);
  e->release();
  REQUIRE(ecs::try_get(h) == nullptr);
  REQUIRE(count_nodes(table) == 0);
}

TEST_CASE("nested Views commit only when outermost ends")
{
  ecs::Table table;
  dc::Node::create(table)->pos()->x = 1;
  {
    ecs::View<dc::Node, dc::Node::Pos> outer(table);
    {
      ecs::View<dc::Node, dc::Node::Pos> inner(table);
      dc::Node::create(table)->pos()->x = 2;
      int n = 0;
      for (auto it = inner.begin(); it != inner.end(); ++it)
        ++n;
      REQUIRE(n == 1);
    }
    // inner ended but outer still holds defer
    int n = 0;
    for (auto it = outer.begin(); it != outer.end(); ++it)
      ++n;
    REQUIRE(n == 1);
  }
  REQUIRE(count_nodes(table) == 2);
  REQUIRE(sum_pos(table) == 3);
}

TEST_CASE("deferred Sprite publish visible to View<Node>")
{
  ecs::Table table;
  dc::Node::create(table)->pos()->x = 10;
  {
    ecs::ScopedDefer guard(table);
    dc::Sprite *s = dc::Sprite::create(table);
    s->pos()->x = 20;
    s->img()->w = 64;
    REQUIRE(count_nodes(table) == 1);
  }
  REQUIRE(count_nodes(table) == 2);
  REQUIRE(sum_pos(table) == 30);

  int sprites = 0;
  for (auto [img] : ecs::View<dc::Sprite, dc::Sprite::Img>(table))
  {
    REQUIRE(img->w == 64);
    ++sprites;
  }
  REQUIRE(sprites == 1);
}

TEST_CASE("deferred subclass-only publish links into empty base View")
{
  ecs::Table table;
  {
    ecs::ScopedDefer guard(table);
    dc::Sprite *s = dc::Sprite::create(table);
    s->pos()->x = 20;
    s->img()->w = 32;
    REQUIRE(count_nodes(table) == 0);
  }
  REQUIRE(count_nodes(table) == 1);
  REQUIRE(sum_pos(table) == 20);
}

TEST_CASE("defer on one Table does not affect another")
{
  ecs::Table a;
  ecs::Table b;
  {
    ecs::ScopedDefer guard(a);
    dc::Node::create(a)->pos()->x = 1;
    dc::Node::create(b)->pos()->x = 2;
    REQUIRE(count_nodes(a) == 0);
    REQUIRE(count_nodes(b) == 1);
  }
  REQUIRE(count_nodes(a) == 1);
  REQUIRE(count_nodes(b) == 1);
  REQUIRE(sum_pos(a) == 1);
  REQUIRE(sum_pos(b) == 2);
}

TEST_CASE("try_get rejects mismatched generation and dead slots")
{
  ecs::Table table;
  dc::Node *e = dc::Node::create(table);
  auto h = ecs::handle_of(e);
  auto bad = h;
  bad.generation = h.generation + 100;
  REQUIRE(ecs::try_get(bad) == nullptr);

  ecs::DestroyEntity(e);
  REQUIRE(ecs::try_get(h) == nullptr);

  ecs::EntityHandle empty;
  REQUIRE(ecs::try_get(empty) == nullptr);
}

TEST_CASE("deferred destroy then freelist reuse after commit")
{
  ecs::Table table;
  dc::Node *a = dc::Node::create(table);
  dc::Node *b = dc::Node::create(table);
  a->pos()->x = 1;
  b->pos()->x = 2;
  auto ha = ecs::handle_of(a);
  const uint32_t aid = a->id;

  {
    ecs::ScopedDefer guard(table);
    ecs::DestroyEntity(a);
    REQUIRE(count_nodes(table) == 1);
  }

  REQUIRE(ecs::try_get(ha) == nullptr);
  dc::Node *reuse = dc::Node::create(table);
  REQUIRE(reuse->id == aid);
  REQUIRE(reuse->generation != ha.generation);
  reuse->pos()->x = 9;
  REQUIRE(count_nodes(table) == 2);
  REQUIRE(sum_pos(table) == 11);
}

TEST_CASE("create and destroy multiple during one View")
{
  ecs::Table table;
  dc::Node *keep = dc::Node::create(table);
  dc::Node *drop = dc::Node::create(table);
  keep->pos()->x = 5;
  drop->pos()->x = 6;

  {
    ecs::View<dc::Node, dc::Node::Pos> view(table);
    ecs::DestroyEntity(drop);
    dc::Node::create(table)->pos()->x = 7;
    dc::Node::create(table)->pos()->x = 8;

    int n = 0;
    int s = 0;
    for (auto it = view.begin(); it != view.end(); ++it)
    {
      auto [p] = *it;
      s += p->x;
      ++n;
    }
    REQUIRE(n == 1);
    REQUIRE(s == 5);
  }

  REQUIRE(count_nodes(table) == 3);
  REQUIRE(sum_pos(table) == 5 + 7 + 8);
}

TEST_CASE("empty commit is no-op")
{
  ecs::Table table;
  dc::Node::create(table)->pos()->x = 3;
  ecs::commit(table);
  REQUIRE(count_nodes(table) == 1);
  REQUIRE(sum_pos(table) == 3);
}

TEST_CASE("staging component writes survive publish")
{
  ecs::Table table;
  {
    ecs::ScopedDefer guard(table);
    dc::Node *e = dc::Node::create(table);
    e->pos()->x = 11;
    e->pos()->x = 22;
    REQUIRE(e->pos()->x == 22);
  }
  REQUIRE(count_nodes(table) == 1);
  REQUIRE(sum_pos(table) == 22);
}

TEST_CASE("View destroy current entity mid-loop continues safely")
{
  ecs::Table table;
  dc::Node *a = dc::Node::create(table);
  dc::Node *b = dc::Node::create(table);
  dc::Node *c = dc::Node::create(table);
  a->pos()->x = 1;
  b->pos()->x = 2;
  c->pos()->x = 3;

  int visited = 0;
  int sum = 0;
  {
    ecs::View<dc::Node, dc::Node::Pos> view(table);
    for (auto it = view.begin(); it != view.end(); ++it)
    {
      auto [p] = *it;
      sum += p->x;
      ++visited;
      if (visited == 1)
        ecs::DestroyEntity(a);
    }
  }
  REQUIRE(visited == 3);
  REQUIRE(sum == 6);
  REQUIRE(count_nodes(table) == 2);
  REQUIRE(sum_pos(table) == 5);
}
