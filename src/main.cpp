#include "ECS.h"
#include <cstdint>
#include <list>

class Node : public fd::Entity {
public:
  ENTITY(Node, fd::Entity)

  void release() override {
    // fd::ReleaseEntity<Node>(id);
  }

  void setPosition(float x, float y);
  Node *getParent();
  static void updatePosition();

private:
  struct Position {
    float x, y;
  };

  struct Velocity {
    float dx = 1;
    float dy = 1;
  };

  struct Tree {
    Node *parent;
    std::list<Node *> children;
  };

  COMPONENT(Position, position)
  COMPONENT(Velocity, velocity)
  COMPONENT(Tree, tree)

  float a, b, c;
};

void Node::setPosition(float x, float y) {
  position()->x = x;
  position()->y = y;
}

void Node::updatePosition() {
  auto view = fd::View<Node, Position, Velocity>();
  for (auto [pos, v] : view) {
    pos->x += v->dx;
    pos->y += v->dy;
  }
}

Node *Node::getParent() { return tree()->parent; }

struct Image {
  int width, height;
  uint32_t *pixels;
};

class Sprite : public Node {
public:
  ENTITY(Sprite, Node)

  COMPONENT(Image, image);
};

TEST_CASE("ECS") {
  Node *a = Node::create();
  a->setPosition(1, 2);
  Node *b = Node::create();
  b->setPosition(3, 4);
  Sprite *c = Sprite::create();
  c->setPosition(5, 6);
  Sprite *d = Sprite::create();
  d->setPosition(7, 8);
  Sprite *e = Sprite::create();
  e->setPosition(9, 10);

  Node::updatePosition();
}