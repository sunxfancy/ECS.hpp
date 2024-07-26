#include "ECS.h"
#include <list>

class Node : public fd::Entity {
public:
  static Node *create() {
    fd::Component<Node, Position>::add();
    fd::Component<Node, Velocity>::add();
    fd::Component<Node, Tree>::add();
    return fd::CreateEntity<Node>();
  }

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
    float dx;
    float dy;
  };

  struct Tree {
    Node *parent;
    std::list<Node *> children;
  };

  fd::Component<Node, Position> position;
  fd::Component<Node, Velocity> velocity;
  fd::Component<Node, Tree> tree;
};

void Node::setPosition(float x, float y) {
  position->x = x;
  position->y = y;
}

void Node::updatePosition() {
  auto view = fd::View<Node, Position, const Velocity>();
  for (auto [pos, v] : view) {
    pos->x += v->dx;
    pos->y += v->dy;
  }
}

Node *Node::getParent() { return tree->parent; }

int main() {
  Node *a = Node::create();
  a->setPosition(1, 2);
  a->release();

  return 0;
}