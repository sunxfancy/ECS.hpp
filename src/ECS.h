#pragma once

#define ZEROERR_IMPLEMENTATION
#include "zeroerr.hpp"

#include <cstdint>
#include <deque>
#include <map>
#include <type_traits>
#include <typeindex>

#define COMPONENT(T, name)                                                     \
  fd::Component<T> name() { return fd::Component<T>(this); }

#define ENTITY(T, BASE)                                                        \
  using super = BASE;                                                          \
  fd::IComponentManager &getComponentManager() const override {                \
    return fd::ComponentManager<T>::inst();                                    \
  }                                                                            \
  static T *create() { return fd::CreateEntity<T>(); }

namespace fd {

class IComponentManager;
template <typename T> class ComponentBuffer;
template <typename T> class BufferIterator;

class Entity {
public:
  virtual void release() = 0;
  virtual IComponentManager &getComponentManager() const = 0;

  uint32_t id;
};

class IComponentBuffer {
public:
  virtual ~IComponentBuffer() = default;
  virtual uint32_t add() = 0;
  virtual void ensure_space(uint32_t) = 0;

  IComponentManager *manager = nullptr;
  IComponentBuffer *parent = nullptr;
  IComponentBuffer *children = nullptr, *next = nullptr;
};

// ------------------------------------------------------------------------

class IComponentManager {
public:
  IComponentManager *parent = nullptr;

  IComponentBuffer *registy = nullptr;
  std::map<std::type_index, IComponentBuffer *> components;

  template <typename T>
  ComponentBuffer<T> *getOrCreateComponentBuffer(bool isRegisty = false) {
    if (isRegisty) {
      if (registy == nullptr) {
        registy = new ComponentBuffer<T>(this);
      }
      return dynamic_cast<ComponentBuffer<T> *>(registy);
    }

    auto it = components.find(std::type_index(typeid(T)));
    if (it == components.end()) {
      auto *cb = new ComponentBuffer<T>(this);
      components[std::type_index(typeid(T))] = cb;
      return cb;
    }
    return dynamic_cast<ComponentBuffer<T> *>(it->second);
  }
};

/**
 * 用户编写的每个类在系统中都会自动创建一个 ComponentManager 来管理其下的所有
 * Component 同时，还会创建一个 Registy 来保存所有创建的类实例
 */
template <typename B> class ComponentManager : public IComponentManager {
public:
  static ComponentManager &inst() {
    static ComponentManager instance;
    return instance;
  }

  ComponentManager() {
    if constexpr (!std::is_same_v<typename B::super, Entity>) {
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

template <typename T> class ComponentBuffer : public IComponentBuffer {
public:
  std::deque<T> container;
  T &get(uint32_t id) {
    if (id >= container.size()) {
      container.resize(id + 1);
    }
    return container.at(id);
  }
  const T &get(uint32_t id) const {
    if (id >= container.size()) {
      container.resize(id + 1);
    }
    return container.at(id);
  }

  uint32_t add() override {
    uint32_t id = container.size();
    printf("Size = %llu\n", sizeof(T));
    container.push_back(T{});
    return id;
  }

  void ensure_space(uint32_t id) override {
    if (id >= container.size()) {
      container.resize(id + 1);
    }
  }

  ComponentBuffer(IComponentManager *cm) {
    manager = cm;

    if (cm->parent != nullptr) {
      IComponentBuffer *pcb = cm->parent->getOrCreateComponentBuffer<T>();
      if (pcb->children == nullptr) {
        pcb->children = this;
      } else {
        IComponentBuffer *old_head = pcb->children;
        pcb->children = this;
        this->next = old_head;
      }
      parent = pcb;
    }
  }

  BufferIterator<T> begin() { return BufferIterator<T>(this); }
  BufferIterator<T> end() { return BufferIterator<T>(); }
};

// ------------------------------------------------------------------------

template <typename T> class Component {
  const Entity *entity;

public:
  Component(const Entity *ent) : entity(ent) {}

  IComponentManager &CM() const { return entity->getComponentManager(); }

  T &operator*() const { return getBuffer(CM())->get(entity->id); }
  T *operator->() const { return &(getBuffer(CM())->get(entity->id)); }

  static ComponentBuffer<T> *getBuffer(IComponentManager &cm) {
    auto *reg = cm.template getOrCreateComponentBuffer<T>();
    return (reg);
  }
};

template <typename T> class OptionalComponent {};

template <typename T> class Registry {
public:
  T instance;
  uint32_t flags;
};

template <typename T> T *CreateEntity() {
  static ComponentBuffer<T> *registry =
      ComponentManager<T>::inst().template getOrCreateComponentBuffer<T>(true);
  uint32_t id = registry->add();
  T &inst = registry->get(id);
  inst.id = id;
  printf("registry ptr: %p\n", registry);
  printf("this ptr: %p\n", &inst);
  printf("id: %d\n", id);

  // This piece of code must be done after the entity is created
  // Otherwise, you may not see the components before first entity is created
  const IComponentManager *cm = &ComponentManager<T>::inst();
  while (cm != nullptr) {
    for (auto [key, component] : cm->components) {
      component->ensure_space(id);
    }
    cm = cm->parent;
  }

  return &inst;
}

// ------------------------------------------------------------------------

template <typename T> class BufferIterator {
public:
  using CBType = ComponentBuffer<std::remove_const_t<T>>;
  BufferIterator() {}
  BufferIterator(CBType *cb) : cb(cb) {}
  BufferIterator &operator++() {
    it++;
    if (it == cb->container.end()) {
      if (cb->children != nullptr) {
        cb = dynamic_cast<CBType*>(cb->children);
        it = cb->container.begin();
      } else if (cb->next != nullptr) {
        cb = dynamic_cast<CBType*>(cb->next);
        it = cb->container.begin();
      } else {
        while (cb->parent != nullptr && cb->parent->next == nullptr) {
          cb = dynamic_cast<CBType*>(cb->parent);
        }
        if (cb->parent != nullptr) {
          cb = dynamic_cast<CBType*>(cb->parent->next);
          it = cb->container.begin();
        } else {
          cb = nullptr;
        }
      }
    }

    return *this;
  }

  bool operator==(const BufferIterator &other) {
    if (cb == nullptr)
      return other.cb == nullptr;
    else {
      if (cb == other.cb) {
        return it == other.it;
      } else {
        return false;
      }
    }
  }
  bool operator!=(const BufferIterator &other) { return !(it == other.it); }

  T *operator->() { return it.get(); }
  T &operator*() { return *it; }

private:
  CBType *cb;
  std::deque<T>::iterator it;
};

template <typename B, typename... Ts>
class ViewIterator : public BufferIterator<B>, public BufferIterator<Ts>... {
public:
  ViewIterator() {}
  ViewIterator &operator++() {
    BufferIterator<B>::operator++();
    (BufferIterator<Ts>::operator++(),...);
    return *this;
  }

  bool operator!=(const ViewIterator &other) { return false; }

  std::tuple<Ts *...> operator*() { return std::tuple<Ts *...>(); }
};

template <typename B, typename... Ts> class View {
public:
  View() {}

  ViewIterator<B, Ts...> begin() { return ViewIterator<B, Ts...>(); }
  ViewIterator<B, Ts...> end() { return ViewIterator<B, Ts...>(); }
};

} // namespace fd
