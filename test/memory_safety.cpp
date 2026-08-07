// Memory-safety contract tests for ECS.hpp.
//
// Naming:
//   [char]   — characterizes CURRENT behavior (passes today; flip when fixing)
//   [target] — desired SAFE behavior (may fail / crash today; drives the fix)
//
// Isolated entity types keep their Table-owned managers independent of main.cpp.

#include "zeroerr.hpp"
#include "ECS.hpp"

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace ms
{

class Base : public ecs::Entity
{
public:
  ENTITY(Base, ecs::Entity)

  void release() override {}

  struct Tag
  {
    int v = 0;
  };

  COMPONENT(Tag, tag)
};

class Child : public Base
{
public:
  ENTITY(Child, Base)

  struct Extra
  {
    int e = 0;
  };

  COMPONENT(Extra, extra)
};

class Other : public ecs::Entity
{
public:
  ENTITY(Other, ecs::Entity)

  void release() override {}
};

} // namespace ms

// ---------------------------------------------------------------------------
// Characterization: document current behavior as evidence
// ---------------------------------------------------------------------------

TEST_CASE("[char] CreateEntity returns stable pointers across more creates")
{
  // std::deque: references/pointers to elements stay valid when pushing at ends.
  // ComponentRef / CreateEntity currently rely on this.
  ms::Base *a = ms::Base::create();
  ms::Base *b = ms::Base::create();
  a->tag()->v = 11;
  b->tag()->v = 22;

  std::vector<ms::Base *> keep;
  keep.push_back(a);
  keep.push_back(b);
  for (int i = 0; i < 64; ++i)
    keep.push_back(ms::Base::create());

  REQUIRE(keep[0] == a);
  REQUIRE(keep[1] == b);
  REQUIRE(a->tag()->v == 11);
  REQUIRE(b->tag()->v == 22);
}

TEST_CASE("[char] getEntity out-of-range silently grows registry")
{
  // Evidence for P1: read path invents default entities.
  // WHEN FIXING: invert — size must stay unchanged; prefer try_get → nullptr.
  auto &cm = ecs::current()->getOrCreateManager<ms::Base>();
  auto *reg = cm.template getOrCreateRegistryComponentBuffer<ms::Base>();
  const uint32_t before = reg->size();
  const uint32_t ghost_id = before + 40;

  ecs::Entity *ghost = reg->getEntity(ghost_id);

  REQUIRE(ghost != nullptr);
  REQUIRE(reg->size() == ghost_id + 1);
  REQUIRE(ghost->id == 0); // default-constructed slot; id never assigned
}

TEST_CASE("[char] ComponentBuffer::get out-of-range silently grows")
{
  auto &cm = ecs::current()->getOrCreateManager<ms::Base>();
  auto *buf = cm.template getOrCreateComponentBuffer<ms::Base::Tag>();
  const uint32_t before = buf->size();
  const uint32_t ghost_id = before + 25;

  ms::Base::Tag &slot = buf->get(ghost_id);

  REQUIRE(buf->size() == ghost_id + 1);
  REQUIRE(slot.v == 0);
}

TEST_CASE("[char] EntityIterator== throws bad_cast on type mismatch")
{
  // Evidence for P2: equality uses reference dynamic_cast → exception, not false.
  // WHEN FIXING: pointer cast, mismatch → false.
  auto &cm_a = ecs::current()->getOrCreateManager<ms::Base>();
  auto &cm_b = ecs::current()->getOrCreateManager<ms::Other>();
  auto *reg_a = cm_a.template getOrCreateRegistryComponentBuffer<ms::Base>();
  auto *reg_b = cm_b.template getOrCreateRegistryComponentBuffer<ms::Other>();

  auto it_a = reg_a->beginEntity();
  auto it_b = reg_b->beginEntity();

  bool threw = false;
  try
  {
    (void)(*it_a == *it_b);
  }
  catch (const std::bad_cast &)
  {
    threw = true;
  }
  REQUIRE(threw);
}

// ---------------------------------------------------------------------------
// Target contracts: desired memory-safe behavior (red until fixed)
// ---------------------------------------------------------------------------

TEST_CASE("[target] getEntity out-of-range must not invent entities")
{
  auto &cm = ecs::current()->getOrCreateManager<ms::Child>();
  auto *reg = cm.template getOrCreateRegistryComponentBuffer<ms::Child>();
  ms::Child::create();
  const uint32_t before = reg->size();

  // Desired safe read API (not yet present). Today getEntity grows — this fails.
  // After fix: either try_getEntity returns nullptr, or getEntity asserts / throws
  // without changing size. We probe via size invariance after a would-be OOB read.
  //
  // Interim: call getEntity and require size unchanged (safe contract).
  (void)reg->getEntity(before + 10);
  REQUIRE(reg->size() == before);
}

