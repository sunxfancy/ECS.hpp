#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <tuple>
#include <type_traits>
#include <typeindex>
#include <utility>
#include <vector>

#define COMPONENT(T, name) \
  ecs::ComponentRef<T> name() { return ecs::ComponentRef<T>(this); }

#define OPTIONAL_COMPONENT(T, name) \
  ecs::OptionalComponentRef<T> name() { return ecs::OptionalComponentRef<T>(this); }

// Both create() forms are templates so that they are instantiated only when
// called. A non-template create() would be compiled for every entity type
// eagerly, which would require every entity type to be default constructible
// even when it is only ever created with constructor arguments.
#define ENTITY(T, BASE)                                        \
  using super = BASE;                                          \
  ecs::IComponentManager &getComponentManager() const override \
  {                                                            \
    return *storage;                                           \
  }                                                            \
  template <class... Args>                                      \
  static T *create(Args &&...args)                              \
  {                                                            \
    return ecs::CreateEntity<T>(std::forward<Args>(args)...);   \
  }                                                            \
  template <class... Args>                                      \
  static T *create(ecs::Table &tbl, Args &&...args)             \
  {                                                            \
    return ecs::CreateEntity<T>(tbl, false,                    \
                                std::forward<Args>(args)...);   \
  }

namespace ecs
{

  class Entity;
  class Table;
  class IComponentManager;
  template <typename T>
  class ComponentManager;
  template <typename T>
  class ComponentBuffer;
  template <typename T>
  class RegistryComponentBuffer;
  template <typename T>
  class BufferIterator;

  inline constexpr uint32_t kEntityDead = 1;
  inline constexpr uint32_t kEntityStaging = 2;

  struct EntityHandle
  {
    Table *table = nullptr;
    std::type_index type{typeid(void)};
    uint32_t id = 0;
    uint32_t generation = 0;
  };

  class Table
  {
  public:
    Table() = default;
    Table(const Table &) = delete;
    Table &operator=(const Table &) = delete;

    ~Table()
    {
      for (auto &[key, cm] : managers_)
        delete_manager(cm);
      managers_.clear();
      for (auto &[key, cm] : staging_managers_)
        delete_manager(cm);
      staging_managers_.clear();
    }

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
      auto *cm = new ComponentManager<T>(this, parent_storage, false);
      managers_[key] = cm;
      return *cm;
    }

    template <typename T>
    ComponentManager<T> &getOrCreateStagingManager()
    {
      auto key = std::type_index(typeid(T));
      auto it = staging_managers_.find(key);
      if (it != staging_managers_.end())
        return *static_cast<ComponentManager<T> *>(it->second);

      IComponentManager *parent_storage = nullptr;
      if constexpr (!std::is_same_v<typename T::super, Entity>)
      {
        parent_storage = &getOrCreateStagingManager<typename T::super>();
      }
      auto *cm = new ComponentManager<T>(this, parent_storage, true);
      staging_managers_[key] = cm;
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

    IComponentManager *getManager(std::type_index type)
    {
      auto it = managers_.find(type);
      if (it == managers_.end())
        return nullptr;
      return it->second;
    }

    bool is_deferred() const { return defer_depth_ > 0; }

    void begin_defer() { ++defer_depth_; }

    void end_defer()
    {
      if (defer_depth_ > 0 && --defer_depth_ == 0)
        commit();
    }

    void commit();

  private:
    struct PendingDestroy
    {
      std::type_index type;
      uint32_t id;
      uint32_t generation;
      bool staging;
    };

    void delete_manager(IComponentManager *cm);
    void reclaim_entity(IComponentManager *cm, uint32_t id);
    void publish_staging();

    std::map<std::type_index, IComponentManager *> managers_;
    std::map<std::type_index, IComponentManager *> staging_managers_;
    int defer_depth_ = 0;
    std::vector<PendingDestroy> pending_destroys_;
    bool has_pending_main_staging_ = false;

    friend void begin_defer(Table &);
    friend void end_defer(Table &);
    template <typename T, typename... Args>
    friend T *CreateEntity(Table &, bool, Args &&...);
    friend void DestroyEntity(Entity *);
  };

  // EVEngine patch (cmake/patches/ecs-shared-default-table.patch): the default
  // Table is process-wide state. When the engine is split into per-layer shared
  // link groups (EVENGINE_MODULE_DLL) every image would get its own copy of the
  // function-local static below, so the storage lives in the foundation link unit
  // (src/engine/common/EcsDefaultTable.cpp) and each image calls into it. OBJECT /
  // static builds keep the inline definition and are byte-for-byte unchanged.
#if defined(EVENGINE_MODULE_DLL)
#  if defined(_WIN32)
#    if defined(EVENGINE_EXPORTS_FOUNDATION)
#      define EVE_ECS_DEFAULT_TABLE_API __declspec(dllexport)
#    else
#      define EVE_ECS_DEFAULT_TABLE_API __declspec(dllimport)
#    endif
#  elif defined(__GNUC__) || defined(__clang__)
#    define EVE_ECS_DEFAULT_TABLE_API __attribute__((visibility("default")))
#  else
#    define EVE_ECS_DEFAULT_TABLE_API
#  endif
  EVE_ECS_DEFAULT_TABLE_API Table &engine_default_table();
#endif

