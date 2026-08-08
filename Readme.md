# ECS.hpp

Header-only, class-enabled Entity/Component System for C++17.

Entities are ordinary C++ classes. Components are plain structs accessed through generated accessors. Inheritance between entity classes is first-class: a `View` over a base type also visits subclasses in the same world (`Table`).

> Status: **in development** — API is usable for experiments and tests, not yet production-hardened.

## Features

- Native C++ entity classes with `ENTITY` / `COMPONENT` macros
- Multi-world isolation via `ecs::Table` (scene / level worlds)
- Thread-local current table (`ScopedTable`, `set_current`)
- Inheritance-aware `View` iteration (base query includes subclasses)
- Defer / commit command buffer for safe create/destroy during iteration
- `EntityHandle` + generation protocol for ABA-safe references
- Header-only: `#include "ECS.hpp"`

## Requirements

- C++17 compiler (GCC, Clang, or MSVC)
- CMake ≥ 3.15 (for the test suite)
- No external runtime dependencies

## Build & Test

```bash
# Prefer Clang or MSVC for the test suite (zeroerr + GCC has pragma issues).
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++
cmake --build build -j
./build/ecs_test
# or: ctest --test-dir build --output-on-failure
```

On Windows (MSVC):

```powershell
cmake -S . -B build
cmake --build build --config Debug
.\build\Debug\ecs_test.exe
```

The test binary links several translation units under `test/`:

| File | Coverage |
| --- | --- |
| `main.cpp` | Smoke / regression for Node–Sprite hierarchy |
| `table_world.cpp` | Multi-table isolation and current context |
| `defer_commit.cpp` | Defer, staging, destroy, nested Views |
| `handles.cpp` | `EntityHandle`, freelist, visibility helpers |
| `view_systems.cpp` | Multi-component Views, deep inheritance, systems |

## Quick Start

```cpp
#include "ECS.hpp"

class Node : public ecs::Entity
{
public:
  ENTITY(Node, ecs::Entity)

  void release() override { ecs::DestroyEntity(this); }

  struct Position { float x = 0, y = 0; };
  COMPONENT(Position, position)
};

class Movable : public Node
{
public:
  ENTITY(Movable, Node)

  struct Velocity { float dx = 1, dy = 1; };
  COMPONENT(Velocity, velocity)
};

int main()
{
  Node *a = Node::create();
  Movable *b = Movable::create();

  a->position()->x = 10;
  b->position()->y = 10;
  b->velocity()->dx = 2;

  for (auto [pos, vel] : ecs::View<Node, Node::Position, Movable::Velocity>())
  {
    pos->x += vel->dx;
    pos->y += vel->dy;
  }
}
```

Macros:

- `ENTITY(T, BASE)` — declares `super`, `getComponentManager()`, and `create()` / `create(Table&)`.
- `COMPONENT(T, name)` — generates `name()` returning `ecs::ComponentRef<T>`.

## Concepts

### Table (world)

A `Table` owns all entity registries and component buffers for one world. Different tables never share entity ids or storage.

```cpp
ecs::Table level;
{
  ecs::ScopedTable guard(level);
  Node *a = Node::create();                          // into level
  auto view = ecs::View<Node, Node::Position>();     // current table
}
Node *b = Node::create(level);                       // explicit
auto view = ecs::View<Node, Node::Position>(level);
```

| API | Meaning |
| --- | --- |
| `ecs::default_table()` | Process-wide fallback world |
| `ecs::current()` | Thread-local current table (or default) |
| `ecs::set_current(t)` | Bind current; `nullptr` → default |
| `ecs::ScopedTable` | RAII save/restore of current |

### View

`View<Base, Comp...>` iterates live (non-dead, non-staging) entities of `Base` **and its subclasses** inside one table, zipping the requested component buffers.

```cpp
// Visits Node and Movable entities that have Position (+ Velocity if listed)
for (auto [pos, vel] : ecs::View<Node, Node::Position, Movable::Velocity>(level))
{
  pos->x += vel->dx;
}
```

Constructing a `View` begins a defer scope; destroying it ends the scope and commits when the outermost defer depth hits zero. Keep the `View` object alive for the whole loop.

### Defer / Commit

Structural mutations (create / destroy) during iteration are buffered:

| State | Create | Destroy | View sees |
| --- | --- | --- | --- |
| Not deferred | Immediate (main storage) | Immediate reclaim | Latest main |
| Deferred | Staging (hidden from View) | Tombstone + queued reclaim | Main only |
| After commit | Staging published | Slot on freelist | Updated main |

```cpp
ecs::Table table;
{
  ecs::ScopedDefer guard(table);
  auto *e = Node::create(table);   // staging
  e->position()->x = 42;           // writable now
}                                  // auto-commit
```

Also available: `begin_defer` / `end_defer` / `commit`, each with current-table and `Table&` overloads.

### Handles & lifetime

Raw pointers into staging become invalid after commit. Prefer handles across defer boundaries:

```cpp
auto h = ecs::handle_of(entity);
ecs::DestroyEntity(entity);
REQUIRE(ecs::try_get(h) == nullptr);   // generation mismatch / dead
```

`DestroyEntity(nullptr)` and double-destroy are no-ops. Destroy bumps `generation` and marks `kEntityDead`; freelist reuse resets component slots and keeps a new generation.

### Inheritance

Parent/child buffer links are **within one Table**. `View<Node>` in table A never sees entities from table B, but does see `Sprite` / deeper subclasses that live in A.

## Documentation

| Document | Content |
| --- | --- |
| [docs/API.md](docs/API.md) | Public API reference |
| [docs/guide.md](docs/guide.md) | Usage guide, patterns, pitfalls |
| [docs/testing.md](docs/testing.md) | How the test suite is organized |
| [Table world design](docs/superpowers/specs/2026-08-07-table-world-design.md) | Multi-world design spec |
| [Defer/commit design](docs/superpowers/specs/2026-08-07-defer-commit-design.md) | Command buffer design spec |

## Project Layout

```
src/ECS.hpp                 # library (header-only)
test/                       # zeroerr-based suite + viz helpers
docs/                       # guides + design specs
CMakeLists.txt              # builds ecs_test
```

## Non-goals (current)

- Moving entities between Tables
- Cross-table Views
- Serialization / persistence
- Multi-threaded commit
- Command-buffering component add/remove

## License

MIT License (c) 2024, sunxfancy