TEST_CASE("[target] ComponentBuffer::get out-of-range must not invent components")
{
  auto &cm = ecs::current()->getOrCreateManager<ms::Child>();
  auto *buf = cm.template getOrCreateComponentBuffer<ms::Child::Extra>();
  ms::Child::create();
  const uint32_t before = buf->size();

  (void)buf->get(before + 10);
  REQUIRE(buf->size() == before);
}

TEST_CASE("[target] View zip keeps component values aligned with entity id")
{
  // Each entity writes a unique tag.v == id. View must yield matching pairs.
  // Uses fresh Child entities so Base/Child registry tree is exercised.
  std::vector<ms::Child *> kids;
  for (int i = 0; i < 5; ++i)
  {
    ms::Child *c = ms::Child::create();
    c->tag()->v = static_cast<int>(c->id);
    c->extra()->e = static_cast<int>(c->id) * 10;
    kids.push_back(c);
  }

  // Also create a Base-only entity — View<Base, Tag> should still align.
  ms::Base *only = ms::Base::create();
  only->tag()->v = static_cast<int>(only->id);

  int seen = 0;
  auto view = ecs::View<ms::Base, ms::Base::Tag>();
  for (auto it = view.begin(); it != view.end(); ++it)
  {
    auto [tag] = *it;
    REQUIRE(tag != nullptr);
    // Desired: tag->v equals the owning entity's id. Without entity in the tuple
    // we can only check self-consistency of Tag; pair with registry by index.
    // Stronger check: every created entity's tag matches when looked up by id.
    ++seen;
  }
  REQUIRE(seen >= static_cast<int>(kids.size()) + 1);

  for (ms::Child *c : kids)
  {
    REQUIRE(c->tag()->v == static_cast<int>(c->id));
    REQUIRE(c->extra()->e == static_cast<int>(c->id) * 10);
  }

  // Zip alignment via parallel View of Tag + Extra on Child:
  auto view2 = ecs::View<ms::Child, ms::Base::Tag, ms::Child::Extra>();
  for (auto it = view2.begin(); it != view2.end(); ++it)
  {
    auto [tag, extra] = *it;
    REQUIRE(tag != nullptr);
    REQUIRE(extra != nullptr);
    REQUIRE(extra->e == tag->v * 10);
  }
}

TEST_CASE("[target] BufferIterator survives CreateEntity during iteration")
{
  // P0: deque iterators invalidate on push_back; index-based iterators would not.
  // Under MSVC iterator debugging this may abort; under release it is UB.
  // Contract after fix: either safe to create, or create-during-iter is rejected.
  auto &cm = ecs::current()->getOrCreateManager<ms::Base>();
  auto *buf = cm.template getOrCreateComponentBuffer<ms::Base::Tag>();

  ms::Base::create();
  ms::Base::create();
  const uint32_t start_size = buf->size();
  REQUIRE(start_size >= 2);

  int visited = 0;
  bool created = false;
  for (auto it = buf->begin(); it != buf->end(); ++it)
  {
    if (!created)
    {
      ms::Base::create();
      created = true;
    }
    (void)(*it);
    ++visited;
  }

  REQUIRE(created);
  REQUIRE(visited >= static_cast<int>(start_size));
  REQUIRE(buf->size() == start_size + 1);
}

TEST_CASE("[target] DestroyEntity / generation invalidates old access")
{
  // Desired API sketch (not implemented):
  //   auto* e = ms::Other::create();
  //   auto id = e->id;
  //   auto gen = e->generation; // or EntityHandle
  //   DestroyEntity(e); // or e->release() that actually frees
  //   REQUIRE(try_get(id, gen) == nullptr);
  //   auto* reuse = ms::Other::create();
  //   REQUIRE(reuse->id == id);           // slot reuse
  //   REQUIRE(reuse->generation != gen);  // ABA prevented
  //
  // Today release() is empty and there is no DestroyEntity — lock the gap.
  ms::Other *e = ms::Other::create();
  REQUIRE(e != nullptr);

  // Fail until a real destroy + generation protocol exists.
  bool has_destroy_protocol = false;
  REQUIRE(has_destroy_protocol);
}

TEST_CASE("[target] EntityIterator== returns false on type mismatch")
{
  auto &cm_a = ecs::current()->getOrCreateManager<ms::Base>();
  auto &cm_b = ecs::current()->getOrCreateManager<ms::Other>();
  auto *reg_a = cm_a.template getOrCreateRegistryComponentBuffer<ms::Base>();
  auto *reg_b = cm_b.template getOrCreateRegistryComponentBuffer<ms::Other>();

  auto it_a = reg_a->beginEntity();
  auto it_b = reg_b->beginEntity();

  // Desired: compare as false, no throw.
  bool threw = false;
  bool equal = false;
  try
  {
    equal = (*it_a == *it_b);
  }
  catch (const std::bad_cast &)
  {
    threw = true;
  }
  REQUIRE_NOT(threw);
  REQUIRE_NOT(equal);
}
