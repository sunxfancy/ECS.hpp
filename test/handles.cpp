#include "zeroerr.hpp"
#include "ECS.hpp"

#include <cstdint>

namespace hd
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

class Other : public ecs::Entity
{
public:
  ENTITY(Other, ecs::Entity)
  void release() override { ecs::DestroyEntity(this); }

  struct Tag
  {
    int v = 0;
  };
  COMPONENT(Tag, tag)
};
} // namespace hd

TEST_CASE("handle_of nullptr yields empty handle")
{
  auto h = ecs::handle_of(nullptr);
  REQUIRE(h.table == nullptr);
  REQUIRE(h.id == 0);
  REQUIRE(h.generation == 0);
  REQUIRE(ecs::try_get(h) == nullptr);
}

TEST_CASE("handle_of captures owning table type id generation")
{
  ecs::Table table;
  hd::Node *e = hd::Node::create(table);
  e->pos()->x = 3;
  auto h = ecs::handle_of(e);
  REQUIRE(h.table == &table);
  REQUIRE(h.id == e->id);
  REQUIRE(h.generation == e->generation);
  REQUIRE(h.type == std::type_index(typeid(hd::Node)));
  REQUIRE(ecs::try_get(h) == e);
}

TEST_CASE("try_get rejects wrong table")
{
  ecs::Table a;
  ecs::Table b;
  hd::Node *e = hd::Node::create(a);
  auto h = ecs::handle_of(e);
  h.table = &b;
  REQUIRE(ecs::try_get(h) == nullptr);
}

TEST_CASE("try_get rejects type with no manager")
{
  ecs::Table table;
  hd::Node *e = hd::Node::create(table);
  auto h = ecs::handle_of(e);
  // Other was never created in this table — no manager for that type.
  h.type = std::type_index(typeid(hd::Other));
  REQUIRE(ecs::try_get(h) == nullptr);
}

TEST_CASE("try_get with wrong type may resolve a different entity slot")
{
  ecs::Table table;
  hd::Node *n = hd::Node::create(table);
  hd::Other *o = hd::Other::create(table);
  auto h = ecs::handle_of(n);
  h.type = std::type_index(typeid(hd::Other));
  // Same numeric id exists under Other; try_get is type-keyed, not "reject
  // if original entity type differs" — it returns the Other at that slot.
  REQUIRE(ecs::try_get(h) == o);
}

TEST_CASE("try_get rejects out-of-range id")
{
  ecs::Table table;
  hd::Node *e = hd::Node::create(table);
  auto h = ecs::handle_of(e);
  h.id = 9999;
  REQUIRE(ecs::try_get(h) == nullptr);
}

TEST_CASE("freelist reuse resets component slots")
{
  ecs::Table table;
  hd::Node *a = hd::Node::create(table);
  a->pos()->x = 42;
  const uint32_t aid = a->id;
  ecs::DestroyEntity(a);

  hd::Node *b = hd::Node::create(table);
  REQUIRE(b->id == aid);
  REQUIRE(b->pos()->x == 0);
  b->pos()->x = 7;
  REQUIRE(b->pos()->x == 7);
}

TEST_CASE("generation starts at 1 and bumps on destroy")
{
  ecs::Table table;
  hd::Node *e = hd::Node::create(table);
  REQUIRE(e->generation == 1);
  auto h1 = ecs::handle_of(e);
  ecs::DestroyEntity(e);

  hd::Node *reuse = hd::Node::create(table);
  REQUIRE(reuse->id == h1.id);
  REQUIRE(reuse->generation == 2);
  REQUIRE(ecs::try_get(h1) == nullptr);
  REQUIRE(ecs::try_get(ecs::handle_of(reuse)) == reuse);
}

