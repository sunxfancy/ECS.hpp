# ECS.hpp Public API Reference

This document describes the public surface of `src/ECS.hpp`. Internal helpers used only by macros or by `Table` friends are noted where relevant.

## Macros

### `ENTITY(T, BASE)`

Placed inside a user entity class that publicly inherits `BASE` (`ecs::Entity` or another entity type).

Expands to:

- `using super = BASE;`
- `getComponentManager()` override → returns the entity's owning storage manager
- `static T *create()` → `ecs::CreateEntity<T>()` (current table)
- `static T *create(ecs::Table &tbl)` → `ecs::CreateEntity<T>(tbl)`

The class must still implement `void release() override`. Typical pattern:

```cpp
void release() override { ecs::DestroyEntity(this); }
```

### `COMPONENT(T, name)`

Generates a member function `ecs::ComponentRef<T> name()` that resolves component storage through the entity's owning manager.

### `OPTIONAL_COMPONENT(T, name)`

Generates `ecs::OptionalComponentRef<T> name()`. The optional ref type exists for future optional-component support; it currently only exposes `CM()`.

---

## World / Table

### `class Table`

Owns per-type `ComponentManager` instances (main + staging), defer depth, and pending destroys.

| Member | Description |
| --- | --- |
| `Table()` | Empty world |
| Non-copyable | Copy/assign deleted |
| `~Table()` | Deletes all owned managers and buffers |
| `getOrCreateManager<T>()` | Main storage for entity type `T` |
| `getOrCreateStagingManager<T>()` | Staging storage used while deferred |
| `getManager<T>()` / `getManager(type_index)` | Lookup; `nullptr` if absent |
| `is_deferred()` | `true` when `defer_depth_ > 0` |
| `begin_defer()` / `end_defer()` | Adjust depth; auto-`commit` at 0 |
| `commit()` | Apply pending destroys, publish staging, clear staging |

Entity ids are **table-local**. Comparing ids across tables is meaningless.

### Context helpers

```cpp
Table &default_table();
Table *current();                 // never null; falls back to default_table()
void set_current(Table *t);       // nullptr → default_table()

struct ScopedTable {
  explicit ScopedTable(Table &t);
  ~ScopedTable();                 // restores previous current
};
```

`current` is `thread_local`. Destroying a table that is still bound as `current` on a thread is a caller error.

---

## Entity lifecycle

### `class Entity`

Abstract base for all entity types.

| Field | Meaning |
| --- | --- |
| `id` | Slot index in the owning registry |
| `flags` | Bitset: `kEntityDead`, `kEntityStaging` |
| `generation` | Bumped on destroy; starts at 1 |
| `table` | Owning `Table *` |
| `storage` | Owning `IComponentManager *` |

| Method | Meaning |
| --- | --- |
| `release()` | Pure virtual; user should call `DestroyEntity` |
| `getComponentManager()` | Storage for this entity's concrete type |

### Flags

```cpp
inline constexpr uint32_t kEntityDead = 1;
inline constexpr uint32_t kEntityStaging = 2;
```

### `is_entity_visible(const Entity *e)`

Returns `true` only when `e` is non-null and has neither `kEntityDead` nor `kEntityStaging`. Views use this to skip invisible slots.

### Create

```cpp
template <typename T>
T *CreateEntity();                 // *current()

template <typename T>
T *CreateEntity(Table &table, bool force_main = false);
```

When the table is deferred and `force_main` is false, the entity is allocated in staging and flagged `kEntityStaging`. Otherwise it goes to main storage immediately.

### Destroy

```cpp
void DestroyEntity(Entity *e);
```

- `nullptr` → no-op
- Already dead → no-op
- Always bumps `generation` and sets `kEntityDead` (except early returns above)
- Deferred + main entity → enqueue reclaim for commit
- Deferred + staging entity → cancelled; never published
- Not deferred → immediate `reclaim` (freelist + component slot reset)

---

## Handles

```cpp
struct EntityHandle {
  Table *table = nullptr;
  std::type_index type{typeid(void)};
  uint32_t id = 0;
  uint32_t generation = 0;
};

EntityHandle handle_of(const Entity *e);
Entity *try_get(EntityHandle h);
```

`try_get` returns `nullptr` when:

- `table` is null
- manager / registry missing
- `id` out of range
- generation mismatch
- entity is dead

**v1 note:** handles taken from staging entities are not remapped through publish. After commit, take a new handle from the published entity (or do not rely on the old staging handle).

---

## Components

### `ComponentRef<T>`

Returned by `COMPONENT` accessors.

| Operation | Behavior |
| --- | --- |
| `operator*` / `operator->` | Access `ComponentBuffer<T>` slot at `entity->id` |
| Buffer resolution | Via `entity->getComponentManager()` (owning table), **not** `current()` |

Creating a component accessor on first use lazily creates the buffer and may grow it.

### Storage layout (per manager)

- `registy` — `RegistryComponentBuffer<T>` holding entity objects (`std::deque<T>`)
- `components` — map of `type_index` → `ComponentBuffer<U>`
- Parent manager links wire inheritance so base Views walk subclass registries.
Creating a subclass entity (or publishing one from staging) recursively
ensures the super registry/component buffers exist and are linked, so
`View<Base>` works even when no base-typed entity was created first.

---

## Views

```cpp
template <typename B, typename... Ts>
class View {
  View();                      // *current(), begins defer
  explicit View(Table &table); // begins defer on that table
  ~View();                     // end_defer → maybe commit
  ViewIterator<B, Ts...> begin();
  ViewIterator<B, Ts...> end();
};
```

Non-copyable. Range-for friendly:

```cpp
for (auto [pos, vel] : ecs::View<Node, Node::Position, Node::Velocity>(table))
  pos->x += vel->dx;
```

`operator*` on the iterator yields `std::tuple<Ts *...>`. Dead and staging entities are skipped.

---

## Defer API

```cpp
void begin_defer();
void begin_defer(Table &t);
void end_defer();
void end_defer(Table &t);
void commit();
void commit(Table &t);

struct ScopedDefer {
  ScopedDefer();                 // current table
  explicit ScopedDefer(Table &t);
  ~ScopedDefer();                // end_defer
};
```

Nesting shares one depth counter per table. Only the outermost exit auto-commits. `commit()` may flush early while depth stays open.

### Commit order

1. Apply pending main-storage destroys (freelist reclaim)
2. Publish staging entities into main (preserving generation; clearing staging flags)
3. Delete staging managers

---

## Type relationships

```
Table
├── managers_[type]          // published ComponentManager<T>
│   ├── registy              // RegistryComponentBuffer<T>
│   ├── components[...]      // ComponentBuffer<U>
│   └── parent → super mgr   // same Table only
├── staging_managers_[type]
└── pending_destroys_ / defer_depth_
```

`CreateEntity` / `View` / `DestroyEntity` always operate on a concrete `Table` (explicit or current).
