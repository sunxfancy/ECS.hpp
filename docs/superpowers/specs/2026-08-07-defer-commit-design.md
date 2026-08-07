# Defer / Commit Command Buffer Design

Date: 2026-08-07  
Status: Approved for planning

## Goal

During `View` iteration (or an explicit defer scope), entity create/destroy must not mutate the primary containers being iterated. Mutations go through a per-`Table` Command Buffer and land on `commit()`, avoiding iterator invalidation. Destroy includes a generation protocol and slot reuse.

## Decisions

| Topic | Choice |
| --- | --- |
| Trigger | Hybrid: auto-defer while a View is active; manual `begin_defer` / `ScopedDefer`; explicit `commit()` anytime |
| Buffered ops | Entity structural only: `CreateEntity` / `DestroyEntity` |
| Create visibility | Staging holds real objects; usable pointers returned immediately; current View does not see Staging; after `commit`, later Views see them |
| Architecture | Unified Command Buffer + Staging (not snapshot-only) |
| Destroy | Full protocol: deferred physical reclaim + generation / handle invalidation + freelist reuse |
| Ownership | Defer state and buffers live on `Table` (aligns with Table-world) |

## Architecture

```
Table
├── TypeStorage[T]          // published entities (main)
│   ├── registry
│   ├── components
│   ├── generation[]
│   └── freelist
├── Staging[T]              // unpublished creates (real, writable)
└── CommandBuffer
    ├── Create { type, staging_id }
    └── Destroy { type, id, generation }
```

### Runtime rules

| State | create | destroy | View sees |
| --- | --- | --- | --- |
| Not deferred | Write main storage | Immediate gen bump + reclaim / freelist | Latest main storage |
| Deferred | Write Staging; return Staging pointer | Record Destroy; bump gen immediately (old access invalid) | Main only; never Staging |
| After `commit()` | Staging merged into main (prefer freelist slots) | Physical reclaim applied | New entities visible; destroyed gone |

### Trigger (hybrid)

- `View` construction increments `defer_depth`; destruction decrements. Callers must keep the `View` object alive for the whole loop (normal usage). When depth hits 0, auto-`commit()`.
- `begin_defer()` / `end_defer()` or RAII `ScopedDefer` also adjust the same depth (manual defer without a View).
- `commit()` may run early while depth stays > 0: flush buffer, then continue accumulating.
- Staging entity ids are staging-local until commit; on commit they receive a main-storage id (freelist reuse preferred). **v1: `EntityHandle` from a Staging entity is only valid until that Table's next `commit`** (after commit, take a new handle from the published entity / `try_get` is not required to chase Staging→main). Raw Staging pointers are likewise invalid after commit.

## Public API

```cpp
namespace ecs {

void begin_defer();              // also begin_defer(Table&)
void end_defer();                // depth--; commit when depth hits 0
void commit();                   // apply CommandBuffer

struct ScopedDefer {
  ScopedDefer();
  ~ScopedDefer();
};

template <typename T> T* CreateEntity();
template <typename T> T* CreateEntity(Table& table);

void DestroyEntity(Entity* e);   // Entity::release() calls this

struct EntityHandle {
  Table* table;
  uint32_t id;
  uint32_t generation;
};

EntityHandle handle_of(const Entity* e);
Entity* try_get(EntityHandle h); // nullptr if gen mismatch or destroyed

} // namespace ecs
```

`Entity` gains `uint32_t generation` (separate from `flags`).

## Semantics

### Create (deferred)

1. Allocate in that Table's `Staging[T]`; set id / generation.
2. Return Staging pointer; `ComponentRef` resolves via the entity into Staging buffers.
3. Current View does not iterate Staging.

### Destroy

1. `DestroyEntity(nullptr)` is a no-op.
2. Bump generation (tombstone); subsequent `try_get` on old handles returns nullptr.
3. Not deferred: reclaim slot into freelist immediately; reset component slots as needed.
4. Deferred: enqueue `Destroy{type,id,gen}`; Views skip tombstones for the rest of the iteration.
5. Double-destroy of the same entity is a no-op (already invalid gen / tombstone).

### Commit order

1. Apply all Destroy commands (freelist).
2. Move Staging creates into main storage (reuse freelist when possible).
3. Clear Staging and the command queue.

Staging raw pointers become invalid after `commit`. Callers must finish using them before commit, or switch to `EntityHandle` / `try_get`.

### Deferred create then destroy

If a Staging entity is destroyed before commit, cancel it from Staging / commands so it never enters main storage.

### Nesting

Nested Views and nested `ScopedDefer` share a depth counter. Only the outermost exit (depth → 0) auto-commits.

### Cross-Table

`DestroyEntity` / commit operate on the entity's owning Table (or that Table's buffer). No cross-Table reclaim.

## Error handling / edge cases

| Case | Behavior |
| --- | --- |
| `commit` with empty buffer | no-op |
| Use Staging pointer after commit | undefined; use handles |
| Create/destroy outside defer | immediate (compatible with today's create + new destroy) |
| Destroy Table that is still `current` | same as Table-world: caller must not; out of scope here |

## Compatibility

- Existing no-arg `CreateEntity` / `View` keep working; without defer they remain immediate.
- Table-world context (`current` / `ScopedTable`) determines which Table owns the buffer when not passed explicitly.

## Non-goals (YAGNI)

- Command-buffering component add/remove
- Entity migration across Tables
- Concurrent multi-threaded commit

## Test plan

1. Existing `main` / `memory_safety` baseline cases still pass on default Table.
2. `CreateEntity` during View: no crash; current pass does not see new entities; after commit / View end, next View sees them.
3. Manual `ScopedDefer` without View: creates stay in Staging until `commit` / `end_defer`.
4. `DestroyEntity` + generation: old handle `try_get` → nullptr; slot reuse with different generation.
5. Destroy during View: no iterator invalidation from shrink; tombstones skipped; entity gone after commit.
6. Nested View / nested defer: depth correct; commit only when outermost ends.
7. Create-then-destroy while deferred: entity absent from main after commit.