  inline Table &default_table()
  {
#if defined(EVENGINE_MODULE_DLL)
    return engine_default_table();
#else
    static Table t;
    return t;
#endif
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

  inline void begin_defer() { current()->begin_defer(); }
  inline void begin_defer(Table &t) { t.begin_defer(); }

  inline void end_defer() { current()->end_defer(); }
  inline void end_defer(Table &t) { t.end_defer(); }

  inline void commit() { current()->commit(); }
  inline void commit(Table &t) { t.commit(); }

  struct ScopedDefer
  {
    ScopedDefer() : table_(current()) { table_->begin_defer(); }
    explicit ScopedDefer(Table &t) : table_(&t) { table_->begin_defer(); }
    ~ScopedDefer() { table_->end_defer(); }
    ScopedDefer(const ScopedDefer &) = delete;
    ScopedDefer &operator=(const ScopedDefer &) = delete;

  private:
    Table *table_;
  };

  /**
   * @brief Entity 是一个抽象类，用于表示一个实体，实体是一个具有一定属性的对象
   *
   * Entity 本身并不存储任何数据，而是通过 Component 来存储数据
   * Entity 是所有用户定义的实体类的基类，而且会被系统自动用来创建 Registry 来存放其下的所有实例
   */
  class Entity
  {
  public:
    virtual void release() = 0;
    virtual IComponentManager &getComponentManager() const = 0;

    uint32_t id = 0;
    uint32_t flags = 0;
    uint32_t generation = 0;
    Table *table = nullptr;
    IComponentManager *storage = nullptr;
  };

  inline bool is_entity_visible(const Entity *e)
  {
    return e && !(e->flags & (kEntityDead | kEntityStaging));
  }

  /**
   * @brief IComponentBuffer 是一个抽象类，用于表示一个存储 Component 数据的容器
   */
  /**
   * @brief What every buffer in a Table shares: identity, size and the
   *        base/derived buffer links used by View recursion.
   *
   * Carries no operation typed on the stored value. A buffer that only ever
   * holds entity records must not be forced to instantiate value-slot
   * operations it never performs: a virtual of a class template is
   * instantiated whenever the class is, so a value-typed virtual here would
   * impose its type requirements on every buffer, including the entity root.
   */
  class IComponentBuffer
  {
  public:
    virtual ~IComponentBuffer() = default;
    virtual uint32_t size() const = 0;
    virtual const std::type_info &getType() const = 0;

    IComponentManager *manager = nullptr;
    IComponentBuffer *parent = nullptr;
    IComponentBuffer *children = nullptr, *next = nullptr;
  };

  /**
   * @brief Value-slot operations, valid only for a buffer that stores an
   *        attached component.
   *
   * These are the operations that create, overwrite or transfer a stored
   * value, so only a component buffer implements them. Keeping them off
   * IComponentBuffer is what lets an entity record be move-only and
   * non-default-constructible.
   */
  class IComponentSlotBuffer : public IComponentBuffer
  {
  public:
    virtual void ensure_space(uint32_t) = 0;
    virtual void transfer_slot(uint32_t from_id, uint32_t to_id,
                               IComponentSlotBuffer *from) = 0;
    virtual IComponentSlotBuffer *ensure_equivalent(IComponentManager *dst_cm) = 0;
    virtual void reset_slot(uint32_t id) = 0;
  };

  class IEntityIterator
  {
  public:
    virtual ~IEntityIterator() = default;
    virtual IEntityIterator &operator++(int) = 0;
    virtual bool operator==(const IEntityIterator &other) = 0;
    virtual bool operator!=(const IEntityIterator &other) = 0;
    virtual Entity *operator->() = 0;
    virtual Entity &operator*() = 0;
  };
  typedef std::unique_ptr<IEntityIterator> IEntityIteratorPtr;

  class IRegistryComponentBuffer
  {
  public:
    virtual ~IRegistryComponentBuffer() = default;
    virtual Entity *getEntity(uint32_t id) = 0;
    virtual IEntityIteratorPtr beginEntity() = 0;
    virtual IEntityIteratorPtr endEntity() = 0;
    virtual void reclaim(uint32_t id) = 0;
    virtual uint32_t entity_count() const = 0;
    virtual Entity *entity_at(uint32_t index) = 0;
  };

  class IComponentManager
  {
    template <typename T> friend class RegistryComponentBuffer;
    template <typename B, typename... Ts> friend class View;
    friend void DestroyEntity(Entity *);
    // Invalidation index only; entity flags remain the liveness authority.
    uint64_t entity_revision_ = 0;

  public:
    Table *table = nullptr;
    IComponentManager *parent = nullptr;
    bool staging = false;

    IComponentBuffer *registy = nullptr;
    std::map<std::type_index, IComponentSlotBuffer *> components;

    virtual ~IComponentManager() = default;
    virtual const std::type_info &getType() const = 0;
    virtual IComponentManager *ensure_main_manager(Table *table) = 0;
    virtual void publish_from(IComponentManager *staging_cm) = 0;
    virtual void reclaim(uint32_t id) = 0;

    template <typename T>
    ComponentBuffer<T> *getComponentBuffer()
    {
      auto it = components.find(std::type_index(typeid(T)));
      if (it == components.end())
        return nullptr;
      return dynamic_cast<ComponentBuffer<T> *>(it->second);
    }

    template <typename T>
    RegistryComponentBuffer<T> *getRegistryComponentBuffer()
    {
      if (registy == nullptr)
        return nullptr;
      return dynamic_cast<RegistryComponentBuffer<T> *>(registy);
    }

    template <typename T>
    ComponentBuffer<T> *getOrCreateComponentBuffer()
    {
      auto it = components.find(std::type_index(typeid(T)));
      if (it == components.end())
      {
        // Always ensure the parent buffer exists first so inheritance links
        // (parent→children) are formed even when a subclass component is
        // touched before any base-type entity/buffer was created.
        IComponentBuffer *parent_buf = nullptr;
        if (parent != nullptr)
          parent_buf = parent->template getOrCreateComponentBuffer<T>();
        auto *cb = new ComponentBuffer<T>(this, parent_buf);
        components[std::type_index(typeid(T))] = cb;
        return cb;
      }
      return dynamic_cast<ComponentBuffer<T> *>(it->second);
    }

    template <typename T>
    RegistryComponentBuffer<T> *getOrCreateRegistryComponentBuffer()
    {
      if (registy == nullptr)
      {
        // Recursively create the super registry in this Table so View<Base>
        // can walk children links when only a subclass was constructed
        // (including deferred publish into a previously empty base).
        IComponentBuffer *pcb = nullptr;
        if (parent != nullptr)
        {
          if constexpr (!std::is_same_v<typename T::super, Entity>)
            pcb = parent->template getOrCreateRegistryComponentBuffer<typename T::super>();
        }
        registy = new RegistryComponentBuffer<T>(this, pcb);
      }
      return dynamic_cast<RegistryComponentBuffer<T> *>(registy);
    }
  };

  template <typename B>
  class ComponentManager : public IComponentManager
  {
  public:
    const std::type_info &getType() const override { return typeid(B); }

    ComponentManager(Table *t, IComponentManager *parent_storage, bool is_staging)
    {
      this->table = t;
      this->parent = parent_storage;
      this->staging = is_staging;
    }

    IComponentManager *ensure_main_manager(Table *table) override
    {
      return &table->template getOrCreateManager<B>();
    }

    void publish_from(IComponentManager *staging_cm) override
    {
      auto *s_reg = dynamic_cast<RegistryComponentBuffer<B> *>(staging_cm->registy);
      if (s_reg == nullptr)
        return;

      auto *m_reg = getOrCreateRegistryComponentBuffer<B>();

      for (uint32_t sid = 0; sid < s_reg->entity_count(); ++sid)
      {
        Entity *se = s_reg->entity_at(sid);
        if (se == nullptr || (se->flags & kEntityDead))
          continue;

        uint32_t preserved_gen = se->generation ? se->generation : 1;
        // Construct the record in place from the staging value. The staging
        // buffers are destroyed immediately after publish_staging() returns,
        // so moving is both safe and the only requirement placed on B.
        uint32_t mid = m_reg->emplace(std::move(*static_cast<B *>(s_reg->entity_at(sid))));
        B &me = *static_cast<B *>(m_reg->entity_at(mid));
        me.id = mid;
        me.flags &= ~(kEntityStaging | kEntityDead);
        me.storage = this;
        me.generation = preserved_gen;
        me.table = table;

        for (auto &[key, s_buf] : staging_cm->components)
        {
          (void)key;
          IComponentSlotBuffer *m_buf = s_buf->ensure_equivalent(this);
          m_buf->transfer_slot(sid, mid, s_buf);
        }
      }
    }

    void reclaim(uint32_t id) override
    {
      auto *reg = getRegistryComponentBuffer<B>();
      if (reg != nullptr)
        reg->reclaim(id);
    }
  };

  /**
   * @brief Raw, correctly aligned storage for one T.
   *
   * sizeof(SlotStorage<T>) == sizeof(T) for every well formed T, so a container
   * of these has exactly the layout of a container of T. Presence is tracked
   * out of band by CommonComponentBuffer's bitmap, the same way std::vector
   * tracks a constructed prefix with a size counter instead of a per element
   * flag. A tag stored next to each value (as std::optional does) would add
   * sizeof(T) rounded up to alignof(T) per slot -- 50% for an 8 byte component
   * and 100% for a float -- on the arrays a View walks every frame.
   */
  template <typename T>
  struct SlotStorage {
    alignas(T) unsigned char bytes[sizeof(T)];
  };

  /**
   * @brief Slot storage for an attached component.
   *
   * "The slot exists" and "a value is alive in it" are two separate facts: the
   * slot array holds raw storage, and a bitmap records which slots hold a live
   * value. That is the split std::vector makes between capacity and size, and
   * it is what lets the storage level avoid constructing a value nobody asked
   * for: growing the slot array, discarding a slot and transferring a slot all
   * touch only raw storage and the bitmap, so they require nothing of T beyond
   * move-constructibility and destructibility.
   *
   * The operations that DO construct a value (get(), emplace(), engageUpTo())
   * require T to be constructible for that particular call. They are ordinary
   * members, not virtuals, so they are instantiated only for component types
   * that actually use them: a component always constructed explicitly from
   * arguments never needs a default constructor.
   */
  template <typename T>
  class CommonComponentBuffer : public IComponentSlotBuffer
  {
    template <typename B, typename... Ts>
    friend class View;
    uint64_t presence_revision_ = 0;
    uint64_t prepared_presence_revision_ = 0;
    uint64_t prepared_entity_revision_ = 0;
    uint32_t prepared_size_ = 0;
    bool preparation_valid_ = false;
    // The whole point of tracking presence out of band: a component buffer must
    // keep the exact layout it had when it stored T directly. If this ever
    // fires, per-slot overhead has crept back into the arrays a View walks.
    static_assert(sizeof(SlotStorage<T>) == sizeof(T),
                  "component slot storage must not change the component layout");

  public:
    std::deque<SlotStorage<T>> container;

    /** @brief Whether slot @p id currently holds a live value. */
    bool has(uint32_t id) const
    {
      const size_t word = static_cast<size_t>(id) >> 6;
      return id < container.size() && word < live_.size() &&
             ((live_[word] >> (id & 63u)) & 1ull) != 0;
    }

    /**
     * @brief Constructs a value in slot @p id when none is alive.
     * @param args Forwarded to T's constructor.
     * @return True when this call constructed the value.
     */
    template <typename... Args>
    bool emplace(uint32_t id, Args &&...args)
    {
      if (id >= container.size())
        container.resize(id + 1);
      if (has(id))
        return false;
      ::new (static_cast<void *>(container[id].bytes)) T(std::forward<Args>(args)...);
      mark(id);
      return true;
    }

    /** @brief Borrows the live value in slot @p id; the caller proved existence. */
    T &at(uint32_t id) { return *std::launder(reinterpret_cast<T *>(container[id].bytes)); }
    const T &at(uint32_t id) const
    {
      return *std::launder(reinterpret_cast<const T *>(container[id].bytes));
    }

    /** @brief Typed pointer to slot @p id's raw storage. */
    T *ptr(uint32_t id) { return std::launder(reinterpret_cast<T *>(container[id].bytes)); }

    /**
     * @brief Borrows slot @p id, constructing a value there when it is empty.
     * @remarks This is the operation that requires T to be default
     *          constructible, and only for component types whose accessor is
     *          actually used this way.
     */
    T &get(uint32_t id)
    {
      if (id >= container.size())
        container.resize(id + 1);
      if (!has(id))
      {
        ::new (static_cast<void *>(container[id].bytes)) T();
        mark(id);
      }
      return at(id);
    }

    uint32_t size() const override { return static_cast<uint32_t>(container.size()); }

    const std::type_info &getType() const override { return typeid(T); }

    /**
     * @brief Grows the slot array to @p new_size, leaving the new slots empty.
     * @remarks Constructs no value, so this requires nothing of T.
     */
    void ensure_space(uint32_t new_size) override
    {
      if (new_size > container.size())
        container.resize(new_size);
    }

    /**
     * @brief Moves one component slot from a staging buffer into this buffer.
     *
     * Move, not copy: the only caller is publish_from(), which runs immediately
     * before the staging buffers are destroyed, so the source value is dead
     * either way. Move construction is the only operation required of T.
     */
    void transfer_slot(uint32_t from_id, uint32_t to_id,
                       IComponentSlotBuffer *from) override
    {
      auto *fb = dynamic_cast<CommonComponentBuffer<T> *>(from);
      if (to_id >= container.size())
        container.resize(to_id + 1);
      if (has(to_id))
      {
        std::destroy_at(ptr(to_id));
        unmark(to_id);
      }
      if (fb->has(from_id))
      {
        ::new (static_cast<void *>(container[to_id].bytes)) T(std::move(*fb->ptr(from_id)));
        mark(to_id);
        std::destroy_at(fb->ptr(from_id));
        fb->unmark(from_id);
      }
    }

    IComponentSlotBuffer *ensure_equivalent(IComponentManager *dst_cm) override
    {
      return dst_cm->template getOrCreateComponentBuffer<T>();
    }

    /**
     * @brief Discards the value in slot @p id.
     * @remarks Destroys instead of reassigning, so no value is constructed and
     *          nothing is required of T.
     */
    void reset_slot(uint32_t id) override
    {
      if (!has(id))
        return;
      std::destroy_at(ptr(id));
      unmark(id);
    }

    CommonComponentBuffer(IComponentManager *cm, IComponentBuffer *pcb)
    {
      manager = cm;

      if (cm->parent != nullptr)
      {
        if (pcb == nullptr)
          return;
        if (pcb->children == nullptr)
          pcb->children = this;
        else
        {
          IComponentBuffer *old_head = pcb->children;
          pcb->children = this;
          this->next = old_head;
        }
        parent = pcb;
      }
    }

  private:
    friend class ComponentBuffer<T>;

    void ensureWord(uint32_t id)
    {
      const size_t word = (static_cast<size_t>(id) >> 6) + 1;
      if (word > live_.size())
        live_.resize(word, 0);
    }
    void mark(uint32_t id)
    {
      ++presence_revision_;
      ensureWord(id);
      live_[static_cast<size_t>(id) >> 6] |= (1ull << (id & 63u));
    }
    void unmark(uint32_t id)
    {
      ++presence_revision_;
      const size_t word = static_cast<size_t>(id) >> 6;
      if (word < live_.size())
        live_[word] &= ~(1ull << (id & 63u));
    }

    /** @brief One bit per slot: the out of band record of which slots are alive. */
    std::vector<uint64_t> live_;
  };

  template <typename T>
  class ComponentBuffer : public CommonComponentBuffer<T>
  {
  public:
    ComponentBuffer(IComponentManager *cm, IComponentBuffer *pcb)
        : CommonComponentBuffer<T>(cm, pcb) {}

    /**
     * @brief Grows to @p new_size and gives every slot a value.
     * @remarks This is where "every live entity has this component" is
     *          materialised, so it is the operation that requires T to be
     *          default constructible. It is not virtual, so it is instantiated
     *          only for the component types a View or accessor actually asks
     *          for; a component always constructed from arguments never
     *          instantiates it.
     */
    void engageUpTo(uint32_t new_size)
    {
      if (new_size > this->container.size())
        this->container.resize(new_size);
      // A full presence word proves that its values need no construction.
      // Re-read incomplete words slot by slot: constructors can change later
      // slots, and retries after a throwing constructor must preserve order.
      for (uint64_t first = 0; first < new_size; first += 64)
      {
        const uint32_t count = static_cast<uint32_t>(
            std::min<uint64_t>(64, uint64_t(new_size) - first));
        const uint64_t mask = count == 64 ? ~uint64_t(0)
                                         : (uint64_t(1) << count) - 1;
        const size_t word = static_cast<size_t>(first >> 6);
        if (word < this->live_.size() && (this->live_[word] & mask) == mask)
          continue;
        for (uint32_t offset = 0; offset < count; ++offset)
        {
          const uint32_t id = static_cast<uint32_t>(first) + offset;
          if (!this->has(id))
            this->emplace(id);
        }
      }
    }

    BufferIterator<T> begin() { return BufferIterator<T>(this); }
    BufferIterator<T> end() { return BufferIterator<T>(); }
  };

  template <typename T>
  class EntityIterator : public IEntityIterator
  {
  public:
    EntityIterator(IRegistryComponentBuffer *registry, uint32_t index)
        : registry_(registry), index_(index), end_(registry->entity_count())
    {
      skip_unconstructed();
    }

    IEntityIterator &operator++(int) override
    {
      if (index_ < end_)
        ++index_;
      skip_unconstructed();
      return *this;
    }

    bool operator==(const IEntityIterator &other) override
    {
      auto *o = dynamic_cast<const EntityIterator<T> *>(&other);
      return o && registry_ == o->registry_ && index_ == o->index_;
    }
    bool operator!=(const IEntityIterator &other) override { return !(*this == other); }
    Entity *operator->() override { return registry_->entity_at(index_); }
    Entity &operator*() override { return *operator->(); }

  private:
    void skip_unconstructed()
    {
      while (index_ < end_ && registry_->entity_at(index_) == nullptr)
        ++index_;
    }
    IRegistryComponentBuffer *registry_;
    uint32_t index_;
    uint32_t end_;
  };

  /**
   * @brief Slot storage for entity records of type T (the entity root).
   *
   * Deliberately does NOT derive from CommonComponentBuffer. That class
   * implements the value-slot virtuals, and a virtual of a class template is
   * instantiated whenever the class is, so inheriting from it would require T
   * to be default-constructible and copy-assignable even though an entity
   * record is never used as a component. Here T only has to be constructible
   * from the arguments create() forwards, and destructible.
   */
  template <typename T>
  class RegistryComponentBuffer : public IComponentBuffer,
                                  public IRegistryComponentBuffer
  {
  public:
    RegistryComponentBuffer(const RegistryComponentBuffer &) = delete;
    RegistryComponentBuffer &operator=(const RegistryComponentBuffer &) = delete;

    RegistryComponentBuffer(IComponentManager *cm, IComponentBuffer *pcb)
    {
      manager = cm;

      if (cm->parent != nullptr)
      {
        if (pcb == nullptr)
          return;
        if (pcb->children == nullptr)
          pcb->children = this;
        else
        {
          IComponentBuffer *old_head = pcb->children;
          pcb->children = this;
          this->next = old_head;
        }
        parent = pcb;
      }
    }

    ~RegistryComponentBuffer() override
    {
      for (uint32_t id = 0; id < size(); ++id)
        if (constructed(id))
          std::destroy_at(record(id));
    }

    /**
     * @brief Constructs one entity record in a fresh or recycled slot.
     * @param args Forwarded to T's constructor; no argument means T().
     * @return The slot id the new record occupies; the registry owns the value.
     * @thread Call on the Table owner thread.
     * @reentrancy Record constructors/destructors must not structurally reenter
     *             the same registry.
     * @throws Propagates allocation or record-construction failures. A failed
     *         reuse leaves an unconstructed, retryable slot, never a live value.
     * @remarks A recycled slot preserves the retired incarnation's generation.
     * @cost Amortized constant storage work plus T construction/destruction.
     */
    template <typename... Args>
    uint32_t emplace(Args &&...args)
    {
      ++this->manager->entity_revision_;
      if (!freelist_.empty())
      {
        const RetiredSlot retired = freelist_.back();
        freelist_.pop_back();
        if (constructed(retired.id))
        {
          set_constructed(retired.id, false);
          std::destroy_at(record(retired.id));
        }
        try
        {
          new (static_cast<void *>(container_[retired.id].bytes)) T(std::forward<Args>(args)...);
        }
        catch (...)
        {
          // pop_back retained capacity; restoring this entry cannot allocate
          // under the no-structural-reentry record-construction contract.
          freelist_.push_back(retired);
          throw;
        }
        set_constructed(retired.id, true);
        record(retired.id)->generation = retired.generation;
        return retired.id;
      }
      const uint32_t id = size();
      // Allocate the lifetime bit before constructing user state.
      constructed_.resize((static_cast<size_t>(id) >> 6) + 1, 0);
      container_.emplace_back();
      try
      {
        new (static_cast<void *>(container_[id].bytes)) T(std::forward<Args>(args)...);
      }
      catch (...)
      {
        container_.pop_back();
        throw;
      }
      set_constructed(id, true);
      return id;
    }

    uint32_t size() const override { return static_cast<uint32_t>(container_.size()); }
    const std::type_info &getType() const override { return typeid(T); }

    /** @brief Borrows a constructed record, or nullptr; never creates a slot. */
    Entity *getEntity(uint32_t id) override
    {
      return constructed(id) ? record(id) : nullptr;
    }

    IEntityIteratorPtr beginEntity() override
    {
      return IEntityIteratorPtr(new EntityIterator<T>(this, 0));
    }
    IEntityIteratorPtr endEntity() override
    {
      return IEntityIteratorPtr(new EntityIterator<T>(this, size()));
    }

    void reclaim(uint32_t id) override
    {
      if (!constructed(id))
        return;
      freelist_.push_back({id, record(id)->generation});
      for (auto &[key, buf] : this->manager->components)
      {
        (void)key;
        buf->reset_slot(id);
      }
    }

    uint32_t entity_count() const override { return size(); }
    Entity *entity_at(uint32_t index) override { return getEntity(index); }

  private:
    struct RetiredSlot
    {
      uint32_t id;
      uint32_t generation;
    };
    // Raw slots keep entity stride/address stability; the bitmap owns C++
    // object lifetime, independently of dead/staging gameplay visibility.
    std::deque<SlotStorage<T>> container_;
    std::vector<uint64_t> constructed_;
    std::vector<RetiredSlot> freelist_;

    bool constructed(uint32_t id) const
    {
      const size_t word = static_cast<size_t>(id) >> 6;
      return id < size() && word < constructed_.size() &&
             (constructed_[word] & (uint64_t(1) << (id & 63u))) != 0;
    }
    void set_constructed(uint32_t id, bool value)
    {
      const uint64_t bit = uint64_t(1) << (id & 63u);
      if (value)
        constructed_[static_cast<size_t>(id) >> 6] |= bit;
      else
        constructed_[static_cast<size_t>(id) >> 6] &= ~bit;
    }
    T *record(uint32_t id)
    {
      return std::launder(reinterpret_cast<T *>(container_[id].bytes));
    }
  };

  // ------------------------------------------------------------------------

  template <typename T>
  class ComponentRef
  {
    const Entity *entity;

  public:
    ComponentRef(const Entity *ent) : entity(ent) {}

    IComponentManager &CM() const { return entity->getComponentManager(); }

    /**
     * @brief Borrows this entity's component.
     *
     * For a default-constructible component this is the convenience it has
     * always been: an empty slot is given a value on first touch. For a
     * component without a default constructor there is nothing to conjure, so
     * the accessor only ever observes: use emplace() to construct it first.
     * @remarks The if-constexpr keeps get() from being instantiated for a type
     *          that cannot satisfy it.
     */
    T &operator*() const
    {
      auto *buf = getBuffer(CM());
      if constexpr (std::is_default_constructible_v<T>)
        return buf->get(entity->id);
      else
        return buf->at(entity->id);
    }

    /** @brief Pointer to this entity's component; nullptr when it has none. */
    T *operator->() const
    {
      auto *buf = getBuffer(CM());
      if constexpr (std::is_default_constructible_v<T>)
        return &buf->get(entity->id);
      else
        return buf->has(entity->id) ? buf->ptr(entity->id) : nullptr;
    }

    /**
     * @brief Constructs this entity's component in place from @p args.
     * @return The component, whether it was just constructed or already alive.
     * @remarks This is the explicit-construction path. It is what a component
     *          without a default constructor uses: operator*() is only the
     *          convenience that materialises a default value, and it is never
     *          instantiated for a type that does not use it.
     */
    template <typename... Args>
    T &emplace(Args &&...args) const
    {
      auto *buf = getBuffer(CM());
      buf->emplace(entity->id, std::forward<Args>(args)...);
      return buf->at(entity->id);
    }

    /** @brief Whether this entity currently holds a value for the component. */
    bool has() const { return getBuffer(CM())->has(entity->id); }

    static ComponentBuffer<T> *getBuffer(IComponentManager &cm)
    {
      return cm.template getOrCreateComponentBuffer<T>();
    }
  };

  template <typename T>
  class OptionalComponentRef
  {
    const Entity *entity;

  public:
    OptionalComponentRef(const Entity *ent) : entity(ent) {}

    IComponentManager &CM() const { return entity->getComponentManager(); }
  };

  inline void Table::delete_manager(IComponentManager *cm)
  {
    if (cm == nullptr)
      return;

    for (auto &[key, buf] : cm->components)
      delete buf;
    cm->components.clear();

    if (cm->registy != nullptr)
    {
      delete cm->registy;
      cm->registy = nullptr;
    }

    delete cm;
  }

  inline void Table::reclaim_entity(IComponentManager *cm, uint32_t id)
  {
    if (cm == nullptr)
      return;
    cm->reclaim(id);
  }

  inline void Table::publish_staging()
  {
    // Deferred creates are reserved directly in main storage with kEntityStaging.
    // Commit promotes them in place so EntityHandle id/generation stay stable.
    // Creation is the only owner of staging admission. An idle View exit
    // must not scan every entity merely to clear flags that were never set.
    if (has_pending_main_staging_)
    {
      has_pending_main_staging_ = false;
      for (auto &[type, cm] : managers_)
      {
        (void)type;
        if (cm == nullptr || cm->registy == nullptr)
          continue;
        auto *reg = dynamic_cast<IRegistryComponentBuffer *>(cm->registy);
        if (reg == nullptr)
          continue;
        for (uint32_t id = 0; id < reg->entity_count(); ++id)
        {
          Entity *e = reg->entity_at(id);
          if (e == nullptr || (e->flags & kEntityDead) != 0)
            continue;
          e->flags &= ~kEntityStaging;
        }
      }
    }

    // Legacy path: move any records still sitting in staging_managers_.
    for (auto &[type, staging_cm] : staging_managers_)
    {
      (void)type;
      IComponentManager *main_cm = staging_cm->ensure_main_manager(this);
      main_cm->publish_from(staging_cm);
    }
  }

  inline void Table::commit()
  {
    for (const auto &d : pending_destroys_)
    {
      if (d.staging)
        continue;
      IComponentManager *cm = getManager(d.type);
      if (cm != nullptr)
        reclaim_entity(cm, d.id);
    }
    pending_destroys_.clear();

    publish_staging();

    for (auto &[key, cm] : staging_managers_)
      delete_manager(cm);
    staging_managers_.clear();
  }

  /**
   * @brief Creates one entity record of type T, forwarding constructor
   *        arguments to it.
   *
   * @param table Owning table.
   * @param force_main When true, creates a visible main entity even while deferred.
   * @param args Forwarded to T's constructor; no argument means T().
   * @return The new record, owned by @p table.
   * @remarks T only has to be constructible from @p args and destructible. A
   *          record is never copied, and is only default constructed when the
   *          caller passes no arguments.
   */
  template <typename T, typename... Args>
  T *CreateEntity(Table &table, bool force_main, Args &&...args)
  {
    // Deferred creates reserve a slot in main storage and mark it staging so
    // Views skip it until commit. That keeps EntityHandle identity stable and
    // avoids a second registry that would require default-constructible holes.
    const bool mark_staging = table.is_deferred() && !force_main;

    IComponentManager &cm =
        static_cast<IComponentManager &>(table.template getOrCreateManager<T>());

    auto *registry = cm.template getOrCreateRegistryComponentBuffer<T>();
    uint32_t id = registry->emplace(std::forward<Args>(args)...);
    T &inst = *static_cast<T *>(registry->entity_at(id));
    inst.id = id;
    inst.table = &table;
    inst.storage = &cm;
    if (mark_staging)
    {
      inst.flags |= kEntityStaging;
      table.has_pending_main_staging_ = true;
    }
    if (inst.generation == 0)
      inst.generation = 1;

    for (auto &[key, component] : cm.components)
    {
      (void)key;
      component->ensure_space(id + 1);
    }

    return &inst;
  }

  template <typename T, typename... Args>
  T *CreateEntity(Table &table, Args &&...args)
  {
    return CreateEntity<T>(table, false, std::forward<Args>(args)...);
  }

  template <typename T, typename... Args>
  T *CreateEntity(Args &&...args)
  {
    return CreateEntity<T>(*current(), false, std::forward<Args>(args)...);
  }

  inline void DestroyEntity(Entity *e)
  {
    if (e == nullptr || e->table == nullptr)
      return;
    if (e->flags & kEntityDead)
      return;

    ++e->getComponentManager().entity_revision_;
    e->generation++;
    e->flags |= kEntityDead;

    Table *table = e->table;

    if (table->is_deferred())
    {
      // Staging-flagged deferred creates live in main storage, so they use the
      // same pending reclaim path as ordinary destroys.
      table->pending_destroys_.push_back(
          {e->getComponentManager().getType(), e->id, e->generation, false});
    }
    else
    {
      e->getComponentManager().reclaim(e->id);
    }
  }

  inline EntityHandle handle_of(const Entity *e)
  {
    EntityHandle h;
    if (e == nullptr)
      return h;
    h.table = e->table;
    h.type = std::type_index(e->getComponentManager().getType());
    h.id = e->id;
    h.generation = e->generation;
    return h;
  }

  inline Entity *try_get(EntityHandle h)
  {
    if (h.table == nullptr)
      return nullptr;

    IComponentManager *cm = h.table->getManager(h.type);
    if (cm == nullptr || cm->registy == nullptr)
      return nullptr;

    auto *reg = dynamic_cast<IRegistryComponentBuffer *>(cm->registy);
    if (reg == nullptr)
      return nullptr;

    if (h.id >= reg->entity_count())
      return nullptr;

    Entity *e = reg->entity_at(h.id);
    if (e == nullptr)
      return nullptr;
    if (e->generation != h.generation)
      return nullptr;
    if (e->flags & kEntityDead)
      return nullptr;
    // Staging-flagged deferred creates are valid handle targets before commit.

    return e;
  }

  // ------------------------------------------------------------------------

  template <typename T>
  class BufferIterator
  {
  public:
    using CBType = ComponentBuffer<std::remove_const_t<T>>;

    BufferIterator() {}
    BufferIterator(CBType *_cb) : root_(_cb)
    {
      setCB(_cb);
    }

    BufferIterator &operator++()
    {
      step();
      return *this;
    }

    bool operator==(const BufferIterator &other) const
    {
      if (cb == nullptr)
        return other.cb == nullptr;
      if (cb == other.cb)
        return index_ == other.index_;
      return false;
    }

    bool operator!=(const BufferIterator &other) const { return !(*this == other); }

    /** @brief Slot index this iterator currently addresses. */
    uint32_t index() const { return index_; }

    /**
     * @brief Whether the current slot holds a value.
     * @remarks Liveness lives in the buffer's bitmap rather than in the slot,
     *          so it is queried by index. Slots stay index-locked across a
     *          View's buffers, so an empty slot is a legitimate state here
     *          rather than something to skip: the zip would desynchronise if
     *          one buffer advanced and another did not.
     */
    bool hasValue() const
    {
      return cb != nullptr && !at_current_end() && cb->has(index_);
    }

    T *operator->() { return hasValue() ? cb->ptr(index_) : nullptr; }
    T &operator*() { return cb->at(index_); }

    CBType *buffer() const { return cb; }

    bool exhausted() const { return cb == nullptr; }

    void step()
    {
      if (cb == nullptr)
        return;
      if (!at_current_end())
        ++index_;
      while (cb != nullptr && at_current_end())
        hop();
    }

  private:
    bool at_current_end() const
    {
      return cb == nullptr || index_ >= end_index_;
    }

    void setCB(CBType *_cb)
    {
      cb = _cb;
      index_ = 0;
      // Snapshot size so deferred CreateEntity can grow the deque without
      // invalidating this cursor or making mid-View creates visible.
      end_index_ = (cb != nullptr) ? static_cast<uint32_t>(cb->container.size()) : 0;
    }

    void hop()
    {
      if (cb == nullptr)
        return;
      if (cb->children != nullptr)
        setCB(dynamic_cast<CBType *>(cb->children));
      else
      {
        while (cb != root_ && cb->next == nullptr)
          cb = dynamic_cast<CBType *>(cb->parent);
        if (cb == root_)
          cb = nullptr;
        else
          setCB(dynamic_cast<CBType *>(cb->next));
      }
    }

    template <typename B, typename... Cs> friend class ViewIterator;
    CBType *root_ = nullptr;
    CBType *cb = nullptr;
    uint32_t index_ = 0;
    uint32_t end_index_ = 0;
  };

  template <typename T>
  class RegistryBufferIterator
  {
  public:
    RegistryBufferIterator() {}
    RegistryBufferIterator(IComponentBuffer *_cb) : root_(_cb) { setCB(_cb); }

    RegistryBufferIterator &operator++()
    {
      step();
      return *this;
    }

    bool operator==(const RegistryBufferIterator &other) const
    {
      if (cb == nullptr)
        return other.cb == nullptr;
      if (cb == other.cb)
        return index_ == other.index_;
      return false;
    }

    bool operator!=(const RegistryBufferIterator &other) const
    {
      return !(*this == other);
    }

    T *operator->()
    {
      return static_cast<T *>(entity());
    }

    T &operator*()
    {
      return *operator->();
    }

    Entity *entity() const
    {
      if (cb == nullptr || rcb == nullptr || at_current_end())
        return nullptr;
      return rcb->entity_at(index_);
    }

    bool at_end() const { return cb == nullptr; }

    bool at_current_end() const
    {
      return cb == nullptr || rcb == nullptr || index_ >= end_index_;
    }

    void step()
    {
      if (cb == nullptr)
        return;
      if (!at_current_end())
        ++index_;
      while (cb != nullptr && at_current_end())
        hop();
    }

    /** @brief Borrows a registry and restarts within its inheritance subtree. */
    void setCB(IComponentBuffer *_cb)
    {
      root_ = _cb;
      set_current_buffer(_cb);
    }

    IComponentBuffer *registry_cb() const { return cb; }

  private:
    template <typename B, typename... Cs> friend class ViewIterator;

    void set_current_buffer(IComponentBuffer *_cb)
    {
      if (_cb != nullptr)
      {
        cb = _cb;
        rcb = dynamic_cast<IRegistryComponentBuffer *>(_cb);
        index_ = 0;
        // Snapshot count: deferred CreateEntity appends into this same deque.
        // Index cursors (and a frozen end) stay valid; deque iterators would not.
        end_index_ = rcb ? rcb->entity_count() : 0;
      }
      else
      {
        cb = nullptr;
        rcb = nullptr;
        index_ = 0;
        end_index_ = 0;
      }
    }

    void hop()
    {
      if (cb == nullptr)
        return;
      if (cb->children != nullptr)
        set_current_buffer(cb->children);
      else
      {
        while (cb != root_ && cb->next == nullptr)
          cb = cb->parent;
        if (cb == root_)
          set_current_buffer(nullptr);
        else
          set_current_buffer(cb->next);
      }
    }

    IComponentBuffer *root_ = nullptr;
    IComponentBuffer *cb = nullptr;
    IRegistryComponentBuffer *rcb = nullptr;
    uint32_t index_ = 0;
    uint32_t end_index_ = 0;
  };

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
      advance_if_invalid();
    }

