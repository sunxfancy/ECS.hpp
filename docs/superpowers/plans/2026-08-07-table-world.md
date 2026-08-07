# Table (World) Abstraction Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a World-level `Table` that owns per-type entity/component storage, with thread-local current context for `CreateEntity` / `View` and explicit Table overrides.

**Architecture:** Keep `IComponentManager` as the per-type **storage** object (registry + component buffers + same-Table parent links), but move ownership from process-wide `ComponentManager<T>::inst()` into `Table`. `ComponentManager<T>` becomes a schema-only singleton (type / `super` parent link). `Entity` gains `Table* table` so `ComponentRef` resolves buffers via the owning Table, not thread-local current.

**Tech Stack:** C++17, header-only `src/ECS.hpp`, CMake + MSVC (`ecs_test`), zeroerr test macros.

**Spec:** `docs/superpowers/specs/2026-08-07-table-world-design.md`

## Global Constraints

- C++17; header-only public API in `src/ECS.hpp`
- Preserve no-arg `T::create()` / `View<...>()` behavior against default/current Table
- No entity migration between Tables; no cross-Table views; no Table serialization
- Destroying a Table that is still `current` on a thread is caller error; implementation may fall back to `default_table()`
- Build: `cmake --build build --config Debug`
- Run tests: `.\build\Debug\ecs_test.exe`

---

## File structure

| File | Responsibility |
| --- | --- |
| `src/ECS.hpp` | `Table`, context API, storage ownership move, `CreateEntity`/`View`/`ENTITY`/`ComponentRef` updates |
| `test/table_world.cpp` | New isolation / context / ComponentRef tests from spec |
| `CMakeLists.txt` | Add `test/table_world.cpp` to `TEST_SOURCES` |
| `test/main.cpp` | Point `dump(...)` at Table-owned managers (default/current) |
| `test/memory_safety.cpp` | Replace `ComponentManager<T>::inst().registy` access with Table storage |
| `test/visualize.cpp` | Keep visualizing `IComponentManager` storage (no API rename required if storage type name stays) |

---

### Task 1: Table context API (no storage move yet)

**Files:**
- Modify: `src/ECS.hpp`
- Create: `test/table_world.cpp`
- Modify: `CMakeLists.txt`
- Test: `test/table_world.cpp`

**Interfaces:**
- Consumes: none
- Produces:
  - `class Table { public: Table(); Table(const Table&) = delete; Table& operator=(const Table&) = delete; };`
  - `Table& default_table();`
  - `Table* current();`
  - `void set_current(Table* t);` — `nullptr` → `&default_table()`
  - `struct ScopedTable { explicit ScopedTable(Table& t); ~ScopedTable(); ScopedTable(const ScopedTable&) = delete; ... private: Table* prev_; };`

- [ ] **Step 1: Write the failing tests**

Create `test/table_world.cpp`:

```cpp
#include "zeroerr.hpp"
#include "ECS.hpp"

TEST_CASE("default current is default_table")
{
  REQUIRE(ecs::current() == &ecs::default_table());
}

TEST_CASE("set_current and ScopedTable restore")
{
  ecs::Table a;
  ecs::Table b;
  ecs::set_current(&a);
  REQUIRE(ecs::current() == &a);
  {
    ecs::ScopedTable guard(b);
    REQUIRE(ecs::current() == &b);
  }
  REQUIRE(ecs::current() == &a);
  ecs::set_current(nullptr);
  REQUIRE(ecs::current() == &ecs::default_table());
}
```

Add to `CMakeLists.txt` `TEST_SOURCES`:

```cmake
${CMAKE_SOURCE_DIR}/test/table_world.cpp
```

- [ ] **Step 2: Run tests to verify they fail**

```powershell
cmake --build build --config Debug
.\build\Debug\ecs_test.exe
```

Expected: compile failure (`Table` / `current` / etc. undeclared) or link/test failure.

- [ ] **Step 3: Minimal implementation in `src/ECS.hpp`**

Add near top of `namespace ecs` (before `Entity` is fine):

