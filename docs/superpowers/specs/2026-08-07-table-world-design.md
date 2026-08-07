# Table (World) Abstraction Design

Date: 2026-08-07  
Status: Approved for planning

## Goal

Add a World-level `Table` so the same entity types can live in multiple isolated storages (e.g. scene A vs scene B). `CreateEntity` / `View` default to a thread-local current Table, with explicit Table overrides.

## Decisions

| Topic | Choice |
| --- | --- |
| Purpose | Multi-world / multi-scene isolation |
| Default create/view | Thread-local current Table context |
| Table granularity | One Table holds all entity types in that world |
| View binding | Context default + explicit `View(table)` |
| Current binding API | `set_current` + RAII `ScopedTable` |
| Architecture | Table owns storage; `ComponentManager<T>` remains type-schema singleton |

## Architecture

```
Table (World)
├── storage[Node]   → registry + ComponentBuffers
├── storage[Sprite] → registry + ComponentBuffers
└── ...

ComponentManager<T>::inst()  → type / parent links only (no entity data)
thread_local Table* current  → default target for create() / View()
Entity                       → id + Table* (owning world)
```

- Process-wide `default_table()` exists; unset current resolves to it.
- Inheritance `parent/children/next` buffer links stay **within one Table**.
- Entity ids are Table-local; comparing ids across Tables is meaningless.
- Out of scope: moving entities between Tables, cross-Table views, serialization.

## Public API

```cpp
namespace ecs {

class Table {
public:
  Table();
};

Table& default_table();
Table* current();
void set_current(Table* t);  // nullptr → default_table()

struct ScopedTable {
  explicit ScopedTable(Table& t);
  ~ScopedTable();  // restore previous current
};

template <typename T>
T* CreateEntity();               // current()
template <typename T>
T* CreateEntity(Table& table);   // explicit

template <typename B, typename... Ts>
class View {
  View();                        // current Table
  explicit View(Table& table);   // explicit Table
};

} // namespace ecs
```

`ENTITY` macro gains:

```cpp
static T *create() { return ecs::CreateEntity<T>(); }
static T *create(ecs::Table& table) { return ecs::CreateEntity<T>(table); }
```

Existing no-arg `create()` / `View()` keep working against the default/current Table.

## Internal data flow

### Storage ownership

Move `registy` and `components` off the global `IComponentManager` singleton into per-type slots owned by `Table` (name may be `TypeStorage` or a Table-local manager instance). `ComponentManager<T>::inst()` keeps parent type linkage and `getType()` only.

### `CreateEntity<T>(table)`

1. On `table`, get-or-create registry for `T` (and parent registry links inside that Table).
2. `add()`, set `id`, set `entity.table = &table`.
3. `ensure_space(id + 1)` on that Table's component buffers for `T` (same timing as today).

### `ComponentRef`

Resolve buffers via `entity.table` → that Table's `ComponentBuffer<T>`, not via global `ComponentManager` storage and not via thread-local current. Holding a pointer from Table A remains correct after current switches to B.

### Context

- `thread_local Table* t_current`.
- `ScopedTable` saves/restores previous current.
- Destroying a Table that is still `current` on a thread: document that callers must not; implementation may fall back to `default_table()`.

## Compatibility

- Unchanged call sites using only default Table behave as today.
- `View<Node, ...>` still includes subclasses **within the selected Table only**.

## Test plan

1. Existing `main` / `memory_safety` cases pass on default Table.
2. Two Tables: entities/components isolated; `View(A)` does not see B.
3. Nested `set_current` / `ScopedTable`: no-arg create/view target the right Table; restore on exit.
4. With current=A, `CreateEntity(B)` / `View(B)` only touch B.
5. Same-Table inheritance: `View<Node>` sees `Sprite` in that Table only.
6. `ComponentRef` on an A entity still reads/writes A after current → B.

## Non-goals (YAGNI)

- Entity migration between Tables
- Unified cross-Table iteration
- Persist / serialize Table