TEST_CASE("staging handle is not remapped through commit")
{
  ecs::Table table;
  hd::Node *live = hd::Node::create(table);
  live->pos()->x = 1;
  auto live_handle = ecs::handle_of(live);

  ecs::EntityHandle staged_handle;
  hd::Node *staged_ptr = nullptr;
  {
    ecs::ScopedDefer guard(table);
    staged_ptr = hd::Node::create(table);
    staged_ptr->pos()->x = 11;
    staged_handle = ecs::handle_of(staged_ptr);
    REQUIRE(staged_handle.id == live_handle.id);
    // try_get only inspects main storage. Staging ids are local, so a colliding
    // id/generation resolves to the live main entity — never the staging pointer.
    REQUIRE(ecs::try_get(staged_handle) == live);
    REQUIRE(ecs::try_get(staged_handle) != staged_ptr);
  }

  // v1: commit does not rewrite staging ids onto the handle.
  REQUIRE(ecs::try_get(staged_handle) == live);
  REQUIRE(ecs::try_get(live_handle) == live);

  int n = 0;
  int sum = 0;
  for (auto [p] : ecs::View<hd::Node, hd::Node::Pos>(table))
  {
    sum += p->x;
    ++n;
  }
  REQUIRE(n == 2);
  REQUIRE(sum == 12);
}

TEST_CASE("is_entity_visible hides dead and staging")
{
  ecs::Table table;
  hd::Node *live = hd::Node::create(table);
  REQUIRE(ecs::is_entity_visible(live));

  {
    ecs::ScopedDefer guard(table);
    hd::Node *staged = hd::Node::create(table);
    REQUIRE_NOT(ecs::is_entity_visible(staged));
    REQUIRE((staged->flags & ecs::kEntityStaging) != 0u);
  }

  ecs::DestroyEntity(live);
  REQUIRE_NOT(ecs::is_entity_visible(live));
  REQUIRE(ecs::is_entity_visible(nullptr) == false);
}

TEST_CASE("no-arg begin_defer end_defer use current table")
{
  ecs::Table table;
  ecs::set_current(&table);
  {
    ecs::begin_defer();
    hd::Node::create()->pos()->x = 5;
    int n = 0;
    for (auto [p] : ecs::View<hd::Node, hd::Node::Pos>())
    {
      (void)p;
      ++n;
    }
    REQUIRE(n == 0);
    ecs::end_defer();
  }
  int n = 0;
  for (auto [p] : ecs::View<hd::Node, hd::Node::Pos>())
  {
    REQUIRE(p->x == 5);
    ++n;
  }
  REQUIRE(n == 1);
  ecs::set_current(nullptr);
}

TEST_CASE("ScopedDefer without args uses current table")
{
  ecs::Table table;
  ecs::set_current(&table);
  {
    ecs::ScopedDefer guard;
    hd::Node::create()->pos()->x = 9;
    REQUIRE(table.is_deferred());
  }
  REQUIRE_NOT(table.is_deferred());
  int n = 0;
  for (auto [p] : ecs::View<hd::Node, hd::Node::Pos>(table))
  {
    REQUIRE(p->x == 9);
    ++n;
  }
  REQUIRE(n == 1);
  ecs::set_current(nullptr);
}

TEST_CASE("nested ScopedTable restores previous current")
{
  ecs::Table a;
  ecs::Table b;
  ecs::Table c;
  ecs::set_current(&a);
  {
    ecs::ScopedTable outer(b);
    REQUIRE(ecs::current() == &b);
    {
      ecs::ScopedTable inner(c);
      REQUIRE(ecs::current() == &c);
      hd::Node *n = hd::Node::create();
      REQUIRE(n->table == &c);
    }
    REQUIRE(ecs::current() == &b);
  }
  REQUIRE(ecs::current() == &a);
  ecs::set_current(nullptr);
}

TEST_CASE("Sprite handle resolves via Sprite manager type")
{
  ecs::Table table;
  hd::Sprite *s = hd::Sprite::create(table);
  s->pos()->x = 1;
  s->img()->w = 32;
  auto h = ecs::handle_of(s);
  REQUIRE(h.type == std::type_index(typeid(hd::Sprite)));
  auto *got = ecs::try_get(h);
  REQUIRE(got == s);
  REQUIRE(static_cast<hd::Sprite *>(got)->img()->w == 32);
}