```cpp
class Table
{
public:
  Table() = default;
  Table(const Table &) = delete;
  Table &operator=(const Table &) = delete;
};

inline Table &default_table()
{
  static Table t;
  return t;
}

inline Table *&current_slot()
{
  thread_local Table *cur = nullptr;
  return cur;
}

inline Table *current()
{
  Table *c = current_slot();
  return c ? c : &default_table();
}

inline void set_current(Table *t)
{
  current_slot() = t ? t : &default_table();
}

struct ScopedTable
{
  explicit ScopedTable(Table &t) : prev_(current_slot())
  {
    current_slot() = &t;
  }
  ~ScopedTable() { current_slot() = prev_; }
  ScopedTable(const ScopedTable &) = delete;
  ScopedTable &operator=(const ScopedTable &) = delete;

private:
  Table *prev_;
};
```

Notes:
- `current()` returns `&default_table()` when slot is null (never returns null to callers).
- `set_current(nullptr)` sets slot to `&default_table()` (non-null), matching spec.

- [ ] **Step 4: Run tests to verify they pass**

```powershell
cmake --build build --config Debug
.\build\Debug\ecs_test.exe
```

Expected: new cases PASS; existing tests still PASS.

- [ ] **Step 5: Commit**

```bash
git add src/ECS.hpp test/table_world.cpp CMakeLists.txt
git commit -m "feat: add Table current-context API"
```

---

### Task 2: Table-owned managers + Entity::table + CreateEntity(Table&)

**Files:**
- Modify: `src/ECS.hpp` (`Entity`, `IComponentManager`, `ComponentManager`, `CreateEntity`, `ENTITY` macro)
- Modify: `test/main.cpp` (registry null check / dump sources)
- Modify: `test/memory_safety.cpp` (any `ComponentManager<T>::inst().registy` usage)
- Test: existing suite + extend `test/table_world.cpp`

**Interfaces:**
- Consumes: Task 1 `Table` / `current()` / `default_table()`
- Produces:
  - `Entity::Table *table = nullptr;`
  - `Table::template getOrCreateManager<T>() -> ComponentManager<T>&` (Table-local storage)
  - `ComponentManager<T>` **no longer** process-wide storage singleton
  - Schema: keep a lightweight singleton for parent-type wiring **or** resolve parent storage only via `Table` + `T::super` (prefer Table-only parent storage links; remove storage from `inst()`)
  - `T* CreateEntity()` → `CreateEntity<T>(*current())`
  - `T* CreateEntity(Table& table)`
  - `ENTITY` macro: `getComponentManager()` returns `table->getOrCreateManager<T>()`; add `create(Table&)`
  - `IComponentManager::table` back-pointer to owning `Table`

**Design detail (implement exactly):**

1. Add to `class Table`:

```cpp
std::map<std::type_index, IComponentManager *> managers_;

template <typename T>
ComponentManager<T> &getOrCreateManager()
{
  auto key = std::type_index(typeid(T));
  auto it = managers_.find(key);
  if (it != managers_.end())
    return *static_cast<ComponentManager<T> *>(it->second);

  IComponentManager *parent_storage = nullptr;
  if constexpr (!std::is_same_v<typename T::super, Entity>)
  {
    parent_storage = &getOrCreateManager<typename T::super>();
  }
  auto *cm = new ComponentManager<T>(this, parent_storage);
  managers_[key] = cm;
  return *cm;
}

template <typename T>
ComponentManager<T> *getManager()
{
  auto it = managers_.find(std::type_index(typeid(T)));
  if (it == managers_.end())
    return nullptr;
  return static_cast<ComponentManager<T> *>(it->second);
}
```

2. Change `ComponentManager<T>`:
   - Remove storage singleton `inst()` **or** make `inst()` return `current()->getOrCreateManager<T>()` **only as a temporary compatibility shim** for dump helpers — prefer updating call sites to `current()->getOrCreateManager<T>()` / `entity.getComponentManager()` and **delete storage `inst()`**.
   - Constructor: `ComponentManager(Table *t, IComponentManager *parent_storage)` sets `this->table = t`, `this->parent = parent_storage`. Do **not** assign `parent` from another Table.

3. `Entity` gains `Table *table = nullptr;`

4. `ENTITY` macro:

```cpp
#define ENTITY(T, BASE)                                        \
  using super = BASE;                                          \
  ecs::IComponentManager &getComponentManager() const override \
  {                                                            \
    return table->template getOrCreateManager<T>();            \
  }                                                            \
  static T *create() { return ecs::CreateEntity<T>(); }        \
  static T *create(ecs::Table &tbl) { return ecs::CreateEntity<T>(tbl); }
```

