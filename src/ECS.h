#pragma once

#include <cstdint>
#include <map>
#include <typeindex>
#include <vector>

namespace fd {

class IComponentRegister {
public:
  virtual ~IComponentRegister() {};
};

template <typename B, typename T> class ComponentRegister;

template <typename B> class ComponentManager {
public:
  static ComponentManager &inst() {
    static ComponentManager instance;
    return instance;
  }

  template <typename T>
  ComponentRegister<B, T> *getOrCreateComponentRegister() {
    auto it = components.find(std::type_index(typeid(T)));
    if (it == components.end()) {
      return new ComponentRegister<B, T>();
    }
    return dynamic_cast<ComponentRegister<B, T> *>(it->second);
  }
  std::map<std::type_index, IComponentRegister *> components;
};

template <typename B, typename T>
class ComponentRegister : public IComponentRegister {
public:
  ComponentRegister() {
    ComponentManager<B>::inst().components[std::type_index(typeid(T))] = this;
  }

  uint32_t add() {
    uint32_t id = container.size();
    container.push_back(T());
    return id;
  }
  T &get(uint32_t id) { return container[id]; }

protected:
  std::vector<T> container;
};

// ------------------------------------------------------------------------

// template <typename T> uint32_t CreateEntity() {
//   static uint32_t id = 0;
//   return id++;
// }

class Entity {
public:
  virtual void release() = 0;
  uint32_t id;
};

template <typename B, typename T> class Component {
public:
  uint32_t ID() { return reinterpret_cast<const uint32_t *>(this)[-1]; }

  T &operator*() { return getReg()->get(ID()); }
  T *operator->() { return &getReg()->get(ID()); }

  static uint32_t add() { getReg()->add(); }

protected:
  static ComponentRegister<B, T> *getReg() {
    auto &cm = ComponentManager<B>::inst();
    auto *reg = cm.getOrCreateComponentRegister<T>();
    return (reg);
  }
};

template <typename T> class OptionalComponent {};

template <typename T> T *CreateEntity() {
  static auto *registry =
      ComponentManager<T>::inst().getOrCreateComponentRegister<T>();
  uint32_t id = registry->add();
  T &inst = registry->get(id);
  inst.id = id;
  return &inst;
}

// ------------------------------------------------------------------------

template <typename B, typename... Ts> class ViewIterator {
public:
  ViewIterator() {}

  ViewIterator &operator++() { return *this; }

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