    ViewIterator &operator++()
    {
      step_all();
      advance_if_invalid();
      return *this;
    }

    bool operator==(const ViewIterator &other) const
    {
      if constexpr (sizeof...(Ts) == 0)
        return RegistryBufferIterator<B>::operator==(other);
      else
      {
        bool reg = RegistryBufferIterator<B>::operator==(other);
        bool ts[] = {BufferIterator<Ts>::operator==(other)...};
        return reg && std::all_of(ts, ts + sizeof...(Ts), [](bool b) { return b; });
      }
    }

    bool operator!=(const ViewIterator &other) const { return !(*this == other); }

    /**
     * @brief Borrowed pointer to the current entity record (base type B).
     * @lifetime Valid until the owning Table mutates this slot (destroy / freelist reuse).
     * @remarks Prefer this (or row()) over a second live-entity index when a system
     *          needs identity for DestroyEntity, Links, or handles.
     */
    B *entity() const
    {
      return static_cast<B *>(RegistryBufferIterator<B>::entity());
    }

    /**
     * @brief Component columns only (existing structured-binding shape).
     * @remarks Use row() when the entity pointer is needed in the same binding.
     */
    std::tuple<Ts *...> operator*()
    {
      return std::tuple<Ts *...>(BufferIterator<Ts>::operator->()...);
    }

