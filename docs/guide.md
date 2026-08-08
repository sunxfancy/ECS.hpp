# ECS.hpp Usage Guide

Practical patterns for building on `ECS.hpp`. For signatures, see [API.md](API.md). For design rationale, see the specs under `docs/superpowers/specs/`.

## Defining entities

Prefer small POD component structs and keep behavior on the entity class or free functions / systems.

```cpp
class Unit : public ecs::Entity
{
public:
  ENTITY(Unit, ecs::Entity)
  void release() override { ecs::DestroyEntity(this); }

  struct Transform { float x = 0, y = 0; };
  struct Health { int hp = 100; };

  COMPONENT(Transform, transform)
  COMPONENT(Health, health)
};

class Enemy : public Unit
{
public:
  ENTITY(Enemy, Unit)
  struct AI { int state = 0; };
  COMPONENT(AI, ai)
};
```

Guidelines:

1. Always implement `release()` (usually forwarding to `DestroyEntity`).
2. Put shared components on the base class so base Views can read them on subclasses.
3. Keep one `Table` per scene/level/simulation island.

## Choosing a world

| Pattern | When |
| --- | --- |
| Default table | Tiny demos / single-world tools |
| Explicit `create(table)` / `View(table)` | Clear ownership, easiest to reason about |
| `ScopedTable` | A block of code should default to one world |

```cpp
void load_level(ecs::Table &level)
{
  ecs::ScopedTable guard(level);
  spawn_player();          // uses current → level
  spawn_enemies();
}
```

Do not destroy a `Table` while it is still `current` on any thread.

## Writing systems with View

Views are the primary query mechanism. Construct once, iterate fully, then let the `View` destroy (commit).

```cpp
// Components listed in the View are zipped with the entity registry walk.
// Prefer declaring shared components on the base type used as View's first
// parameter so subclasses participate naturally.
void integrate(ecs::Table &world, float dt)
{
  for (auto [pos] : ecs::View<Unit, Unit::Transform>(world))
  {
    pos->x += dt;
  }
}

void tick_enemies(ecs::Table &world)
{
  for (auto [pos, brain] : ecs::View<Enemy, Unit::Transform, Enemy::AI>(world))
  {
    if (brain->state == 0)
      pos->x += 1;
  }
}
```

Safer style for gameplay systems:

```cpp
void apply_damage(ecs::Table &world)
{
  ecs::View<Unit, Unit::Health> view(world);
  for (auto it = view.begin(); it != view.end(); ++it)
  {
    auto [hp] = *it;
    if (hp->hp <= 0)
    {
      // Structural change is deferred until view ends.
      // Prefer DestroyEntity via an Entity* you already hold.
    }
  }
}
```

Because `View` does not currently yield the `Entity*` in the tuple, hold entity pointers you create, or use handles, when you need to destroy from inside a system.

## Safe create/destroy during iteration

### Prefer defer (automatic with View)

```cpp
ecs::Table world;
Unit *a = Unit::create(world);
Unit *b = Unit::create(world);

{
  ecs::View<Unit, Unit::Health> view(world);
  ecs::DestroyEntity(a);                 // tombstone; skipped by later visits
  Unit *spawned = Unit::create(world);   // staging; not seen this pass
  spawned->health()->hp = 50;
}
// commit: a reclaimed, spawned published
```

### Manual defer without a View

```cpp
{
  ecs::ScopedDefer guard(world);
  batch_spawn(world);
  batch_despawn(world);
} // commit
```

### Early flush

```cpp
ecs::begin_defer(world);
spawn_wave(world);
ecs::commit(world);      // publish now; still deferred
spawn_wave(world);
ecs::end_defer(world);   // publish second wave
```

## Handles across frames

```cpp
ecs::EntityHandle target = ecs::handle_of(enemy);

// ... later, possibly after destroys / freelist reuse ...
if (ecs::Entity *e = ecs::try_get(target))
{
  static_cast<Enemy *>(e)->ai()->state = 1;
}
```

Rules of thumb:

- Do not store staging pointers past `commit`.
- After a deferred create commits, re-acquire a handle from a View or from a pointer you know is in main storage.
- Generation mismatch ⇒ treat as “entity gone”.

## Multi-world tips

```cpp
ecs::Table gameplay;
ecs::Table editor_preview;

Unit::create(gameplay);
Unit::create(editor_preview);

// Isolation: counts are independent
```

Inheritance works **inside** each table. A `View<Unit>(gameplay)` never sees `editor_preview` entities.

Subclass-only worlds are fine: creating `Enemy` without ever calling `Unit::create` still wires the `Unit` registry so `View<Unit>` visits those enemies (including after a deferred publish into an empty base).

## Common pitfalls

1. **Letting a temporary View die early**  
   `for (auto [p] : ecs::View<...>(t))` is fine (range-for extends lifetime). Do not call `.begin()` on a temporary without binding the `View`.

2. **Assuming ids are global**  
   Ids are per-table and reused via freelist.

3. **Using staging handles after commit**  
   v1 does not remap staging → main for `try_get`.

4. **Forgetting `release` / `DestroyEntity`**  
   Without destroy, slots are never reclaimed and Views keep seeing the entity.

5. **Cross-table ComponentRef**  
   Component access follows `entity->table`. Switching `current` does not move an entity.

6. **Iterator invalidation without defer**  
   Creating entities while iterating raw `ComponentBuffer` iterators (not `View`) can invalidate `std::deque` iterators. Prefer `View` / defer for structural changes.

## Minimal game loop sketch

```cpp
ecs::Table world;

void init()
{
  auto *player = Unit::create(world);
  player->transform()->x = 0;
  player->health()->hp = 100;
}

void update(float dt)
{
  {
    ecs::View<Unit, Unit::Transform> view(world);
    for (auto it = view.begin(); it != view.end(); ++it)
    {
      auto [tf] = *it;
      tf->x += dt; // placeholder motion
    }
  }
}

void shutdown()
{
  // Destroying `world` releases all managers/buffers.
}
```

## Further reading

- [API.md](API.md) — complete reference
- [testing.md](testing.md) — suite map and how to add cases
- [Table world design](superpowers/specs/2026-08-07-table-world-design.md)
- [Defer/commit design](superpowers/specs/2026-08-07-defer-commit-design.md)
