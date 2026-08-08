# Testing Guide

The suite builds a single binary `ecs_test` (CMake target) using [zeroerr](test/zeroerr.hpp) macros (`TEST_CASE`, `REQUIRE`, …).

## Running

```bash
# Prefer Clang — zeroerr TEST_CASE macros disagree with GCC _Pragma placement.
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++
cmake --build build -j
./build/ecs_test
# or: ctest --test-dir build --output-on-failure
```

MSVC:

```powershell
cmake -S . -B build
cmake --build build --config Debug
.\build\Debug\ecs_test.exe
# or: ctest --test-dir build -C Debug --output-on-failure
```

## Continuous Integration

Workflow: [`.github/workflows/ci.yml`](../.github/workflows/ci.yml)

| Job | Runner | Compiler | Config |
| --- | --- | --- | --- |
| Ubuntu Clang | `ubuntu-latest` | `clang++` | Debug |
| Ubuntu Clang Release | `ubuntu-latest` | `clang++` | Release |
| Windows MSVC | `windows-latest` | MSVC (VS 2022) | Debug |
| macOS AppleClang | `macos-latest` | AppleClang | Debug |

Triggers: pushes to `main` / `cursor/**`, and pull requests targeting `main`.  
Each job configures with CMake, builds `ecs_test`, then runs `ctest --output-on-failure`.

## Layout

| File | Role |
| --- | --- |
| `test/main.cpp` | Defines `ZEROERR_IMPLEMENTATION`; Node/Sprite smoke + empty-View regression |
| `test/table_world.cpp` | Table isolation, `ScopedTable`, current routing, ComponentRef ownership |
| `test/defer_commit.cpp` | Staging, nested defer/View, destroy, freelist, cross-table defer |
| `test/handles.cpp` | `handle_of` / `try_get` edge cases, visibility, freelist component reset |
| `test/view_systems.cpp` | Multi-component Views, 3-level inheritance, bulk iteration, systems-style updates |
| `test/visualize.cpp` | DOT dump helpers used by `main.cpp` (`dump`) |
| `test/memory_safety.cpp` | Characterization + target contracts (not linked by default; see below) |
| `test/debug_defer.cpp` | Standalone debug harness (not part of `ecs_test`) |
| `test/dsv.hpp` | Graphviz-oriented data-structure viz support |

`CMakeLists.txt` lists the linked sources explicitly. Add new `.cpp` files there when introducing suites.

## Conventions

1. **Isolate entity types per file/namespace** (`tw::`, `dc::`, `hd::`, `vs::`, …) so registries do not collide with `main.cpp`'s `Node`/`Sprite` on the default table.
2. Prefer creating a **local `ecs::Table`** inside each `TEST_CASE` unless you intentionally test `default_table()` / `current()`.
3. When a test changes `ecs::set_current`, restore with `ecs::set_current(nullptr)` before returning.
4. Use `ScopedDefer` / `View` scopes to express commit boundaries clearly.
5. Name cases after the behavior under test (`"try_get rejects wrong table"`), not ticket numbers.

## What each suite locks in

### `table_world.cpp`

- `current()` defaults to `default_table()`
- `ScopedTable` / `set_current` nesting and restore
- Explicit create/view isolation across tables
- `ComponentRef` stays on the owning table after current switches
- Same-table inheritance visibility for `View<Node>`

### `defer_commit.cpp`

- Staging invisible until commit
- Create/destroy during View
- Generation invalidation + freelist reuse
- Nested defer / nested View depth
- Manual `commit` while defer stays open
- Cross-table defer independence
- Mid-loop destroy of the current entity

### `handles.cpp`

- Empty / wrong-table / wrong-type / OOB handles
- Component slot reset on freelist reuse
- Staging handle not remapped after publish (v1 contract)
- `is_entity_visible`
- No-arg `begin_defer` / `ScopedDefer` against current table

### `view_systems.cpp`

- Empty view creates registry safely
- Position ← Velocity integration style loop
- Three-level inheritance (`Node` → `Sprite` → `AnimatedSprite`)
- Zip alignment across many entities
- Bulk visit counts
- Table destructor smoke

## Adding a new test file

1. Create `test/my_suite.cpp` with `#include "zeroerr.hpp"` and `#include "ECS.hpp"`.
2. Put entity types in an anonymous or unique namespace.
3. Append the path to `TEST_SOURCES` in `CMakeLists.txt`.
4. Build and run `ecs_test`; fix failures before committing.

Do **not** define `ZEROERR_IMPLEMENTATION` outside `main.cpp`.

## `memory_safety.cpp` (opt-in)

This file documents current vs desired memory-safety contracts:

- `[char]` cases characterize today's behavior (should pass).
- `[target]` cases encode the safe end-state and may fail until fixes land.

It is **not** listed in `TEST_SOURCES` so the default suite stays green. To experiment:

```cmake
list(APPEND TEST_SOURCES ${CMAKE_SOURCE_DIR}/test/memory_safety.cpp)
```

Treat failing `[target]` cases as a backlog for hardening work, not as regressions of the deferred-create / table features.

## Design-spec coverage

| Spec scenario | Primary tests |
| --- | --- |
| Multi-table isolation | `table_world.cpp` |
| Current / ScopedTable | `table_world.cpp`, `handles.cpp` |
| View create deferral | `defer_commit.cpp`, `view_systems.cpp` |
| Destroy + generation | `defer_commit.cpp`, `handles.cpp` |
| Nested defer depth | `defer_commit.cpp` |
| Inheritance within one table | `table_world.cpp`, `view_systems.cpp` |
| Staging publish components | `defer_commit.cpp`, `view_systems.cpp` |