    /** @brief Entity plus component columns: structured-bind as auto [e, a, b] = it.row(). */
    std::tuple<B *, Ts *...> row()
    {
      return std::tuple<B *, Ts *...>(entity(), BufferIterator<Ts>::operator->()...);
    }

  private:
    bool at_end() const { return RegistryBufferIterator<B>::at_end(); }

    Entity *current_entity() const
    {
      return RegistryBufferIterator<B>::entity();
    }

    bool current_entity_visible() const
    {
      return is_entity_visible(current_entity());
    }

    void step_all()
    {
      auto *previous = RegistryBufferIterator<B>::registry_cb();
      RegistryBufferIterator<B>::operator++();
      auto *registry = RegistryBufferIterator<B>::registry_cb();
      if (registry != previous)
      {
        // Bind by concrete Manager once per registry transition. Component
        // trees may have been created in a different order from registries.
        (BufferIterator<Ts>::setCB(registry ? registry->manager->template
            getOrCreateComponentBuffer<std::remove_const_t<Ts>>() : nullptr), ...);
      }
      ((BufferIterator<Ts>::index_ = RegistryBufferIterator<B>::index_), ...);
    }

    bool current_components_present() const
    {
      return (BufferIterator<Ts>::hasValue() && ...);
    }

    void advance_if_invalid()
    {
      while (!at_end() && !(current_entity_visible() && current_components_present()))
        step_all();
    }
  };

  template <typename B, typename... Ts>
  class View
  {
    Table *table_;

  public:
    View() : View(*current()) {}

    /**
     * @brief Prepares the component columns and defers structural changes.
     * @ownership Borrows the Table; the Table continues to own all entities.
     * @lifetime The Table must outlive the View and its borrowed component data.
     * @thread Call on the Table owner thread; preparation may construct values.
     * @reentrancy If preparation throws, only this View's defer level is released.
     *              Successfully constructed slots remain available for retry.
     * @cost Checks presence words and constructs missing default values per
     *       component column; existing values are reused on repeated Views.
     */
    explicit View(Table &table) : table_(&table)
    {
      table_->begin_defer();
      try
      {
        auto *reg = table.template getOrCreateManager<B>()
                        .template getOrCreateRegistryComponentBuffer<B>();
        ensure_space(reg);
      }
      catch (...)
      {
        table_->end_defer();
        throw;
      }
    }

    ~View() { table_->end_defer(); }

    View(const View &) = delete;
    View &operator=(const View &) = delete;

    /**
     * @brief Gives this View's component slots a value for the first @p n ids.
     *
     * The established behaviour is that every live entity has the components a
     * View names, so the slots are engaged here. Engaging needs only the
     * default constructor of the component types a View actually names, and is
     * skipped for a type that has none: such a component then stays present
     * only where it was constructed explicitly, and the View yields exactly
     * those entities.
     */
    template <typename C>
    static void engageSlots(ComponentBuffer<C> *cb, uint32_t n)
    {
      if (cb == nullptr)
        return;
      cb->ensure_space(n);
      if constexpr (std::is_default_constructible_v<C>)
        cb->engageUpTo(n);
    }

  private:
    // Keep the public prefix helper's raw-buffer contract. View preparation
    // additionally respects registry liveness, including pending destruction.
    template <typename C>
    static void engageEntitySlots(ComponentBuffer<C> *cb, uint32_t n,
                                  IRegistryComponentBuffer *registry)
    {
      cb->ensure_space(n);
      if constexpr (std::is_default_constructible_v<C>)
      {
        const uint64_t revision = cb->manager->entity_revision_;
        if (cb->preparation_valid_ && cb->prepared_size_ == n &&
            cb->prepared_entity_revision_ == revision &&
            cb->prepared_presence_revision_ == cb->presence_revision_)
          return;
        cb->preparation_valid_ = false;
        bool constructed = false;
        for (uint64_t first = 0; first < n; first += 64)
        {
          const uint32_t count = static_cast<uint32_t>(
              std::min<uint64_t>(64, uint64_t(n) - first));
          const uint64_t mask = count == 64 ? ~uint64_t(0)
                                           : (uint64_t(1) << count) - 1;
          const size_t word = static_cast<size_t>(first >> 6);
          if (word < cb->live_.size() && (cb->live_[word] & mask) == mask)
            continue;
          for (uint32_t offset = 0; offset < count; ++offset)
          {
            const uint32_t id = static_cast<uint32_t>(first) + offset;
            if (cb->has(id))
              continue;
            Entity *entity = registry->entity_at(id);
            if (entity && !(entity->flags & kEntityDead))
            {
              constructed = true;
              cb->emplace(id);
            }
          }
        }
        // A construction callback may reset an earlier slot or mutate the
        // registry. Cache only a complete pass that invoked no user code.
        if (!constructed && revision == cb->manager->entity_revision_)
        {
          cb->prepared_size_ = n;
          cb->prepared_entity_revision_ = revision;
          cb->prepared_presence_revision_ = cb->presence_revision_;
          cb->preparation_valid_ = true;
        }
      }
    }

  public:
    void ensure_space(IComponentBuffer *cur)
    {
      IComponentManager *cm = cur->manager;
      [[maybe_unused]]
      const uint32_t n = cur->size();
      [[maybe_unused]]
      auto *registry = dynamic_cast<IRegistryComponentBuffer *>(cur);
      (void)std::initializer_list<int>{
          (engageEntitySlots<std::remove_const_t<Ts>>(
               cm->template getOrCreateComponentBuffer<std::remove_const_t<Ts>>(), n, registry),
           0)...};

      for (auto *child = cur->children; child != nullptr; child = child->next)
        ensure_space(child);
    }

    ViewIterator<B, Ts...> begin()
    {
      return ViewIterator<B, Ts...>(table_->template getOrCreateManager<B>());
    }

    ViewIterator<B, Ts...> end() { return ViewIterator<B, Ts...>(); }
  };

} // namespace ecs

// Opt-in compiled container ownership; standalone users remain header-only.
#if defined(EVE_ECS_EXTERN_CONTAINERS)
#if defined(_WIN32) && defined(EVENGINE_MODULE_DLL) && !defined(EVENGINE_EXPORTS_FOUNDATION)
extern template class __declspec(dllimport) std::map<std::type_index, ecs::IComponentManager*>;
extern template class __declspec(dllimport) std::map<std::type_index, ecs::IComponentSlotBuffer*>;
#else
extern template class std::map<std::type_index, ecs::IComponentManager*>;
extern template class std::map<std::type_index, ecs::IComponentSlotBuffer*>;
#endif

// Handle values keep their original Table/type/id/generation projection and
// borrowed lifetimes. Only Windows DLL non-template container code is shared.
#if defined(_WIN32) && defined(EVENGINE_MODULE_DLL) && !defined(EVENGINE_EXPORTS_FOUNDATION)
extern template class __declspec(dllimport) std::vector<ecs::EntityHandle>;
#endif

#endif
