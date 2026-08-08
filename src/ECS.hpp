#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <iostream>
#include <map>
#include <memory>
#include <tuple>
#include <type_traits>
#include <typeindex>
#include <vector>

#define COMPONENT(T, name) \
  ecs::ComponentRef<T> name() { return ecs::ComponentRef<T>(this); }

#define OPTIONAL_COMPONENT(T, name) \
  ecs::OptionalComponentRef<T> name() { return ecs::OptionalComponentRef<T>(this); }

#define ENTITY(T, BASE)                                       \
  using super = BASE;                                         \
  ecs::IComponentManager &getComponentManager() const override \
  {                                                           \
    return *storage;                                          \
  }                                                           \
  static T *create() { return ecs::CreateEntity<T>(); }        \
  static T *create(ecs::Table &tbl) { return ecs::CreateEntity<T>(tbl); }

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

    friend void begin_defer(Table &);
    friend void end_defer(Table &);
    friend void DestroyEntity(Entity *);
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
  class IComponentBuffer
  {
  public:
    virtual ~IComponentBuffer() = default;
    virtual uint32_t add() = 0;
    virtual void ensure_space(uint32_t) = 0;
    virtual uint32_t size() const = 0;
    virtual const std::type_info &getType() const = 0;
    virtual void copy_slot(uint32_t from_id, uint32_t to_id,
                           IComponentBuffer *from) = 0;
    virtual IComponentBuffer *ensure_equivalent(IComponentManager *dst_cm) = 0;
    virtual void reset_slot(uint32_t id) = 0;

    IComponentManager *manager = nullptr;
    IComponentBuffer *parent = nullptr;
    IComponentBuffer *children = nullptr, *next = nullptr;
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
  public:
    Table *table = nullptr;
    IComponentManager *parent = nullptr;
    bool staging = false;

    IComponentBuffer *registy = nullptr;
    std::map<std::type_index, IComponentBuffer *> components;

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
        uint32_t mid = m_reg->add();
        m_reg->container[mid] = std::move(s_reg->container[sid]);
        B &me = m_reg->container[mid];
        me.id = mid;
        me.flags &= ~(kEntityStaging | kEntityDead);
        me.storage = this;
        me.generation = preserved_gen;
        me.table = table;

        for (auto &[key, s_buf] : staging_cm->components)
        {
          (void)key;
          IComponentBuffer *m_buf = s_buf->ensure_equivalent(this);
          m_buf->copy_slot(sid, mid, s_buf);
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

  template <typename T>
  class CommonComponentBuffer : public IComponentBuffer
  {
  public:
    std::deque<T> container;

    T &get(uint32_t id)
    {
      if (id >= container.size())
        container.resize(id + 1);
      return container.at(id);
    }

    const T &get(uint32_t id) const
    {
      if (id >= container.size())
        container.resize(id + 1);
      return container.at(id);
    }

    uint32_t add() override
    {
      uint32_t id = static_cast<uint32_t>(container.size());
      container.push_back(T{});
      return id;
    }

    uint32_t size() const override { return static_cast<uint32_t>(container.size()); }

    const std::type_info &getType() const override { return typeid(T); }

    void ensure_space(uint32_t new_size) override
    {
      if (new_size > container.size())
        container.resize(new_size);
    }

    void copy_slot(uint32_t from_id, uint32_t to_id,
                   IComponentBuffer *from) override
    {
      auto *fb = dynamic_cast<CommonComponentBuffer<T> *>(from);
      get(to_id) = fb->get(from_id);
    }

    IComponentBuffer *ensure_equivalent(IComponentManager *dst_cm) override
    {
      return dst_cm->template getOrCreateComponentBuffer<T>();
    }

    void reset_slot(uint32_t id) override
    {
      if (id < container.size())
        container[id] = T{};
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
  };

  template <typename T>
  class ComponentBuffer : public CommonComponentBuffer<T>
  {
  public:
    ComponentBuffer(IComponentManager *cm, IComponentBuffer *pcb)
        : CommonComponentBuffer<T>(cm, pcb) {}

    BufferIterator<T> begin() { return BufferIterator<T>(this); }
    BufferIterator<T> end() { return BufferIterator<T>(); }
  };

  template <typename T>
  class EntityIterator : public IEntityIterator
  {
  public:
    EntityIterator(typename std::deque<T>::iterator it) : it(it) {}
    typename std::deque<T>::iterator it;

    IEntityIterator &operator++(int) override
    {
      it++;
      return *this;
    }

    bool operator==(const IEntityIterator &other) override
    {
      auto *o = dynamic_cast<const EntityIterator<T> *>(&other);
      if (o == nullptr)
        return false;
      return it == o->it;
    }

    bool operator!=(const IEntityIterator &other) override
    {
      return !(*this == other);
    }

    Entity *operator->() override { return &*it; }
    Entity &operator*() override { return *it; }
  };

  template <typename T>
  class RegistryComponentBuffer : public CommonComponentBuffer<T>,
                                  public IRegistryComponentBuffer
  {
  public:
    std::vector<uint32_t> freelist_;

    RegistryComponentBuffer(IComponentManager *cm, IComponentBuffer *pcb)
        : CommonComponentBuffer<T>(cm, pcb) {}

    uint32_t add() override
    {
      if (!freelist_.empty())
      {
        uint32_t id = freelist_.back();
        freelist_.pop_back();
        uint32_t preserved_gen = this->container[id].generation;
        this->container[id] = T{};
        this->container[id].generation = preserved_gen;
        return id;
      }
      return CommonComponentBuffer<T>::add();
    }

    Entity *getEntity(uint32_t id) override
    {
      if (id >= this->container.size())
        this->container.resize(id + 1);
      return &this->container.at(id);
    }

    IEntityIteratorPtr beginEntity() override
    {
      return IEntityIteratorPtr(new EntityIterator<T>(this->container.begin()));
    }

    IEntityIteratorPtr endEntity() override
    {
      return IEntityIteratorPtr(new EntityIterator<T>(this->container.end()));
    }

    void reclaim(uint32_t id) override
    {
      if (id >= this->container.size())
        return;

      freelist_.push_back(id);

      for (auto &[key, buf] : this->manager->components)
      {
        (void)key;
        buf->reset_slot(id);
      }

    }

    uint32_t entity_count() const override
    {
      return static_cast<uint32_t>(this->container.size());
    }

    Entity *entity_at(uint32_t index) override
    {
      if (index >= this->container.size())
        return nullptr;
      return &this->container[index];
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

    T &operator*() const { return getBuffer(CM())->get(entity->id); }
    T *operator->() const { return &(getBuffer(CM())->get(entity->id)); }

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
    for (auto &[type, staging_cm] : staging_managers_)
    {
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

  template <typename T>
  T *CreateEntity(Table &table, bool force_main = false)
  {
    const bool use_staging = table.is_deferred() && !force_main;

    IComponentManager &cm = use_staging
                                ? static_cast<IComponentManager &>(
                                      table.template getOrCreateStagingManager<T>())
                                : static_cast<IComponentManager &>(
                                      table.template getOrCreateManager<T>());

    auto *registry = cm.template getOrCreateRegistryComponentBuffer<T>();
    uint32_t id = registry->add();
    T &inst = registry->get(id);
    inst.id = id;
    inst.table = &table;
    inst.storage = &cm;
    if (use_staging)
      inst.flags |= kEntityStaging;
    if (inst.generation == 0)
      inst.generation = 1;

    for (auto &[key, component] : cm.components)
    {
      (void)key;
      component->ensure_space(id + 1);
    }

    return &inst;
  }

  template <typename T>
  T *CreateEntity()
  {
    return CreateEntity<T>(*current());
  }

  inline void DestroyEntity(Entity *e)
  {
    if (e == nullptr || e->table == nullptr)
      return;
    if (e->flags & kEntityDead)
      return;

    e->generation++;
    e->flags |= kEntityDead;

    Table *table = e->table;
    const bool is_staging = (e->flags & kEntityStaging) != 0;

    if (table->is_deferred())
    {
      if (!is_staging)
      {
        table->pending_destroys_.push_back(
            {e->getComponentManager().getType(), e->id, e->generation, false});
      }
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

    return e;
  }

  // ------------------------------------------------------------------------

  template <typename T>
  class BufferIterator
  {
  public:
    using CBType = ComponentBuffer<std::remove_const_t<T>>;

    BufferIterator() {}
    BufferIterator(CBType *_cb)
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
        return it == other.it;
      return false;
    }

    bool operator!=(const BufferIterator &other) const { return !(*this == other); }

    T *operator->() { return cb ? &*it : nullptr; }
    T &operator*() { return *it; }

    CBType *buffer() const { return cb; }

    bool exhausted() const { return cb == nullptr; }

    void step()
    {
      if (cb == nullptr)
        return;
      if (it != cb->container.end())
        ++it;
      while (cb != nullptr && it == cb->container.end())
        hop();
    }

  private:
    void setCB(CBType *_cb)
    {
      cb = _cb;
      if (cb != nullptr)
        it = cb->container.begin();
    }

    void hop()
    {
      if (cb == nullptr)
        return;
      if (cb->children != nullptr)
      {
        setCB(dynamic_cast<CBType *>(cb->children));
      }
      else if (cb->next != nullptr)
      {
        setCB(dynamic_cast<CBType *>(cb->next));
      }
      else
      {
        while (cb->parent != nullptr && cb->parent->next == nullptr)
          cb = dynamic_cast<CBType *>(cb->parent);
        if (cb != nullptr && cb->parent != nullptr)
          setCB(dynamic_cast<CBType *>(cb->parent->next));
        else
          cb = nullptr;
      }
    }

    CBType *cb = nullptr;
    typename std::deque<T>::iterator it;
  };

  template <typename T>
  class RegistryBufferIterator
  {
  public:
    RegistryBufferIterator() {}
    RegistryBufferIterator(IComponentBuffer *_cb) { setCB(_cb); }

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
        return it == other.it;
      return false;
    }

    bool operator!=(const RegistryBufferIterator &other) const
    {
      return !(*this == other);
    }

    T *operator->()
    {
      return cb ? static_cast<T *>(it->operator->()) : nullptr;
    }

    T &operator*()
    {
      return static_cast<T &>(it->operator*());
    }

    Entity *entity() const
    {
      if (cb == nullptr || !it || at_current_end())
        return nullptr;
      return it->operator->();
    }

    bool at_end() const { return cb == nullptr; }

    bool at_current_end() const
    {
      if (cb == nullptr || rcb == nullptr || !it)
        return true;
      return *it == *(rcb->endEntity());
    }

    void step()
    {
      if (cb == nullptr)
        return;
      if (!at_current_end())
        (*it)++;
      while (cb != nullptr && at_current_end())
        hop();
    }

    void setCB(IComponentBuffer *_cb)
    {
      if (_cb != nullptr)
      {
        cb = _cb;
        rcb = dynamic_cast<IRegistryComponentBuffer *>(_cb);
        it = rcb ? rcb->beginEntity() : IEntityIteratorPtr();
      }
      else
      {
        cb = nullptr;
        rcb = nullptr;
        it = IEntityIteratorPtr();
      }
    }

    IComponentBuffer *registry_cb() const { return cb; }

  private:
    void hop()
    {
      if (cb == nullptr)
        return;
      if (cb->children != nullptr)
      {
        setCB(cb->children);
      }
      else if (cb->next != nullptr)
      {
        setCB(cb->next);
      }
      else
      {
        while (cb->parent != nullptr && cb->parent->next == nullptr)
          setCB(cb->parent);
        if (cb != nullptr && cb->parent != nullptr)
          setCB(cb->parent->next);
        else
          setCB(nullptr);
      }
    }

    IComponentBuffer *cb = nullptr;
    IRegistryComponentBuffer *rcb = nullptr;
    IEntityIteratorPtr it;
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
      bool reg = RegistryBufferIterator<B>::operator==(other);
      bool ts[] = {BufferIterator<Ts>::operator==(other)...};
      return reg && std::all_of(ts, ts + sizeof...(Ts), [](bool b) { return b; });
    }

    bool operator!=(const ViewIterator &other) const { return !(*this == other); }

    std::tuple<Ts *...> operator*()
    {
      return std::tuple<Ts *...>(BufferIterator<Ts>::operator->()...);
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
      RegistryBufferIterator<B>::operator++();
      (BufferIterator<Ts>::operator++(), ...);
    }

    void advance_if_invalid()
    {
      while (!at_end() && !current_entity_visible())
        step_all();
    }
  };

  template <typename B, typename... Ts>
  class View
  {
    Table *table_;

  public:
    View() : View(*current()) {}

    explicit View(Table &table) : table_(&table)
    {
      table_->begin_defer();
      auto *reg = table.template getOrCreateManager<B>()
                      .template getOrCreateRegistryComponentBuffer<B>();
      ensure_space(reg);
    }

    ~View() { table_->end_defer(); }

    View(const View &) = delete;
    View &operator=(const View &) = delete;

    void ensure_space(IComponentBuffer *cur)
    {
      IComponentManager *cm = cur->manager;
      std::vector<IComponentBuffer *> cbs = {
          cm->template getOrCreateComponentBuffer<std::remove_const_t<Ts>>()...};

      for (auto cb : cbs)
      {
        if (cb != nullptr)
          cb->ensure_space(cur->size());
      }

      if (cur->children != nullptr)
        ensure_space(cur->children);
      if (cur->next != nullptr)
        ensure_space(cur->next);
    }

    ViewIterator<B, Ts...> begin()
    {
      return ViewIterator<B, Ts...>(table_->template getOrCreateManager<B>());
    }

    ViewIterator<B, Ts...> end() { return ViewIterator<B, Ts...>(); }
  };

} // namespace ecs