5. `CreateEntity`:

```cpp
template <typename T>
T *CreateEntity(Table &table)
{
  auto *registry = table.template getOrCreateManager<T>()
                       .template getOrCreateRegistryComponentBuffer<T>();
  uint32_t id = registry->add();
  T &inst = registry->get(id);
  inst.id = id;
  inst.table = &table;

  IComponentManager *cm = &table.template getOrCreateManager<T>();
  for (auto [key, component] : cm->components)
  {
    component->ensure_space(id + 1);
  }
  return &inst;
}

template <typename T>
T *CreateEntity()
{
  return CreateEntity<T>(*current());
}
```

6. Update `getOrCreateRegistryComponentBuffer` parent lookup to use **this manager's** `parent` (already Table-local), not `ComponentManager<super>::inst()`.

- [ ] **Step 1: Write failing isolation test (create into two tables)**

Append to `test/table_world.cpp` (use isolated types so they do not collide with `main.cpp` Node):

```cpp
namespace tw
{
class Node : public ecs::Entity
{
public:
  ENTITY(Node, ecs::Entity)
  void release() override {}
  struct Pos { int x = 0; };
  COMPONENT(Pos, pos)
};
} // namespace tw

TEST_CASE("CreateEntity into explicit tables isolates entities")
{
  ecs::Table a;
  ecs::Table b;
  tw::Node *na = tw::Node::create(a);
  tw::Node *nb = tw::Node::create(b);
  na->pos()->x = 1;
  nb->pos()->x = 2;
  REQUIRE(na->table == &a);
  REQUIRE(nb->table == &b);
  REQUIRE(na->pos()->x == 1);
  REQUIRE(nb->pos()->x == 2);
  REQUIRE(a.getManager<tw::Node>()->registy->size() == 1);
  REQUIRE(b.getManager<tw::Node>()->registy->size() == 1);
}
```

- [ ] **Step 2: Run test — expect fail** (API/storage still global)

```powershell
cmake --build build --config Debug
.\build\Debug\ecs_test.exe
```

- [ ] **Step 3: Implement storage move + CreateEntity(Table&) as above**

Also fix compile breakages:
- `test/main.cpp`: replace `&ecs::ComponentManager<Node>::inst()` dump args with `&ecs::current()->getOrCreateManager<Node>()` (or an entity’s `getComponentManager()`).
- Replace `REQUIRE(ecs::ComponentManager<Node>::inst().registy != nullptr)` with `REQUIRE(ecs::current()->getOrCreateManager<Node>().registy != nullptr)` **after** empty View still forces registry creation via View path (Task 3 may be needed if View still uses old inst — if so, temporarily keep View on `current()->getOrCreateManager` in this task).
- `test/memory_safety.cpp`: every `ComponentManager<T>::inst()` used for `.registy` / buffers → `ecs::current()->getOrCreateManager<T>()` or create into an explicit local `Table` and use that.

- [ ] **Step 4: Run full test suite — expect PASS**

```powershell
cmake --build build --config Debug
.\build\Debug\ecs_test.exe
```

- [ ] **Step 5: Commit**

```bash
git add src/ECS.hpp test/table_world.cpp test/main.cpp test/memory_safety.cpp
git commit -m "feat: move entity storage into Table-owned managers"
```

---

### Task 3: Route View through Table

**Files:**
- Modify: `src/ECS.hpp` (`View`, `ViewIterator`)
- Test: `test/table_world.cpp`

**Interfaces:**
- Consumes: `Table::getOrCreateManager<B>()`
- Produces:
  - `View()` uses `*current()`
  - `explicit View(Table& table)`
  - `ViewIterator` constructed from `IComponentManager&` / `ComponentManager<B>&` storage ref (not schema singleton)

- [ ] **Step 1: Write failing View isolation tests**

