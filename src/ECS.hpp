#pragma once
#include "zeroerr.hpp"

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <type_traits>
#include <typeindex>

#define COMPONENT(T, name) \
  ecs::ComponentRef<T> name() { return ecs::ComponentRef<T>(this); }

#define OPTIONAL_COMPONENT(T, name) \
  ecs::OptionalComponentRef<T> name() { return ecs::OptionalComponentRef<T>(this); }

#define ENTITY(T, BASE)                                       \
  using super = BASE;                                         \
  ecs::IComponentManager &getComponentManager() const override \
  {                                                           \
    return ecs::ComponentManager<T>::inst();                   \
  }                                                           \
  static T *create() { return ecs::CreateEntity<T>(); }

namespace ecs
{

  class IComponentManager;
  template <typename T>
  class ComponentBuffer;
  template <typename T>
  class RegistryComponentBuffer;
  template <typename T>
  class BufferIterator;

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

    uint32_t id;
    uint32_t flags;
  };


  /**
   * @brief IComponentBuffer 是一个抽象类，用于表示一个存储 Component 数据的容器
   * 这里 IComponentBuffer 使用了类型擦除技术，其具体的子类实现了对应类型的 ComponentBuffer，即：
   * IComponentBuffer -> ComponentBuffer<T>
   *  而 ComponentBuffer<T> 是一个容器模板类，用来存储 T 类型的 Component 数据
   */
  class IComponentBuffer
  {
  public:
    virtual ~IComponentBuffer() = default;
    virtual uint32_t add() = 0;
    virtual void ensure_space(uint32_t) = 0;
    virtual uint32_t size() const = 0;
    virtual const std::type_info &getType() const = 0;

    IComponentManager *manager = nullptr;
    IComponentBuffer *parent = nullptr;
    IComponentBuffer *children = nullptr, *next = nullptr;
  };

  class IEntityIterator
  {
  public:
    virtual IEntityIterator &operator++(int) = 0;
    virtual bool operator==(const IEntityIterator &other) = 0;
    virtual bool operator!=(const IEntityIterator &other) = 0;
    virtual Entity *operator->() = 0;
    virtual Entity &operator*() = 0;
  };
  typedef std::unique_ptr<IEntityIterator> IEntityIteratorPtr;

  /**
   * @brief IRegistryComponentBuffer 是一个额外的抽象接口类，用来表示 Registry 的额外接口
   */
  class IRegistryComponentBuffer
  {
  public:
    virtual Entity *getEntity(uint32_t id) = 0;
    virtual IEntityIteratorPtr beginEntity() = 0;
    virtual IEntityIteratorPtr endEntity() = 0;
  };

  // ------------------------------------------------------------------------


  /**
   * @brief IComponentManager 是一个管理所有 Component 的ComponentManager的抽象接口
   * 
   * IComponentManager 使用了类型擦除技术，其具体的子类实现了对应类型的 ComponentManager，即：
   * IComponentManager -> ComponentManager<T>
   *  而 ComponentManager<T> 是一个单例模板类，用于记录所有 T 类型的 Entity 都有哪些 Component
   */
  class IComponentManager
  {
  public:
    IComponentManager *parent = nullptr;

    IComponentBuffer *registy = nullptr;
    std::map<std::type_index, IComponentBuffer *> components;

    virtual const std::type_info &getType() const = 0;

    template <typename T>
    ComponentBuffer<T> *getComponentBuffer()
    {
      auto it = components.find(std::type_index(typeid(T)));
      if (it == components.end())
      {
        return nullptr;
      }
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
        auto *cb = new ComponentBuffer<T>(
            this, parent ? parent->getComponentBuffer<T>() : nullptr);
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
        registy = new RegistryComponentBuffer<T>(
            this, parent ? parent->getRegistryComponentBuffer<typename T::super>()
                         : nullptr);
      }
      return dynamic_cast<RegistryComponentBuffer<T> *>(registy);
    }
  };

  /**
   * 用户编写的每个类在系统中都会自动创建一个 ComponentManager 来管理其下的所有
   * Component 同时，还会创建一个 Registy 来保存所有创建的类实例
   */
  template <typename B>
  class ComponentManager : public IComponentManager
  {
  public:
    static ComponentManager &inst()
    {
      static ComponentManager instance;
      return instance;
    }

    const std::type_info &getType() const override { return typeid(B); }

    ComponentManager()
    {
      if constexpr (!std::is_same_v<typename B::super, Entity>)
      {
        parent = &ComponentManager<typename B::super>::inst();
      }
    }
  };

  /**
   * 这个 ComponentBuffer
   * 是所有类数据的容器，是最关键的数据结构，对于一个类的继承树结构，我们会创建一系列
   * 相互链接的 ComponentBuffer 来保存每个 Component
   * 的数据，例如，对于下面的类结构，B 是 A 的子类：
   * +-----------------------------------------------+
   * | Class A | Component<Data1> | Component<Data2> |
   * +-----------------------------------------------+
   * |  0      |  Data 1          |    Data 2        |
   * |  1      |  Data 1          |    Data 2        |
   * +-----------------------------------------------+------------------+
   * | Class B | Component<Data1> | Component<Data2> | Component<Data3> |
   * +------------------------------------------------------------------+
   * |  0      |  Data 1          |    Data 2        |    Data 3        |
   * |  1      |  Data 1          |    Data 2        |    Data 3        |
   * +-----------------------------------------------+------------------+
   *
   * 为了实现这个结构，每个类都有一个 ComponentBuffer ，保存了所有的 Component
   * 数据
   */
  template <typename T>
  class CommonComponentBuffer : public IComponentBuffer
  {
  public:
    std::deque<T> container;
    T &get(uint32_t id)
    {
      if (id >= container.size())
      {
        container.resize(id + 1);
      }
      return container.at(id);
    }
    const T &get(uint32_t id) const
    {
      if (id >= container.size())
      {
        container.resize(id + 1);
      }
      return container.at(id);
    }

    uint32_t add() override
    {
      uint32_t id = container.size();
      container.push_back(T{});
      return id;
    }

    uint32_t size() const override { return container.size(); }

    const std::type_info &getType() const override { return typeid(T); }

    void ensure_space(uint32_t new_size) override
    {
      if (new_size > container.size())
      {
        container.resize(new_size);
      }
    }

    CommonComponentBuffer(IComponentManager *cm, IComponentBuffer *pcb)
    {
      manager = cm;

      if (cm->parent != nullptr)
      {
        if (pcb == nullptr)
          return;
        if (pcb->children == nullptr)
        {
          pcb->children = this;
        }
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
      return it == dynamic_cast<const EntityIterator<T> &>(other).it;
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
    RegistryComponentBuffer(IComponentManager *cm, IComponentBuffer *pcb)
        : CommonComponentBuffer<T>(cm, pcb) {}

    Entity *getEntity(uint32_t id) override
    {
      if (id >= this->container.size())
      {
        this->container.resize(id + 1);
      }
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
      auto *reg = cm.template getOrCreateComponentBuffer<T>();
      return (reg);
    }
  };

  template <typename T>
  class OptionalComponentRef
  {
    const Entity *entity;
  public:
    OptionalComponentRef(const Entity *ent) : entity(ent) {}

    IComponentManager &CM() const { return entity->getComponentManager(); }

    // TODO: Implement this with a ComponentMap
  };

  template <typename T>
  T *CreateEntity()
  {
    static RegistryComponentBuffer<T> *registry =
        ComponentManager<T>::inst()
            .template getOrCreateRegistryComponentBuffer<T>();
    uint32_t id = registry->add();
    T &inst = registry->get(id);
    inst.id = id;

    // This piece of code must be done after the entity is created
    // Otherwise, you may not see the components before first entity is created
    const IComponentManager *cm = &ComponentManager<T>::inst();
    for (auto [key, component] : cm->components)
    {
      component->ensure_space(id + 1);
    }

    return &inst;
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
      if (_cb != nullptr)
      {
        this->cb = _cb;
      }

      it = cb->container.begin();

      while (cb != nullptr && !IsValid())
        MoveNext();
    }
    BufferIterator &operator++()
    {
      it++;

      while (cb != nullptr && !IsValid())
        MoveNext();
      return *this;
    }

    bool IsValid()
    {
      if (cb == nullptr)
        return false;
      if (it == cb->container.end())
        return false;
      return true;
    }

    void MoveNext()
    {
      if (it == cb->container.end())
      {
        if (cb->children != nullptr)
        {
          cb = dynamic_cast<CBType *>(cb->children);
          it = cb->container.begin();
        }
        else if (cb->next != nullptr)
        {
          cb = dynamic_cast<CBType *>(cb->next);
          it = cb->container.begin();
        }
        else
        {
          while (cb->parent != nullptr && cb->parent->next == nullptr)
          {
            cb = dynamic_cast<CBType *>(cb->parent);
          }
          if (cb->parent != nullptr)
          {
            cb = dynamic_cast<CBType *>(cb->parent->next);
            it = cb->container.begin();
          }
          else
          {
            cb = nullptr;
          }
        }
      }
    }

    bool operator==(const BufferIterator &other)
    {
      if (cb == nullptr)
        return other.cb == nullptr;
      else
      {
        if (cb == other.cb)
        {
          return it == other.it;
        }
        else
        {
          return false;
        }
      }
    }
    bool operator!=(const BufferIterator &other) { return !(*this == other); }

    T *operator->() { return &*it; }
    T &operator*() { return *it; }

  private:
    CBType *cb = nullptr;
    typename std::deque<T>::iterator it;
  };

  template <typename T>
  class RegistryBufferIterator
  {
  public:
    RegistryBufferIterator() {}
    RegistryBufferIterator(IComponentBuffer *_cb)
    {
      setCB(_cb);

      it = rcb->beginEntity();

      while (cb != nullptr && !IsValid())
        MoveNext();
    }
    RegistryBufferIterator &operator++()
    {
      (*it)++;

      while (cb != nullptr && !IsValid())
        MoveNext();
      return *this;
    }

    bool IsValid()
    {
      if (cb == nullptr)
        return false;
      if (*it == *(rcb->endEntity()))
        return false;
      return true;
    }

    void MoveNext()
    {
      if (*it == *(rcb->endEntity()))
      {
        if (cb->children != nullptr)
        {
          setCB(cb->children);
          it = rcb->beginEntity();
        }
        else if (cb->next != nullptr)
        {
          setCB(cb->next);
          it = rcb->beginEntity();
        }
        else
        {
          while (cb->parent != nullptr && cb->parent->next == nullptr)
          {
            setCB(cb->parent);
          }
          if (cb->parent != nullptr)
          {
            setCB(cb->parent->next);
            it = rcb->beginEntity();
          }
          else
          {
            setCB(nullptr);
          }
        }
      }
    }

    bool operator==(const RegistryBufferIterator &other)
    {
      if (cb == nullptr)
        return other.cb == nullptr;
      else
      {
        if (cb == other.cb)
        {
          return it == other.it;
        }
        else
        {
          return false;
        }
      }
    }
    bool operator!=(const RegistryBufferIterator &other) { return !(*this == other); }

    T *operator->() { return &*it; }
    T &operator*() { return *it; }

    void setCB(IComponentBuffer *_cb)
    {
      if (_cb != nullptr)
      {
        this->cb = _cb;
        this->rcb = dynamic_cast<IRegistryComponentBuffer *>(_cb);
      } else {
        this->cb = nullptr;
        this->rcb = nullptr;
      }
    }

  private:
    IComponentBuffer *cb = nullptr;
    IRegistryComponentBuffer *rcb = nullptr;
    IEntityIteratorPtr it;
  };

  template <typename B, typename... Ts>
  class ViewIterator : public RegistryBufferIterator<B>, public BufferIterator<Ts>...
  {
  public:
    ViewIterator() {}

    ViewIterator(ComponentManager<B> &cm)
        : RegistryBufferIterator<B>(cm.registy),
          BufferIterator<Ts>(cm.template getOrCreateComponentBuffer<
                             std::remove_const_t<Ts>>())...
    {
      std::cout << cm.registy->size() << std::endl;
      uint32_t sizes[] = {
          (cm.template getOrCreateComponentBuffer<std::remove_const_t<Ts>>())
              ->size()...};
      for (int i = 0; i < sizeof...(Ts); i++)
      {
        std::cout << sizes[i] << std::endl;
      }
    }

    ViewIterator &operator++()
    {
      RegistryBufferIterator<B>::operator++();
      (BufferIterator<Ts>::operator++(), ...);
      return *this;
    }
    bool operator==(const ViewIterator &other)
    {
      bool reg = RegistryBufferIterator<B>::operator==(other);
      bool ts[] = {BufferIterator<Ts>::operator==(other)...};
      return reg && std::all_of(ts, ts + sizeof...(Ts), [](bool b)
                                { return b; });
    }
    bool operator!=(const ViewIterator &other) { return !(*this == other); }

    std::tuple<Ts *...> operator*()
    {
      return std::tuple<Ts *...>(BufferIterator<Ts>::operator->()...);
    }
  };

  template <typename B, typename... Ts>
  class View
  {
  public:
    View() { ensure_space(ComponentManager<B>::inst().registy); }
    void ensure_space(IComponentBuffer *cur)
    {
      IComponentManager *cm = cur->manager;
      std::vector<IComponentBuffer *> cbs = {
          cm->template getOrCreateComponentBuffer<std::remove_const_t<Ts>>()...};

      for (auto cb : cbs)
      {
        if (cb != nullptr)
        {
          cb->ensure_space(cur->size());
        }
      }

      if (cur->children != nullptr)
        ensure_space(cur->children);
      if (cur->next != nullptr)
        ensure_space(cur->next);
    }

    ViewIterator<B, Ts...> begin()
    {
      return ViewIterator<B, Ts...>(ComponentManager<B>::inst());
    }
    ViewIterator<B, Ts...> end() { return ViewIterator<B, Ts...>(); }
  };

} // namespace ecs
