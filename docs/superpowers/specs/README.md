# Design Specs

Approved design documents that drove the current `ECS.hpp` architecture.

| Spec | Status | Summary |
| --- | --- | --- |
| [2026-08-07-table-world-design.md](2026-08-07-table-world-design.md) | Implemented | Multi-world `Table`, thread-local current context, isolated storage |
| [2026-08-07-defer-commit-design.md](2026-08-07-defer-commit-design.md) | Implemented | Hybrid defer/commit, staging creates, generation-based destroy |

Related:

- Implementation plan (Table): [`../plans/2026-08-07-table-world.md`](../plans/2026-08-07-table-world.md)
- User-facing docs: [`../../API.md`](../../API.md), [`../../guide.md`](../../guide.md), [`../../testing.md`](../../testing.md)
- Library entry: [`../../../Readme.md`](../../../Readme.md)