```cpp
TEST_CASE("View(table) does not see other table entities")
{
  ecs::Table a;
  ecs::Table b;
  tw::Node::create(a)->pos()->x = 10;
  tw::Node::create(b)->pos()->x = 20;

  int count_a = 0, sum_a = 0;
  for (auto [p] : ecs::View<tw::Node, tw::Node::Pos>(a))
  {
    ++count_a;
    sum_a += p->x;
  }
  REQUIRE(count_a == 1);
  REQUIRE(sum_a == 10);

  int count_b = 0, sum_b = 0;
  for (auto [p] : ecs::View<tw::Node, tw::Node::Pos>(b))
  {
    ++count_b;
    sum_b += p->x;
  }
  REQUIRE(count_b == 1);
  REQUIRE(sum_b == 20);
}

TEST_CASE("no-arg View follows current table")
{
  ecs::Table a;
  ecs::Table b;
  tw::Node::create(a);
  tw::Node::create(b);
  ecs::set_current(&a);
  int n = 0;
  for (auto [p] : ecs::View<tw::Node, tw::Node::Pos>())
  {
    (void)p;
    ++n;
  }
  REQUIRE(n == 1);
  ecs::set_current(nullptr);
}
```

- [ ] **Step 2: Run — expect fail** if View still global

- [ ] **Step 3: Implement**

```cpp
template <typename B, typename... Ts>
class ViewIterator : public RegistryBufferIterator<B>, public BufferIterator<Ts>...
{
public:
  ViewIterator() {}
  ViewIterator(IComponentManager &cm)
      : RegistryBufferIterator<B>(cm.registy),
        BufferIterator<Ts>(cm.template getOrCreateComponentBuffer<
                           std::remove_const_t<Ts>>())...
  {
  }
  // operators unchanged
};

template <typename B, typename... Ts>
class View
{
  Table *table_;
public:
  View() : View(*current()) {}
  explicit View(Table &table) : table_(&table)
  {
    auto *reg = table.template getOrCreateManager<B>()
                    .template getOrCreateRegistryComponentBuffer<B>();
    ensure_space(reg);
  }

  // ensure_space unchanged (uses cur->manager)

  ViewIterator<B, Ts...> begin()
  {
    return ViewIterator<B, Ts...>(table_->template getOrCreateManager<B>());
  }
  ViewIterator<B, Ts...> end() { return ViewIterator<B, Ts...>(); }
};
```

- [ ] **Step 4: Run full suite — PASS**

- [ ] **Step 5: Commit**

```bash
git add src/ECS.hpp test/table_world.cpp
git commit -m "feat: bind View to Table context or explicit Table"
```

---

### Task 4: ComponentRef uses owning Entity::table (cross-current safety)

**Files:**
- Modify: `src/ECS.hpp` (`ComponentRef` — only if still going through wrong manager)
- Test: `test/table_world.cpp`

**Interfaces:**
- Consumes: `Entity::table`, `ENTITY::getComponentManager()` already Table-local
- Produces: verified behavior — `ComponentRef` must use `entity->getComponentManager()` (already does via `CM()`); no use of `current()` inside `ComponentRef`

- [ ] **Step 1: Write failing/characterizing test**

```cpp
TEST_CASE("ComponentRef stays on owning table after current switches")
{
  ecs::Table a;
  ecs::Table b;
  tw::Node *na = tw::Node::create(a);
  na->pos()->x = 7;
  ecs::set_current(&b);
  tw::Node::create(b);
  REQUIRE(na->pos()->x == 7);
  na->pos()->x = 8;
  REQUIRE(na->pos()->x == 8);
  ecs::set_current(nullptr);
}
```

- [ ] **Step 2: Run test**

If `getComponentManager()` already uses `entity->table`, this should PASS after Task 2. If anything in `ComponentRef::getBuffer` still touches `ComponentManager<T>::inst()`, fix it to only use the `IComponentManager&` argument from `CM()`.

- [ ] **Step 3: Fix any remaining global lookups in ComponentRef / OptionalComponentRef**

`ComponentRef::CM()` must remain:

```cpp
IComponentManager &CM() const { return entity->getComponentManager(); }
```

- [ ] **Step 4: Run full suite — PASS**

- [ ] **Step 5: Commit**

```bash
git add src/ECS.hpp test/table_world.cpp
git commit -m "test: lock ComponentRef to owning Table across current switches"
```

---

### Task 5: Inheritance within one Table + context create()

**Files:**
- Test: `test/table_world.cpp`
- Modify: `src/ECS.hpp` only if inheritance parent registry linking inside Table is broken

