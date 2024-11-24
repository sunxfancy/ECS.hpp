ECS.hpp - Class enabled Entity/Component System
===============================================

ECS.hpp is a header-only, class enabled entity/component system which provided a inheritence between different entity classes. 
Currently, it is in development and is not ready for production use.

## Features

1. Native C++ Class support for Entities and Components
2. Entity/Component System

## Basic Usage Example

Assuming we have a `Node` class which has a `Position` component and a `Movable` class which inherits from `Node` and has a `Velocity` component.
We can define the classes as follows:

```cpp
#include "ECS.hpp"

class Node : public ecs::Entity
{
public:
  ENTITY(Node, ecs::Entity)

  struct Position
  {
    float x, y;
  };
  COMPONENT(Position, position)
};

class Movable : public Node
{
public:
  ENTITY(Movable, Node)  // Inherit from Node

  struct Velocity
  {
    float dx = 1;
    float dy = 1;
  };
  COMPONENT(Velocity, velocity)
};
```

The `ENTITY` and `COMPONENT` macros are used to define the entity and component classes respectively. The first argument to the `ENTITY` macro is the class name and the second argument is the base class. The `COMPONENT` macro takes the component name and the member variable name as arguments.

We can now create instances of the `Node` and `Movable` classes and access their components as follows:

```cpp

int main()
{
    Node *a = Node::create();
    Movable *b = Movable::create();

    a->position().x = 10;
    a->position().y = 0;

    b->position().x = 0;
    b->position().y = 10;
    b->velocity().dx = 2;
    b->velocity().dy = 2;
    ...
}
```

The component macros will create the necessary boilerplate code to access the components of the entities. The `create` method is used to create instances of the entities.

If you want handle all the entities under a class, you can use the View class to iterate all the entities under the class and its subclasses.

```cpp
    auto view = ecs::View<Node, Position>();

    // This loop will iterate Node and Movable entities
    for (auto it = view.begin(); it != view.end(); ++it)
    {
        auto [v] = *it;
        v->x = 0;
        v->y = 0;
    }
```



## License

MIT License (c) 2024, sunxfancy
