#include "zeroerr.hpp"
#include "ECS.hpp"

#include <vector>

namespace vs
{
class Node : public ecs::Entity
{
public:
  ENTITY(Node, ecs::Entity)
  void release() override { ecs::DestroyEntity(this); }

  struct Position
  {
    float x = 0;
    float y = 0;
  };
  struct Velocity
  {
    float dx = 0;
    float dy = 0;
  };

  COMPONENT(Position, position)
  COMPONENT(Velocity, velocity)
};

class Sprite : public Node
{
public:
  ENTITY(Sprite, Node)

  struct Image
  {
    int width = 0;
    int height = 0;
  };
  COMPONENT(Image, image)
};

class AnimatedSprite : public Sprite
{
public:
  ENTITY(AnimatedSprite, Sprite)

  struct Frame
  {
    int index = 0;
  };
  COMPONENT(Frame, frame)
};
} // namespace vs

static int count_nodes(ecs::Table &table)
{
  int n = 0;
  for (auto [p] : ecs::View<vs::Node, vs::Node::Position>(table))
  {
    (void)p;
    ++n;
  }
  return n;
}

TEST_CASE("empty View over fresh table visits nothing")
{
  ecs::Table table;
  int n = 0;
  for (auto [p, v] : ecs::View<vs::Node, vs::Node::Position, vs::Node::Velocity>(table))
  {
    (void)p;
    (void)v;
    ++n;
  }
  REQUIRE(n == 0);
  REQUIRE(table.getManager<vs::Node>() != nullptr);
  REQUIRE(table.getManager<vs::Node>()->registy != nullptr);
}

TEST_CASE("multi-component View updates Position from Velocity")
{
  ecs::Table table;
  vs::Node *a = vs::Node::create(table);
  vs::Node *b = vs::Node::create(table);
  a->position()->x = 1;
  a->position()->y = 2;
  a->velocity()->dx = 3;
  a->velocity()->dy = 4;
  b->position()->x = 10;
  b->position()->y = 20;
  b->velocity()->dx = -1;
  b->velocity()->dy = -2;

  for (auto [pos, vel] : ecs::View<vs::Node, vs::Node::Position, vs::Node::Velocity>(table))
  {
    pos->x += vel->dx;
    pos->y += vel->dy;
  }

  REQUIRE(a->position()->x == 4);
  REQUIRE(a->position()->y == 6);
  REQUIRE(b->position()->x == 9);
  REQUIRE(b->position()->y == 18);
}

TEST_CASE("View includes subclass entities for base query")
{
  ecs::Table table;
  vs::Node::create(table)->position()->x = 1;
  vs::Sprite::create(table)->position()->x = 2;
  vs::AnimatedSprite::create(table)->position()->x = 3;

  REQUIRE(count_nodes(table) == 3);

  int sprites = 0;
  int anim = 0;
  for (auto [img] : ecs::View<vs::Sprite, vs::Sprite::Image>(table))
  {
    (void)img;
    ++sprites;
  }
  for (auto [f] : ecs::View<vs::AnimatedSprite, vs::AnimatedSprite::Frame>(table))
  {
    (void)f;
    ++anim;
  }
  // Sprite view includes AnimatedSprite via inheritance links.
  REQUIRE(sprites == 2);
  REQUIRE(anim == 1);
}

TEST_CASE("subclass-only create still visible to View of base")
{
  ecs::Table table;
  vs::Sprite::create(table)->position()->x = 9;
  REQUIRE(count_nodes(table) == 1);

  ecs::Table deep;
  vs::AnimatedSprite::create(deep)->position()->x = 8;
  REQUIRE(count_nodes(deep) == 1);
}

TEST_CASE("three-level inheritance component access")
{
  ecs::Table table;
  vs::AnimatedSprite *a = vs::AnimatedSprite::create(table);
  a->position()->x = 5;
  a->velocity()->dx = 1;
  a->image()->width = 64;
  a->image()->height = 32;
  a->frame()->index = 2;

  REQUIRE(a->position()->x == 5);
  REQUIRE(a->velocity()->dx == 1);
  REQUIRE(a->image()->width == 64);
  REQUIRE(a->frame()->index == 2);

  int seen = 0;
  for (auto [pos, img, frame] :
       ecs::View<vs::AnimatedSprite, vs::Node::Position, vs::Sprite::Image,
                 vs::AnimatedSprite::Frame>(table))
  {
    REQUIRE(pos->x == 5);
    REQUIRE(img->width == 64);
    REQUIRE(frame->index == 2);
    ++seen;
  }
  REQUIRE(seen == 1);
}

TEST_CASE("View zip keeps multi-component values aligned")
{
  ecs::Table table;
  std::vector<vs::Node *> nodes;
  for (int i = 0; i < 8; ++i)
  {
    vs::Node *n = vs::Node::create(table);
    n->position()->x = static_cast<float>(i);
    n->velocity()->dx = static_cast<float>(i * 10);
    nodes.push_back(n);
  }
  vs::Sprite *s = vs::Sprite::create(table);
  s->position()->x = 100;
  s->velocity()->dx = 1000;
  s->image()->width = 16;

  int seen = 0;
  for (auto [pos, vel] : ecs::View<vs::Node, vs::Node::Position, vs::Node::Velocity>(table))
  {
    REQUIRE(vel->dx == pos->x * 10);
    ++seen;
  }
  REQUIRE(seen == 9);
}

