#include <ECS.hpp>

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
class Root : public ecs::Entity {
public:
    ENTITY(Root, ecs::Entity)
    void release() override { ecs::DestroyEntity(this); }
};
class Left : public Root {
public:
    ENTITY(Left, Root)
};
class Leaf : public Left {
public:
    ENTITY(Leaf, Left)
};
class Right : public Root {
public:
    ENTITY(Right, Root)
};
struct Value {
    int owner = 0;
};
struct Explicit {
    explicit Explicit(int input) : owner(input) {}
    int owner;
};
template <typename T, typename C = Value>
int count(ecs::Table& table) {
    int result = 0;
    for (auto row : ecs::View<T, C>(table)) {
        (void)row;
        require(++result < 100, "query did not terminate within its subtree");
    }
    return result;
}
void scope() {
    for (bool reverse : {false, true}) {
        ecs::Table table;
        Root::create(table);
        if (reverse) {
            Right::create(table);
            Leaf::create(table);
        } else {
            Leaf::create(table);
            Right::create(table);
        }
        Left::create(table);
        require(count<Left>(table) == 2, "subtype query escaped its subtree");
        require(count<Leaf>(table) == 1, "leaf query escaped its subtree");
        require(count<Right>(table) == 1, "sibling query escaped its subtree");
        require(table.getOrCreateManager<Root>().getComponentBuffer<Value>() != nullptr,
                "parent component link missing");
        require(count<Root>(table) == 4, "base query missed descendants");
        int entities = 0;
        for (auto it = ecs::View<Left>(table).begin(); it != ecs::View<Left>(table).end(); ++it) {
            require(dynamic_cast<Left*>(static_cast<ecs::Entity*>(it.entity())) != nullptr,
                    "entity-only query returned wrong dynamic type");
            require(++entities < 100, "entity-only query did not terminate");
        }
        require(entities == 2, "entity-only subtype scope incorrect");
    }
    {
        ecs::Table table;
        Left::create(table);
        Right::create(table);
        ecs::RegistryBufferIterator<Right> cursor;
        cursor.setCB(table.getOrCreateManager<Right>().getOrCreateRegistryComponentBuffer<Right>());
        int records = 0;
        while (!cursor.at_end()) {
            require(dynamic_cast<Right*>(cursor.entity()) != nullptr, "restarted registry cursor escaped its subtree");
            require(++records < 100, "registry cursor did not terminate");
            ++cursor;
        }
        require(records == 1, "restarted registry cursor scope incorrect");
    }
    ecs::Table isolated;
    Left::create(isolated);
    Right::create(isolated);
    require(count<Left>(isolated) == 1, "isolated subtype count incorrect");
    require(isolated.getOrCreateManager<Right>().getComponentBuffer<Value>() == nullptr,
            "query prepared a sibling's components");
}
void alignment() {
    for (bool reverse : {false, true}) {
        ecs::Table table;
        auto*      left  = Left::create(table);
        auto*      right = Right::create(table);
        Right::create(table);
        auto initialize = [&](ecs::Entity* entity, int owner) {
            auto* buffer = entity->getComponentManager().getOrCreateComponentBuffer<Value>();
            buffer->emplace(entity->id);
            buffer->at(entity->id).owner = owner;
        };
        if (reverse) {
            initialize(left, 1);
            initialize(right, 2);
        } else {
            initialize(right, 2);
            initialize(left, 1);
        }
        int  rows = 0;
        auto view = ecs::View<Root, const Value>(table);
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [entity, value] = it.row();
            require(value != nullptr, "query returned absent component");
            const int expected = dynamic_cast<Left*>(entity) ? 1 : (entity->id == 0 ? 2 : 0);
            require(value->owner == expected, "entity/component columns are misaligned");
            require(++rows < 100, "misaligned columns prevented query termination");
        }
        require(rows == 3, "query missed an entity with uneven registry sizes");
        auto* explicitBuffer = left->getComponentManager().getOrCreateComponentBuffer<Explicit>();
        explicitBuffer->emplace(left->id, 17);
        require(count<Root, Explicit>(table) == 1, "non-default component filter incorrect");
        auto* leftBuffer = left->getComponentManager().getComponentBuffer<Value>();
        int   slots      = 0;
        for (auto it = leftBuffer->begin(); it != leftBuffer->end(); ++it) {
            require(++slots < 100, "component iterator escaped its subtree");
        }
        require(slots == 1, "standalone component iterator included a sibling");
    }
}
struct Resource {
    inline static int alive = 0;
    Resource() { ++alive; }
    Resource(Resource&&) { ++alive; }
    ~Resource() { --alive; }
};
class Fallible : public ecs::Entity {
public:
    ENTITY(Fallible, ecs::Entity)
    Resource         resource;
    std::vector<int> data = std::vector<int>(8, 42);
    explicit Fallible(bool fail) {
        if (fail) throw std::runtime_error("injected entity construction failure");
    }
    Fallible(const Fallible&) = delete;
    Fallible(Fallible&&)      = default;
    void release() override { ecs::DestroyEntity(this); }
};
void expectFailure(ecs::Table& table) {
    bool threw = false;
    try {
        Fallible::create(table, true);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    require(threw, "entity constructor did not throw");
}
void reuse() {
    require(Resource::alive == 0, "resource state leaked from another case");
    {
        ecs::Table table;
        expectFailure(table);
        require(count<Fallible>(table) == 0, "failed fresh construction published an entity");
        auto*      entity = Fallible::create(table, false);
        const auto handle = ecs::handle_of(entity);
        const auto id     = entity->id;
        ecs::DestroyEntity(entity);
        for (int attempt = 0; attempt < 2; ++attempt) {
            expectFailure(table);
            require(ecs::try_get(handle) == nullptr, "failed reuse revived a stale handle");
            require(count<Fallible>(table) == 0, "failed reuse became query-visible");
            auto* registry = table.getOrCreateManager<Fallible>().getOrCreateRegistryComponentBuffer<Fallible>();
            auto  begin    = registry->beginEntity();
            auto  end      = registry->endEntity();
            require(*begin == *end, "raw entity iterator exposed an unconstructed slot");
            require(Resource::alive == 0, "failed construction leaked a resource");
        }
        auto* replacement = Fallible::create(table, false);
        require(replacement->id == id, "failed reuse lost its reusable slot");
        require(ecs::try_get(handle) == nullptr, "successful retry revived a stale handle");
        require(replacement->data.front() == 42, "retry lost constructor arguments or resources");
        const auto replacementHandle = ecs::handle_of(replacement);
        ecs::DestroyEntity(replacement);
        {
            ecs::ScopedDefer defer(table);
            expectFailure(table);
            auto*      staged       = Fallible::create(table, false);
            const auto stagedHandle = ecs::handle_of(staged);
            require(ecs::try_get(stagedHandle) == staged, "staged retry handle is invalid");
            require(count<Fallible>(table) == 0, "staged retry became visible before commit");
            require(ecs::try_get(replacementHandle) == nullptr, "staged retry revived old generation");
        }
        require(count<Fallible>(table) == 1, "staged retry was not published");
    }
    require(Resource::alive == 0, "Table destruction leaked or destroyed resources twice");
    {
        ecs::Table table;
        ecs::DestroyEntity(Fallible::create(table, false));
        expectFailure(table);
        // Destruction directly after failed reuse must also be safe.
    }
    require(Resource::alive == 0, "failed-slot Table destruction corrupted resource lifetime");
}
}  // namespace

int main(int argc, char** argv) {
    try {
        const std::string selected = argc > 1 ? argv[1] : "all";
        if (selected == "scope" || selected == "all") scope();
        if (selected == "alignment" || selected == "all") alignment();
        if (selected == "reuse" || selected == "all") reuse();
        require(selected == "scope" || selected == "alignment" || selected == "reuse" || selected == "all",
                "unknown regression case");
        std::cout << selected << ": passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
