#define ZEROERR_IMPLEMENTATION
#include "zeroerr.hpp"

#include "ECS.hpp"
#include <cstdint>
#include <list>

extern void dump(ecs::IComponentManager *icm, std::string name);


class Node : public ecs::Entity
{
public:
  ENTITY(Node, ecs::Entity)

  void release() override
  {
    // ecs::ReleaseEntity<Node>(id);
  }

  void setPosition(float x, float y);
  Node *getParent();
  static void updatePosition();
  static void updateVelocity();

  struct Position
  {
    float x, y;
  };

  struct Velocity
  {
    float dx = 1;
    float dy = 1;
  };

  struct Tree
  {
    Node *parent;
    std::list<Node *> children;
  };

  COMPONENT(Position, position)
  COMPONENT(Velocity, velocity)
  COMPONENT(Tree, tree)

  float a, b, c;
};

std::ostream &operator<<(std::ostream &os, const Node::Position &position)
{
  os << "{ x = " << position.x << ", y = " << position.y << "}";
  return os;
}

std::ostream &operator<<(std::ostream &os, const Node::Velocity &velocity)
{
  os << "{ dx = " << velocity.dx << ", dy = " << velocity.dy << "}";
  return os;
}

void Node::setPosition(float x, float y)
{
  position()->x = x;
  position()->y = y;
}

void Node::updateVelocity()
{
  auto view = ecs::View<Node, Velocity>();
  dump(&ecs::current()->getOrCreateManager<Node>(), "node6.dot");
  for (auto it = view.begin(); it != view.end(); ++it)
  {
    auto [v] = *it;
    v->dx += 1;
    v->dy += 1;
  }
}

void Node::updatePosition()
{
  auto view = ecs::View<Node, Position, Velocity>();
  for (auto [pos, v] : view)
  {
    pos->x += v->dx;
    pos->y += v->dy;
  }
}

Node *Node::getParent() { return tree()->parent; }

struct Image
{
  int width, height;
  uint32_t *pixels;
};

std::ostream &operator<<(std::ostream &os, const Image &image)
{
  os << "{ width = " << image.width << ", height = " << image.height << "}";
  return os;
}

class Sprite : public Node
{
public:
  ENTITY(Sprite, Node)

  COMPONENT(Image, image);
};


TEST_CASE("main test")
{
  // Regression: View before any CreateEntity must not crash on null registry.
  {
    auto empty_view = ecs::View<Node, Node::Velocity>();
    int empty_count = 0;
    for (auto [v] : empty_view)
    {
      (void)v;
      ++empty_count;
    }
    REQUIRE(empty_count == 0);
    REQUIRE(ecs::current()->getOrCreateManager<Node>().registy != nullptr);
  }

  Node *a = Node::create();
  a->setPosition(1, 2);
  dump(&ecs::current()->getOrCreateManager<Node>(), "node1.dot");
  Node *b = Node::create();
  b->setPosition(3, 4);
  dump(&ecs::current()->getOrCreateManager<Node>(), "node2.dot");
  Sprite *c = Sprite::create();
  c->setPosition(5, 6);
  dump(&ecs::current()->getOrCreateManager<Node>(), "node3.dot");
  Sprite *d = Sprite::create();
  d->setPosition(7, 8);
  dump(&ecs::current()->getOrCreateManager<Node>(), "node4.dot");
  Sprite *e = Sprite::create();
  e->setPosition(9, 10);
  dbg(*(a->velocity()));
  dump(&ecs::current()->getOrCreateManager<Node>(), "node5.dot");

  Node::updateVelocity();
  dbg(*(a->velocity()));
  REQUIRE(a->velocity()->dx == 2);
  REQUIRE(a->velocity()->dy == 2);
  REQUIRE(b->velocity()->dx == 2);
  REQUIRE(b->velocity()->dy == 2);
  REQUIRE(c->velocity()->dx == 2);
  REQUIRE(c->velocity()->dy == 2);
  REQUIRE(d->velocity()->dx == 2);
  REQUIRE(d->velocity()->dy == 2);
  REQUIRE(e->velocity()->dx == 2);
  REQUIRE(e->velocity()->dy == 2);

  Node::updatePosition();

  REQUIRE(a->position()->x == 3);
  REQUIRE(a->position()->y == 4);
  REQUIRE(b->position()->x == 5);
  REQUIRE(b->position()->y == 6);
  REQUIRE(c->position()->x == 7);
  REQUIRE(c->position()->y == 8);
  REQUIRE(d->position()->x == 9);
  REQUIRE(d->position()->y == 10);
  REQUIRE(e->position()->x == 11);
  REQUIRE(e->position()->y == 12);
}