TEST_CASE("dead entities skipped by subsequent View")
{
  ecs::Table table;
  vs::Node *a = vs::Node::create(table);
  vs::Node *b = vs::Node::create(table);
  vs::Node *c = vs::Node::create(table);
  a->position()->x = 1;
  b->position()->x = 2;
  c->position()->x = 3;
  ecs::DestroyEntity(b);

  int sum = 0;
  int n = 0;
  for (auto [p] : ecs::View<vs::Node, vs::Node::Position>(table))
  {
    sum += static_cast<int>(p->x);
    ++n;
  }
  REQUIRE(n == 2);
  REQUIRE(sum == 4);
}

TEST_CASE("destroy then create during View publishes after end")
{
  ecs::Table table;
  vs::Node *keep = vs::Node::create(table);
  vs::Node *drop = vs::Node::create(table);
  keep->position()->x = 1;
  drop->position()->x = 2;

  {
    ecs::View<vs::Node, vs::Node::Position> view(table);
    ecs::DestroyEntity(drop);
    vs::Node *created = vs::Node::create(table);
    created->position()->x = 3;
    created->velocity()->dx = 9;

    int n = 0;
    for (auto it = view.begin(); it != view.end(); ++it)
      ++n;
    REQUIRE(n == 1);
  }

  REQUIRE(count_nodes(table) == 2);
  int sum = 0;
  for (auto [p] : ecs::View<vs::Node, vs::Node::Position>(table))
    sum += static_cast<int>(p->x);
  REQUIRE(sum == 4);
}

TEST_CASE("many entities View visits all once")
{
  ecs::Table table;
  const int N = 128;
  for (int i = 0; i < N; ++i)
  {
    vs::Node *n = vs::Node::create(table);
    n->position()->x = static_cast<float>(i);
  }

  int count = 0;
  float sum = 0;
  for (auto [p] : ecs::View<vs::Node, vs::Node::Position>(table))
  {
    sum += p->x;
    ++count;
  }
  REQUIRE(count == N);
  REQUIRE(sum == static_cast<float>((N - 1) * N / 2));
}

TEST_CASE("Table destructor clears managers without leak assert")
{
  {
    ecs::Table table;
    vs::Node::create(table);
    vs::Sprite::create(table);
    vs::AnimatedSprite::create(table);
    REQUIRE(count_nodes(table) == 3);
  }
  // Leaving scope destroys Table and owned managers.
  REQUIRE(true);
}

TEST_CASE("explicit table View ignores current table entities")
{
  ecs::Table a;
  ecs::Table b;
  vs::Node::create(a)->position()->x = 1;
  vs::Node::create(b)->position()->x = 2;
  ecs::set_current(&a);

  int sum = 0;
  int n = 0;
  for (auto [p] : ecs::View<vs::Node, vs::Node::Position>(b))
  {
    sum += static_cast<int>(p->x);
    ++n;
  }
  REQUIRE(n == 1);
  REQUIRE(sum == 2);
  ecs::set_current(nullptr);
}

TEST_CASE("component writes via ComponentRef match View reads")
{
  ecs::Table table;
  vs::Sprite *s = vs::Sprite::create(table);
  s->position()->x = 7;
  s->position()->y = 8;
  s->velocity()->dx = 1;
  s->image()->width = 64;

  for (auto [pos, vel, img] :
       ecs::View<vs::Sprite, vs::Node::Position, vs::Node::Velocity, vs::Sprite::Image>(
           table))
  {
    REQUIRE(pos->x == 7);
    REQUIRE(pos->y == 8);
    REQUIRE(vel->dx == 1);
    REQUIRE(img->width == 64);
    pos->x = 70;
  }
  REQUIRE(s->position()->x == 70);
}

TEST_CASE("deferred subclass create publishes inherited components")
{
  ecs::Table table;
  {
    ecs::ScopedDefer guard(table);
    vs::AnimatedSprite *a = vs::AnimatedSprite::create(table);
    a->position()->x = 4;
    a->velocity()->dy = 2;
    a->image()->height = 48;
    a->frame()->index = 3;
    REQUIRE(count_nodes(table) == 0);
  }
  REQUIRE(count_nodes(table) == 1);

  int seen = 0;
  for (auto [pos, vel, img, frame] :
       ecs::View<vs::AnimatedSprite, vs::Node::Position, vs::Node::Velocity,
                 vs::Sprite::Image, vs::AnimatedSprite::Frame>(table))
  {
    REQUIRE(pos->x == 4);
    REQUIRE(vel->dy == 2);
    REQUIRE(img->height == 48);
    REQUIRE(frame->index == 3);
    ++seen;
  }
  REQUIRE(seen == 1);
}