**Interfaces:**
- Consumes: Table-local parent manager links from Task 2
- Produces: spec tests 3–5 covered

- [ ] **Step 1: Write tests**

```cpp
namespace tw
{
class Sprite : public Node
{
public:
  ENTITY(Sprite, Node)
  struct Img { int w = 0; };
  COMPONENT(Img, img)
};
} // namespace tw

TEST_CASE("same-table View<Node> includes Sprite; other table excluded")
{
  ecs::Table a;
  ecs::Table b;
  tw::Node::create(a);
  tw::Sprite::create(a);
  tw::Sprite::create(b);

  int n = 0;
  for (auto [p] : ecs::View<tw::Node, tw::Node::Pos>(a))
  {
    (void)p;
    ++n;
  }
  REQUIRE(n == 2); // Node + Sprite in a
}

TEST_CASE("CreateEntity() uses current table")
{
  ecs::Table a;
  ecs::set_current(&a);
  tw::Node *n = tw::Node::create();
  REQUIRE(n->table == &a);
  ecs::set_current(nullptr);
}

TEST_CASE("explicit CreateEntity(B) while current is A")
{
  ecs::Table a;
  ecs::Table b;
  ecs::set_current(&a);
  tw::Node *n = tw::Node::create(b);
  REQUIRE(n->table == &b);
  int n_a = 0;
  for (auto [p] : ecs::View<tw::Node, tw::Node::Pos>())
  {
    (void)p;
    ++n_a;
  }
  REQUIRE(n_a == 0);
  ecs::set_current(nullptr);
}
```

- [ ] **Step 2: Run — fix parent registry linking in `getOrCreateRegistryComponentBuffer` if `n != 2`**

Parent buffer must be `parent->getRegistryComponentBuffer<super>()` on the **same** `IComponentManager::parent` (Table-local), never another Table.

- [ ] **Step 3: Run full suite — PASS**

- [ ] **Step 4: Commit**

```bash
git add src/ECS.hpp test/table_world.cpp
git commit -m "test: cover Table inheritance and current create routing"
```

---

### Task 6: Compatibility cleanup + docs touch

**Files:**
- Modify: `test/main.cpp`, `test/memory_safety.cpp`, `test/visualize.cpp` (only if needed)
- Modify: `Readme.md` — short note on Table / `ScopedTable`
- Test: full `ecs_test`

- [ ] **Step 1: Grep for leftover storage `ComponentManager<T>::inst()`**

```powershell
rg "ComponentManager<.*<::inst\(" src test
```

Every hit must be either removed or intentionally schema-only (if schema `inst()` still exists with no `registy`). Prefer `current()->getOrCreateManager<T>()` or `entity.getComponentManager()`.

- [ ] **Step 2: Update Readme.md Basic Usage with a short Table example**

```cpp
ecs::Table level;
{
  ecs::ScopedTable guard(level);
  Node *a = Node::create();
  auto view = ecs::View<Node, Node::Position>();
}
// or: Node::create(level); ecs::View<Node, Node::Position>(level);
```

- [ ] **Step 3: Run full suite — PASS**

```powershell
cmake --build build --config Debug
.\build\Debug\ecs_test.exe
```

- [ ] **Step 4: Commit**

```bash
git add src/ECS.hpp test Readme.md CMakeLists.txt
git commit -m "docs: document Table world API and finish compatibility cleanup"
```

---

## Spec coverage checklist

| Spec item | Task |
| --- | --- |
| `Table`, `default_table`, `current`, `set_current`, `ScopedTable` | 1 |
| Table owns per-type registry + component buffers | 2 |
| `CreateEntity()` / `CreateEntity(Table&)` / `T::create(Table&)` | 2, 5 |
| `Entity::table` + ComponentRef via owning table | 2, 4 |
| `View()` + `View(Table&)` | 3 |
| Inheritance links within one Table | 2, 5 |
| Isolation / context / explicit override tests | 2–5 |
| Existing main/memory_safety on default Table | 2, 6 |
| Non-goals (migration, cross-view, serialize) | not implemented |

## Plan self-review

- No TBD placeholders in steps.
- `getOrCreateManager` / `CreateEntity` / `View` signatures consistent across tasks.
- `ComponentManager<T>::inst()` storage singleton is removed; call sites redirected in Task 2/6.
